// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TST-53: the point interpolation (IND-41) against hand-calculated grids
// (one, two and many points, coincident points, power and radius limits),
// measured cells equal to their points, the confidence mask (IND-43), the
// same result with and without SIMD and with any thread count; and over a
// project: the map of the shared point list, storage and staleness, hover
// readout, the floor-area basis (EXP-06) and the background job (IND-45:
// progress, cancel, deterministic results).
//
// Job and floor-area cases adapted from yantra-v2 68ea20a:
// tests/survey/test_survey_interpolation.cpp (IND-41..45).

#include "survey_helpers.h"
#include "yantra/core/survey/interpolation.h"
#include "yantra/core/survey/sample.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>

namespace yu = yantra::util;

namespace {

/// A one-shot gate between threads.
class Latch {
public:
    void open() {
        const std::lock_guard l(m_);
        open_ = true;
        cv_.notify_all();
    }
    void wait() {
        std::unique_lock l(m_);
        cv_.wait(l, [&] { return open_; });
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    bool open_ = false;
};

/// Plan units of the fixture floors (0.05 m per unit).
bm::Point2 pu(double x_m, double y_m) { return {x_m * 20.0, y_m * 20.0}; }

/// Adds manual points at `metres` with `levels` (dBm, 390 MHz) on `floor`.
std::int64_t add_points(pj::Project& p, const std::string& floor, const std::vector<bm::Point2>& metres,
                        const std::vector<double>& levels) {
    pj::Session s;
    s.floor_id = floor;
    s.kind = "manual";
    s.name = "Points";
    const std::int64_t id = p.add_session(s);
    std::vector<pj::StoredPoint> pts;
    for (std::size_t i = 0; i < metres.size(); ++i) {
        pj::StoredPoint sp;
        sp.session_id = id;
        sp.sequence = static_cast<std::int64_t>(i + 1);
        sp.time_ns = static_cast<std::int64_t>(i + 1) * kS;
        sp.utc_ns = kUtc0 + sp.time_ns;
        sp.plan = pu(metres[i].x, metres[i].y);
        sp.kind = "manual";
        sp.group = static_cast<std::uint32_t>(i + 1);
        sp.sample = ys::sample_json(level_cycle(sp.time_ns, levels[i])).dump();
        pts.push_back(sp);
    }
    p.add_points(id, pts);
    return id;
}

template <class F>
pj::ProjectError::Code code_of(F&& f) {
    try {
        f();
    } catch (const pj::ProjectError& e) {
        return e.code();
    }
    FAIL("no ProjectError");
    return pj::ProjectError::Code::Invalid;
}

/// A row of `n` cells of 1 m from (0, 0).
ys::Grid row_grid(int n) { return {{0.0, 0.0}, 1.0, n, 1}; }

pj::InterpolationSettings settings(double power = 2.0, double radius = 10.0, double full = 2.0) {
    pj::InterpolationSettings s;
    s.power = power;
    s.radius_m = radius;
    s.full_confidence_m = full;
    return s;
}

constexpr float kNoValue = -std::numeric_limits<float>::infinity();

/// The ground floor (20 x 10 m) of the survey fixture with a grid of
/// manual points on its left half.
struct MapFixture : Fixture {
    std::vector<bm::Point2> pts;
    std::vector<double> levels;
    std::int64_t session = 0;
    explicit MapFixture(const std::string& name) : Fixture(name) {
        for (double y = 1.0; y < 10.0; y += 2.0)
            for (double x = 0.5; x <= 9.0; x += 1.5) {
                pts.push_back({x, y});
                levels.push_back(-50.0 - 2.0 * x - 0.5 * y);
            }
        session = add_points(*p, ground, pts, levels);
    }
    ys::InterpolationRequest request() const {
        ys::InterpolationRequest r;
        r.floor_id = ground;
        r.metric.value = ys::Metric::Value::Level;
        return r;
    }
};

}  // namespace

TEST_CASE("TST-53 IDW: one and two points, power and radius limits, the mask (hand-calculated)") {
    // One point in cell 0: the cell takes it; the others too (one weight).
    ys::IdwField f = ys::idw(row_grid(4), {{{0.5, 0.5}, -60.0}}, settings());
    REQUIRE(f.value_db.size() == 4);
    CHECK(f.value_db[0] == -60.0f);
    CHECK(f.measured == std::vector<std::uint8_t>{1, 0, 0, 0});
    for (int i = 1; i < 4; ++i) CHECK(f.value_db[i] == doctest::Approx(-60.0));
    // Mask: 1 up to 2 m, then (10 - d) / (10 - 2).
    CHECK(f.confidence[1] == 1.0f);
    CHECK(f.confidence[2] == 1.0f);
    CHECK(f.confidence[3] == doctest::Approx(0.875));

    // Two points 3 m apart: weights 1/d^2.
    const std::vector<ys::IdwSample> two{{{0.5, 0.5}, -60.0}, {{3.5, 0.5}, -80.0}};
    f = ys::idw(row_grid(4), two, settings());
    CHECK(f.value_db[0] == -60.0f);
    CHECK(f.value_db[3] == -80.0f);
    CHECK(f.value_db[1] == doctest::Approx(-64.0));  // (-60 * 1 - 80 / 4) / 1.25
    CHECK(f.value_db[2] == doctest::Approx(-76.0));
    // Power 1 and 3 (the general power path).
    f = ys::idw(row_grid(4), two, settings(1.0));
    CHECK(f.value_db[1] == doctest::Approx(-200.0 / 3.0));  // (-60 - 80 / 2) / 1.5
    f = ys::idw(row_grid(4), two, settings(3.0));
    CHECK(f.value_db[1] == doctest::Approx(-70.0 / 1.125));  // (-60 - 80 / 8) / 1.125
    // Radius 1.5 m: each cell sees only its near point.
    f = ys::idw(row_grid(4), two, settings(2.0, 1.5, 0.5));
    CHECK(f.value_db[1] == doctest::Approx(-60.0));
    CHECK(f.value_db[2] == doctest::Approx(-80.0));
    CHECK(f.confidence[1] == doctest::Approx(0.5));  // (1.5 - 1) / (1.5 - 0.5)
    // Beyond the radius of every point: no value, no confidence.
    f = ys::idw(row_grid(8), {{{0.5, 0.5}, -60.0}, {{7.5, 0.5}, -80.0}}, settings(2.0, 1.5, 0.5));
    CHECK(f.value_db[3] == kNoValue);
    CHECK(f.value_db[4] == kNoValue);
    CHECK(f.confidence[3] == 0.0f);
    CHECK(f.value_db[1] == doctest::Approx(-60.0));
    // A point exactly at the radius counts, with confidence 0.
    f = ys::idw(row_grid(3), {{{0.5, 0.5}, -60.0}}, settings(2.0, 2.0, 1.0));
    CHECK(f.value_db[2] == doctest::Approx(-60.0));
    CHECK(f.confidence[2] == 0.0f);
    // Invalid settings are refused.
    CHECK_THROWS_AS(ys::idw(row_grid(2), two, settings(0.1)), std::invalid_argument);
    CHECK_THROWS_AS(ys::idw(row_grid(2), two, settings(2.0, 0.1)), std::invalid_argument);
    CHECK_THROWS_AS(ys::idw(row_grid(2), two, settings(2.0, 5.0, 5.0)), std::invalid_argument);
    auto bad_grid = settings();
    bad_grid.grid_m = 0.0;
    CHECK_THROWS_AS(ys::idw(row_grid(2), two, bad_grid), std::invalid_argument);
}

TEST_CASE("TST-53 IDW: many points and coincident points (hand-calculated), measured cells exact") {
    // 5 x 5 cells of 1 m; the centre cell (2.5, 2.5) sees four points.
    const ys::Grid g{{0.0, 0.0}, 1.0, 5, 5};
    const std::vector<ys::IdwSample> many{
        {{2.5, 0.5}, -70.0}, {{4.5, 2.5}, -80.0}, {{0.5, 0.5}, -90.0}, {{2.5, 3.5}, -60.0}};
    ys::IdwField f = ys::idw(g, many, settings());
    // Weights 1/4, 1/4, 1/8, 1: (-17.5 - 20 - 11.25 - 60) / 1.625.
    CHECK(f.value_db[g.index(2, 2)] == doctest::Approx(-108.75 / 1.625));
    // Equidistant points: their mean.
    f = ys::idw(g, {{{1.5, 1.5}, -60.0}, {{3.5, 1.5}, -70.0}, {{1.5, 3.5}, -80.0}, {{3.5, 3.5}, -90.0}}, settings());
    CHECK(f.value_db[g.index(2, 2)] == doctest::Approx(-75.0));

    // Coincident points: the cell takes the median (two: their mean); a
    // neighbour weighs them equally.
    f = ys::idw(row_grid(3), {{{0.5, 0.5}, -60.0}, {{0.5, 0.5}, -70.0}}, settings());
    CHECK(f.value_db[0] == -65.0f);
    CHECK(f.value_db[1] == doctest::Approx(-65.0));
    f = ys::idw(row_grid(3), {{{0.5, 0.5}, -60.0}, {{0.5, 0.5}, -70.0}, {{0.9, 0.2}, -90.0}}, settings());
    CHECK(f.value_db[0] == -70.0f);  // median of -60, -70, -90 in the cell
    CHECK(f.measured[0] == 1);

    // Random points: every measured cell equals its points' median.
    std::mt19937 rng(53);
    std::uniform_real_distribution<double> x(0.0, 20.0), y(0.0, 10.0), v(-110.0, -40.0);
    std::vector<ys::IdwSample> rnd;
    for (int i = 0; i < 300; ++i) rnd.push_back({{x(rng), y(rng)}, v(rng)});
    const ys::Grid big{{0.0, 0.0}, 0.5, 40, 20};
    f = ys::idw(big, rnd, settings());
    std::map<std::size_t, std::vector<double>> in_cell;
    for (const auto& s : rnd) in_cell[*big.cell_at(s.position_m)].push_back(s.value_db);
    std::size_t measured = 0;
    for (std::size_t i = 0; i < big.size(); ++i) measured += f.measured[i];
    CHECK(measured == in_cell.size());
    for (auto& [cell, values] : in_cell) {
        std::sort(values.begin(), values.end());
        const std::size_t n = values.size();
        const double median = n % 2 == 1 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
        CHECK(f.measured[cell] == 1);
        CHECK(f.value_db[cell] == static_cast<float>(median));
        CHECK(f.confidence[cell] == 1.0f);
    }
    // The mask falls with the distance to the nearest point.
    f = ys::idw(row_grid(12), {{{0.5, 0.5}, -60.0}}, settings());
    for (int i = 1; i < 12; ++i) CHECK(f.confidence[static_cast<std::size_t>(i)] <= f.confidence[static_cast<std::size_t>(i - 1)]);
    CHECK(f.confidence[6] == doctest::Approx(0.5));  // d = 6: (10 - 6) / 8
    CHECK(f.confidence[11] == 0.0f);
}

TEST_CASE("TST-53 IDW: the same field with and without SIMD and with any thread count") {
    std::mt19937 rng(41);
    std::uniform_real_distribution<double> x(-2.0, 62.0), y(-2.0, 32.0), v(-110.0, -40.0);
    std::vector<ys::IdwSample> s;
    for (int i = 0; i < 2000; ++i) s.push_back({{x(rng), y(rng)}, v(rng)});
    const ys::Grid g{{0.0, 0.0}, 0.5, 120, 60};
    for (double power : {2.0, 2.5}) {
        CAPTURE(power);
        ys::RunControl base;
        base.isa = ys::Isa::Scalar;
        base.threads = 1;
        const ys::IdwField want = ys::idw(g, s, settings(power), base);
        for (ys::Isa isa : {ys::Isa::Scalar, ys::Isa::Simd})
            for (unsigned threads : {0U, 1U, 3U, 7U}) {
                ys::RunControl run;
                run.isa = isa;
                run.threads = threads;
                const ys::IdwField got = ys::idw(g, s, settings(power), run);
                CHECK(std::memcmp(got.value_db.data(), want.value_db.data(), want.value_db.size() * sizeof(float)) == 0);
                CHECK(got.confidence == want.confidence);
                CHECK(got.measured == want.measured);
            }
    }
    // Progress is reported and ends at 1; a set cancel flag stops the run.
    std::vector<double> seen;
    ys::RunControl run;
    run.progress = [&](double f) { seen.push_back(f); };
    (void)ys::idw(g, s, settings(), run);
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.back() == 1.0);
    std::atomic<bool> stop{true};
    ys::RunControl cancelled;
    cancelled.cancel = &stop;
    CHECK(ys::idw(g, s, settings(), cancelled).cancelled);
}

TEST_CASE("IND-41, IND-43 interpolated map of a floor: the project's points and settings, readout, inputs checked") {
    MapFixture fx("interp-map");
    const auto r = ys::interpolate(*fx.p, fx.request());
    REQUIRE(r);
    CHECK(r->label() == "interpolated");
    CHECK(r->params == pj::InterpolationSettings{});
    CHECK(r->grid == ys::Grid{{0.0, 0.0}, 0.5, 40, 20});  // 20 x 10 m at the default 0.5 m
    CHECK(r->floor_cells == 800);
    REQUIRE(r->points.size() == fx.pts.size());
    for (std::size_t i = 0; i < fx.pts.size(); ++i) {
        const auto cell = r->grid.cell_at(fx.pts[i]);
        REQUIRE(cell);
        CHECK(r->measured[*cell] == 1);
        CHECK(r->value_dbm[*cell] == doctest::Approx(fx.levels[i]).epsilon(1e-6));
        const auto ro = ys::readout(*r, fx.pts[i]);
        CHECK(ro.measured);
        CHECK(ro.label == "measured");
        CHECK(ro.nearest_m == doctest::Approx(0.0));
    }
    // Between points: interpolated, between the neighbours' levels.
    const auto mid = ys::readout(*r, {1.25, 1.0});
    CHECK(mid.on_map);
    CHECK_FALSE(mid.measured);
    CHECK(mid.label == "interpolated");
    REQUIRE(mid.value_dbm);
    CHECK(*mid.value_dbm < -50.5);
    CHECK(*mid.value_dbm > -70.0);
    CHECK(mid.confidence == 1.0);
    // Far right: more than 10 m from every point - no value.
    const auto far = ys::readout(*r, {19.9, 0.2});
    CHECK(far.on_map);
    CHECK_FALSE(far.value_dbm);
    CHECK(far.confidence == 0.0);
    CHECK_FALSE(ys::readout(*r, {-1.0, 1.0}).on_map);
    // The colours: alpha is the confidence.
    const auto colours = ys::cell_colours(*r, ys::ColourScale{});
    CHECK((colours[*r->grid.cell_at(fx.pts[0])] >> 24) == 255U);
    CHECK((colours[*r->grid.cell_at({19.9, 0.2})] >> 24) == 0U);

    // The request's own settings win over the project's.
    auto req = fx.request();
    req.settings = settings(1.0, 4.0, 1.0);
    req.settings->grid_m = 1.0;
    const auto r1 = ys::interpolate(*fx.p, req);
    REQUIRE(r1);
    CHECK(r1->params == *req.settings);
    CHECK(r1->grid.cols == 20);
    // Inputs are checked.
    auto bad = fx.request();
    bad.metric.value = ys::Metric::Value::C1;
    CHECK(code_of([&] { (void)ys::interpolate(*fx.p, bad); }) == pj::ProjectError::Code::Invalid);
    bad = fx.request();
    bad.floor_id = "floor-99";
    CHECK(code_of([&] { (void)ys::interpolate(*fx.p, bad); }) == pj::ProjectError::Code::NotFound);
    bad = fx.request();
    bad.floor_id = fx.first;  // no points
    CHECK(code_of([&] { (void)ys::interpolate(*fx.p, bad); }) == pj::ProjectError::Code::Invalid);
    bad = fx.request();
    bad.settings = settings(9.0);
    CHECK(code_of([&] { (void)ys::interpolate(*fx.p, bad); }) == pj::ProjectError::Code::Invalid);
}

TEST_CASE("IND-45 stored interpolation: round trip, replace by key, staleness") {
    MapFixture fx("interp-store");
    const auto r = ys::interpolate(*fx.p, fx.request());
    REQUIRE(r);
    const auto id = ys::store(*fx.p, *r);
    const auto back = ys::stored_interpolation(*fx.p, id);
    REQUIRE(back);
    CHECK(back->id == id);
    CHECK_FALSE(back->created_utc.empty());
    auto want = *r;
    want.id = id;
    want.created_utc = back->created_utc;
    want.created_with = back->created_with;
    CHECK(*back == want);
    CHECK(ys::stored_interpolations(*fx.p, fx.ground).size() == 1);
    // Stored again under the same key: replaced.
    const auto id2 = ys::store(*fx.p, *r);
    CHECK(ys::stored_interpolations(*fx.p, fx.ground).size() == 1);
    CHECK_FALSE(ys::stored_interpolation(*fx.p, id));
    // Fresh until an input changes; stays usable when stale.
    CHECK_FALSE(ys::staleness(*fx.p, *back).stale);
    auto s = fx.p->interpolation_settings();
    s.radius_m = 12.0;
    fx.p->set_interpolation_settings(s);
    auto st = ys::staleness(*fx.p, *back);
    CHECK(st.stale);
    REQUIRE(st.reasons.size() == 1);
    CHECK(st.reasons[0] == "interpolation settings changed");
    fx.p->set_interpolation_settings({});
    CHECK_FALSE(ys::staleness(*fx.p, *back).stale);
    add_points(*fx.p, fx.ground, {{2.0, 2.0}}, {-50.0});
    st = ys::staleness(*fx.p, *back);
    REQUIRE(st.reasons.size() == 1);
    CHECK(st.reasons[0] == "measured points changed");
    fx.p->set_scale(fx.ground, *bm::factor_scale(0.04));
    CHECK(ys::staleness(*fx.p, *back).reasons.size() == 2);  // the floors and the points' metres
    CHECK(ys::stored_interpolation(*fx.p, id2)->value_dbm == r->value_dbm);
    // Coverage summary: the measured points and the area side by side.
    const auto sum = ys::coverage_summary(*fx.p, *back, {-60.0});
    CHECK(sum.stale);
    CHECK(sum.interpolated.label == "Share of floor area (interpolated) (%)");
    CHECK(sum.measured.points == fx.pts.size() + 1);
    // A damaged row is reported, not guessed.
    auto raw = ys::to_stored(*r);
    raw.result_json = R"({"format":99})";
    CHECK_THROWS_AS(ys::from_stored(raw), pj::ProjectError);
    raw = ys::to_stored(*r);
    raw.cells.pop_back();
    CHECK_THROWS_AS(ys::from_stored(raw), pj::ProjectError);
}

TEST_CASE("EXP-06 floor-area basis: hand-computed floor, two floors, the trim outline") {
    // A hand-made map: 4 x 2 cells of 1 m; the last cell is off the floor.
    ys::Interpolation a;
    a.grid = {{0.0, 0.0}, 1.0, 4, 2};
    const float ninf = -std::numeric_limits<float>::infinity();
    a.value_dbm = {-60, -70, -80, -90, -65, -75, -85, ninf};
    a.confidence = {1.0f, 1.0f, 0.6f, 0.4f, 1.0f, 0.5f, 0.49f, 0.0f};
    a.floor_cells = 7;
    auto s = ys::area_stats(a, {-72.0, -85.0});
    CHECK(s.label == "Share of floor area (interpolated) (%)");
    CHECK(s.floor_area_m2 == 7.0);
    CHECK(s.assessed_cells == 5);  // confidence >= 0.5
    CHECK(s.assessed_area_m2 == 5.0);
    REQUIRE(s.thresholds.size() == 2);
    CHECK(s.thresholds[0].area_m2 == 3.0);  // -60, -70, -65
    CHECK(s.thresholds[0].percent == doctest::Approx(60.0));
    CHECK(s.thresholds[0].percent_below == doctest::Approx(40.0));
    CHECK(s.thresholds[1].percent == doctest::Approx(100.0));
    s = ys::area_stats(a, {-85.0}, 0.4);
    CHECK(s.assessed_cells == 7);
    CHECK(s.thresholds[0].percent == doctest::Approx(600.0 / 7.0));

    ys::AcceptanceProfile prof;
    prof.name = "Area";
    prof.basis = ys::AcceptanceProfile::Basis::FloorArea;
    prof.criteria = {{-72.0, 50.0}, {-85.0, 95.0}};
    auto res = ys::evaluate_area(prof, {&a});
    CHECK(res.pass);
    CHECK(res.cells == 5);
    CHECK(res.area_m2 == 5.0);
    REQUIRE(res.criteria.size() == 2);
    CHECK(res.criteria[0].percent == doctest::Approx(60.0));
    CHECK(res.criteria[1].percent == doctest::Approx(100.0));
    // A second floor: 4 cells of 0.5 m, all -95 dBm; area-weighted.
    ys::Interpolation b;
    b.grid = {{0.0, 0.0}, 0.5, 2, 2};
    b.value_dbm = {-95, -95, -95, -95};
    b.confidence = {1, 1, 1, 1};
    b.floor_cells = 4;
    res = ys::evaluate_area(prof, {&a, &b});
    CHECK(res.cells == 9);
    CHECK(res.area_m2 == 6.0);
    CHECK(res.criteria[0].percent == doctest::Approx(50.0));
    CHECK(res.criteria[0].pass);
    CHECK(res.criteria[1].percent == doctest::Approx(500.0 / 6.0));
    CHECK_FALSE(res.criteria[1].pass);
    CHECK_FALSE(res.pass);
    CHECK_FALSE(ys::evaluate_area(prof, {}).pass);
    // Other bases and units are refused; the point statistics do not mix
    // interpolated values in.
    auto pts_prof = prof;
    pts_prof.basis = ys::AcceptanceProfile::Basis::Points;
    CHECK_THROWS_AS(ys::evaluate_area(pts_prof, {&a}), std::invalid_argument);
    auto dbfs = prof;
    dbfs.unit = "dBFS";
    CHECK_THROWS_AS(ys::evaluate_area(dbfs, {&a}), std::invalid_argument);
    const std::vector<ys::SurveyPoint> points{valued(-50.0)};
    const auto on_points = ys::evaluate(prof, points);
    CHECK_FALSE(on_points.pass);
    CHECK_FALSE(on_points.note.empty());

    // The floor outline: an L-shaped polygon trim of a 10 x 10 m floor
    // (200 x 200 units at 0.05 m) covers 75 of its 100 cells of 1 m.
    bm::Building bld;
    bm::Floor f;
    f.id = "l";
    f.scale.method = bm::Scale::Method::Factor;
    f.scale.metres_per_unit = 0.05;
    f.plan_width = 200;
    f.plan_height = 200;
    f.trim.shape = bm::Trim::Shape::Polygon;
    f.trim.outline = {{0, 0}, {200, 0}, {200, 100}, {100, 100}, {100, 200}, {0, 200}};
    bld.floors.push_back(f);
    const auto grid = ys::floor_grid(bld, bld.floors[0], 1.0);
    REQUIRE(grid.size() == 100);
    const auto on = ys::floor_cells(bld, bld.floors[0], grid);
    CHECK(std::count(on.begin(), on.end(), true) == 75);
    CHECK(on[grid.index(2, 2)]);
    CHECK_FALSE(on[grid.index(7, 7)]);
    // Without a trim: the plan rectangle.
    bld.floors[0].trim = {};
    const auto all = ys::floor_cells(bld, bld.floors[0], grid);
    CHECK(std::count(all.begin(), all.end(), true) == 100);
}

TEST_CASE("IND-45 interpolation job: progress, stored result, same result with any thread count") {
    MapFixture fx("interp-job");
    std::mutex m;
    std::vector<yu::JobStatus> seen;
    std::vector<pj::Change> changes;
    fx.p->subscribe([&](const pj::Change& c) {
        const std::lock_guard l(m);
        changes.push_back(c);
    });
    auto job = ys::InterpolationJob::start(fx.p, fx.request(), {}, [&](const yu::JobStatus& s) {
        const std::lock_guard l(m);
        seen.push_back(s);
    });
    job->wait();
    const auto st = job->status();
    INFO(st.error);
    REQUIRE(st.state == yu::JobState::Done);
    CHECK(st.progress == 1.0);
    const auto r = job->result();
    REQUIRE(r);
    CHECK(r->id > 0);
    CHECK_FALSE(r->created_utc.empty());
    const auto stored = ys::stored_interpolations(*fx.p, fx.ground);
    REQUIRE(stored.size() == 1);
    CHECK(stored[0] == *r);
    {
        const std::lock_guard l(m);
        REQUIRE(seen.size() >= 3);
        for (std::size_t i = 1; i < seen.size(); ++i) CHECK(seen[i].progress >= seen[i - 1].progress);
        std::vector<std::string> st_names;
        for (const auto& s : seen)
            if (st_names.empty() || st_names.back() != s.stage) st_names.push_back(s.stage);
        CHECK(st_names.front() == "Points");
        CHECK(std::find(st_names.begin(), st_names.end(), "Map") != st_names.end());
        CHECK(std::find(st_names.begin(), st_names.end(), "Storing") != st_names.end());
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].kind == pj::Change::Kind::Interpolations);
        CHECK(changes[0].id == fx.ground);
    }
    // One worker thread and scalar: the same map.
    ys::InterpolationJob::Options one;
    one.threads = 1;
    one.isa = ys::Isa::Scalar;
    one.store = false;
    auto job1 = ys::InterpolationJob::start(fx.p, fx.request(), one);
    job1->wait();
    REQUIRE(job1->result());
    CHECK(job1->result()->id == 0);
    CHECK(job1->result()->value_dbm == r->value_dbm);
    CHECK(job1->result()->params == r->params);
    // A failing request ends Failed with the reason.
    auto bad = fx.request();
    bad.floor_id = "floor-99";
    auto job2 = ys::InterpolationJob::start(fx.p, bad, {});
    job2->wait();
    CHECK(job2->status().state == yu::JobState::Failed);
    CHECK(job2->status().error.find("floor-99") != std::string::npos);
    CHECK_FALSE(job2->result());
    CHECK_THROWS_AS(ys::InterpolationJob::start(nullptr, fx.request(), {}), std::invalid_argument);
}

TEST_CASE("IND-45 interpolation job: a cancel stores nothing") {
    MapFixture fx("interp-job-cancel");
    for (const char* at : {"Points", "Map"}) {
        CAPTURE(at);
        Latch ready;
        std::atomic<ys::InterpolationJob*> self{nullptr};
        std::atomic<bool> first{true};
        const std::string stage = at;
        auto job = ys::InterpolationJob::start(fx.p, fx.request(), {}, [&](const yu::JobStatus& s) {
            if (first.exchange(false)) ready.wait();
            if (s.stage == stage) self.load()->cancel();
        });
        self = job.get();
        ready.open();
        job->wait();
        CHECK(job->status().state == yu::JobState::Cancelled);
        CHECK_FALSE(job->result());
        CHECK(ys::stored_interpolations(*fx.p, fx.ground).empty());
    }
    // The destructor cancels and joins a running job.
    { auto job = ys::InterpolationJob::start(fx.p, fx.request(), {}); }
    CHECK(ys::stored_interpolations(*fx.p, fx.ground).size() <= 1);
}
