// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interpolated map of a floor over a project (IND-41..45, EXP-06):
// fit to the shared point list, residual map and confidence mask,
// estimated sources, storage and staleness, hover readout, the
// floor-area basis, and the background job (progress, cancel,
// deterministic results with any thread count). The measured points are
// made with the ray tracer at known parameters.
//
// Adapted from yantra-v2 68ea20a: tests/survey/test_survey_interpolation.cpp
// (IND-41..45).

#include "survey_helpers.h"
#include "yantra/core/raytrace/model.h"
#include "yantra/core/survey/interpolation.h"
#include "yantra/core/survey/sample.h"

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace yr = yantra::raytrace;
namespace yu = yantra::util;

namespace {

constexpr std::uint64_t kHz = 390'000'000;

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

bm::Cell cell(const std::string& id) {
    bm::Cell c;
    c.id = id;
    c.label = "Cell " + id;
    c.technology_id = "tetra";
    c.schema_id = "tetra_cell";
    c.carrier_hz = kHz;
    return c;
}

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

yr::ModelParams truth_params(const pj::Project& p, double exponent) {
    auto t = ys::model_params(p.propagation());
    t.path_loss_exponent = exponent;
    return t;
}

/// The ground floor (20 x 10 m) with a brick wall at x = 10 m (y 0..6) and
/// one antenna at (4, 5) m, EIRP unknown to the project; the truth is
/// 14 dBm and exponent 2.4. Points on the left of the wall only.
struct MapFixture : Fixture {
    std::vector<bm::Point2> pts;
    std::vector<double> levels;
    std::int64_t session = 0;
    explicit MapFixture(const std::string& name) : Fixture(name) {
        auto b = p->building();
        b.cells.push_back(cell("cell-1"));
        auto& g = *std::find_if(b.floors.begin(), b.floors.end(), [&](const bm::Floor& f) { return f.id == ground; });
        g.walls.push_back({"wall-1", "brick", {pu(10, 0), pu(10, 6)}, std::nullopt});
        bm::Antenna a;
        a.id = "antenna-1";
        a.name = "DAS 1";
        a.position = pu(4, 5);
        a.height_m = 2.7;
        a.cell = "cell-1";
        g.antennas.push_back(a);
        p->replace_building(b);
        for (double y = 1.0; y < 10.0; y += 2.0)
            for (double x = 0.5; x <= 9.0; x += 1.5) pts.push_back({x, y});
        auto truth = p->building();
        for (auto& f : truth.floors)
            for (auto& an : f.antennas) an.eirp_dbm = 14.0;
        const auto tp = truth_params(*p, 2.4);
        levels = yr::evaluate(truth, yr::trace_points(truth, ground, pts, tp), tp).best_dbm;
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

TEST_CASE("IND-41 interpolated map: the fit recovers the true model, the map follows the points, the mask fades") {
    MapFixture fx("interp-fit");
    std::vector<std::string> stages;
    const auto r = ys::interpolate(*fx.p, fx.request(), {}, [&](const std::string& s) { stages.push_back(s); });
    REQUIRE(r);
    CHECK(stages == std::vector<std::string>{"Points", "Fit", "Map", "Residuals"});
    CHECK(r->label() == "interpolated");
    CHECK_FALSE(r->estimated_sources);
    CHECK(r->quality.points == fx.pts.size());
    CHECK(r->points.size() == fx.pts.size());
    // Stated tolerances (noise-free): rms 1e-6 dB, EIRP 1e-4 dB, exponent 1e-6.
    CHECK(r->quality.rms_db < 1e-6);
    CHECK(r->quality.start_rms_db > 0.5);
    CHECK(std::fabs(r->params.antenna_eirp_dbm.at("antenna-1") - 14.0) < 1e-4);
    CHECK(std::fabs(r->params.path_loss_exponent - 2.4) < 1e-6);
    CHECK(r->group_key.empty());  // the strongest group
    CHECK(r->frequency_hz == static_cast<double>(kHz));
    CHECK_FALSE(r->start_params.frequency_hz);  // the cell's carrier
    // No point behind the brick wall: listed, not fitted.
    bool brick = false;
    for (const auto& n : r->not_fitted) brick = brick || n.find("'brick'") != std::string::npos;
    CHECK(brick);
    // The grid covers the floor; every cell is on it (no trim).
    CHECK(r->grid.cell_m == 0.25);
    CHECK(r->grid.size() == 80u * 40u);
    CHECK(r->floor_cells == r->grid.size());
    // Map = model + residual (here 0); the model equals the truth.
    auto truth = fx.p->building();
    for (auto& f : truth.floors)
        for (auto& an : f.antennas) an.eirp_dbm = 14.0;
    const auto tp = truth_params(*fx.p, 2.4);
    const auto tm = yr::compute_map(truth, fx.ground, tp);
    for (std::size_t i = 0; i < r->grid.size(); i += 37) {
        CHECK(std::fabs(r->model_dbm[i] - tm.best_dbm[i]) < 1e-3);
        CHECK(std::fabs(r->value_dbm[i] - r->model_dbm[i]) < 1e-3);
    }
    // Confidence: 1 at the points, 0 behind the wall and far from them.
    CHECK(r->confidence[*r->grid.cell_at({0.6, 1.1})] == 1.0f);
    CHECK(r->confidence[*r->grid.cell_at({10.5, 1.0})] == 0.0f);  // behind the wall
    CHECK(r->confidence[*r->grid.cell_at({19.9, 9.9})] == 0.0f);  // > 10 m from every point
    CHECK(r->confidence[*r->grid.cell_at({12.0, 9.0})] > 0.0f);
    CHECK(r->confidence[*r->grid.cell_at({12.0, 9.0})] < 1.0f);
    // Hover readout: over a point its measured value, else interpolated.
    const auto over = ys::readout(*r, fx.pts[3]);
    CHECK(over.on_map);
    CHECK(over.measured);
    CHECK(over.label == "measured");
    CHECK(*over.value_dbm == fx.levels[3]);
    const auto between = ys::readout(*r, {1.25, 2.0});
    CHECK_FALSE(between.measured);
    CHECK(between.label == "interpolated");
    REQUIRE(between.value_dbm);
    CHECK(between.nearest_m == doctest::Approx(1.25));  // (0.5, 1) and (2, 1)
    CHECK_FALSE(ys::readout(*r, {30.0, 3.0}).on_map);
    // Colours: alpha = confidence.
    const auto colours = ys::cell_colours(*r, ys::default_colour_scale("dBm"));
    CHECK((colours[*r->grid.cell_at({0.6, 1.1})] >> 24) == 255u);
    CHECK((colours[*r->grid.cell_at({10.5, 1.0})] >> 24) == 0u);
}

TEST_CASE("IND-41 interpolation: the metric's carrier picks the cell, inputs are checked") {
    MapFixture fx("interp-checks");
    auto q = fx.request();
    q.metric.carrier = ys::Metric::Carrier::Frequency;
    q.metric.frequency_hz = kHz;
    const auto r = ys::interpolate(*fx.p, q);
    REQUIRE(r);
    CHECK(r->group_key == "cell:cell-1");
    CHECK(r->quality.rms_db < 1e-6);
    CHECK(q.key() == "level|frequency:390000000|auto");
    CHECK(fx.request().key() == "level|serving|auto");
    auto code = [&](const ys::InterpolationRequest& x) {
        try {
            (void)ys::interpolate(*fx.p, x);
        } catch (const pj::ProjectError& e) {
            return e.code();
        }
        FAIL("no error");
        return pj::ProjectError::Code::Io;
    };
    q = fx.request();
    q.metric.value = ys::Metric::Value::C1;
    CHECK(code(q) == pj::ProjectError::Code::Invalid);
    q = fx.request();
    q.floor_id = "floor-99";
    CHECK(code(q) == pj::ProjectError::Code::NotFound);
    q = fx.request();
    q.floor_id = fx.first;  // no points there
    CHECK(code(q) == pj::ProjectError::Code::Invalid);
    q = fx.request();
    q.residuals.power = 9.0;
    CHECK(code(q) == pj::ProjectError::Code::Invalid);
    q = fx.request();
    q.grid_m = 0.01;
    CHECK(code(q) == pj::ProjectError::Code::Invalid);
    // A floor without scale.
    bm::Floor f;
    f.name = "Loft";
    f.level = 2;
    f.plan_width = 100;
    f.plan_height = 100;
    q.floor_id = fx.p->add_floor(f);
    q.grid_m.reset();
    CHECK(code(q) == pj::ProjectError::Code::Invalid);
    // A cancelled run gives nothing.
    std::atomic<bool> cancel{true};
    yr::RunControl run;
    run.cancel = &cancel;
    CHECK_FALSE(ys::interpolate(*fx.p, fx.request(), run));
}

TEST_CASE("IND-41 interpolation: the same result with any thread count and with or without SIMD") {
    MapFixture fx("interp-threads");
    const auto a = ys::interpolate(*fx.p, fx.request());
    yr::RunControl one;
    one.threads = 1;
    one.isa = yr::Isa::Scalar;
    const auto b = ys::interpolate(*fx.p, fx.request(), one);
    yr::RunControl four;
    four.threads = 4;
    const auto c = ys::interpolate(*fx.p, fx.request(), four);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(c);
    CHECK(*a == *b);
    CHECK(*a == *c);
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
    auto s = fx.p->propagation();
    s.reflection_loss_db = 9.0;
    fx.p->set_propagation(s);
    auto st = ys::staleness(*fx.p, *back);
    CHECK(st.stale);
    REQUIRE(st.reasons.size() == 1);
    CHECK(st.reasons[0] == "propagation settings or parameters changed");
    fx.p->set_propagation({});
    CHECK_FALSE(ys::staleness(*fx.p, *back).stale);
    add_points(*fx.p, fx.ground, {{2.0, 2.0}}, {-50.0});
    st = ys::staleness(*fx.p, *back);
    REQUIRE(st.reasons.size() == 1);
    CHECK(st.reasons[0] == "measured points changed");
    auto b = fx.p->building();
    b.floor_attenuation_db = 20.0;
    fx.p->replace_building(b);
    CHECK(ys::staleness(*fx.p, *back).reasons.size() == 2);
    CHECK(ys::stored_interpolation(*fx.p, id2)->value_dbm == r->value_dbm);
    // Coverage summary: the measured points and the area side by side.
    const auto sum = ys::coverage_summary(*fx.p, *back, {-60.0});
    CHECK(sum.stale);
    CHECK(sum.interpolated.label == "Share of floor area (interpolated) (%)");
    CHECK(sum.fit.points == fx.pts.size());
    // A damaged row is reported, not guessed.
    auto raw = ys::to_stored(*r);
    raw.result_json = R"({"format":99})";
    CHECK_THROWS_AS(ys::from_stored(raw), pj::ProjectError);
}

TEST_CASE("IND-42 estimated sources: a floor without antennas gets sources from its points, labelled") {
    MapFixture fx("interp-estimated");
    auto s = fx.p->propagation();
    s.other_floors = false;  // the ground floor's antenna does not reach the first floor
    s.max_reflections = 0;
    fx.p->set_propagation(s);
    // The truth on the first floor: a hidden source at (13, 4) m, 8 dBm.
    auto truth = fx.p->building();
    auto& f1 = *std::find_if(truth.floors.begin(), truth.floors.end(), [&](const bm::Floor& f) { return f.id == fx.first; });
    bm::Antenna a;
    a.id = "hidden";
    a.position = pu(13, 4);
    a.height_m = 2.7;
    a.eirp_dbm = 8.0;
    a.cell = "cell-1";
    f1.antennas.push_back(a);
    std::vector<bm::Point2> pts;
    for (double y = 0.5; y < 10.0; y += 1.5)
        for (double x = 0.5; x < 20.0; x += 1.5) pts.push_back({x, y});
    const auto tp = ys::model_params(s);
    const auto lv = yr::evaluate(truth, yr::trace_points(truth, fx.first, pts, tp), tp).best_dbm;
    add_points(*fx.p, fx.first, pts, lv);
    auto q = fx.request();
    q.floor_id = fx.first;
    q.sources.step_m = 1.0;
    std::vector<std::string> stages;
    const auto r = ys::interpolate(*fx.p, q, {}, [&](const std::string& x) { stages.push_back(x); });
    REQUIRE(r);
    CHECK(stages == std::vector<std::string>{"Points", "Sources", "Fit", "Map", "Residuals"});
    CHECK(r->estimated_sources);
    CHECK(r->label() == "interpolated, estimated sources");
    REQUIRE(r->sources.size() == 1);
    CHECK(std::hypot(r->sources[0].position.x - pu(13, 4).x, r->sources[0].position.y - pu(13, 4).y) * 0.05 < 0.2);
    CHECK(r->sources[0].cell == yr::kEstimatedCellId);
    REQUIRE(r->sources[0].eirp_dbm);
    CHECK(std::fabs(*r->sources[0].eirp_dbm - 8.0) < 0.5);
    CHECK(r->quality.rms_db < 0.5);
    // The project itself is not changed: the sources live in the result.
    for (const auto& f : fx.p->building().floors)
        if (f.id == fx.first) CHECK(f.antennas.empty());
    // Stored and read back with its sources and label.
    const auto id = ys::store(*fx.p, *r);
    const auto back = ys::stored_interpolation(*fx.p, id);
    REQUIRE(back);
    CHECK(back->sources == r->sources);
    CHECK(back->label() == r->label());
}

TEST_CASE("EXP-06 floor-area basis: hand-computed floor, two floors, the trim outline") {
    // A hand-made map: 4 x 2 cells of 1 m; the last cell is off the floor.
    ys::Interpolation a;
    a.grid = {{0.0, 0.0}, 1.0, 4, 2};
    const float ninf = -std::numeric_limits<float>::infinity();
    a.value_dbm = {-60, -70, -80, -90, -65, -75, -85, ninf};
    a.model_dbm = a.value_dbm;
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
    b.model_dbm = b.value_dbm;
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
    const auto grid = yr::floor_grid(bld, bld.floors[0], 1.0);
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
        REQUIRE(seen.size() >= 5);
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
    one.isa = yr::Isa::Scalar;
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
    for (const char* at : {"Points", "Fit", "Map", "Residuals"}) {
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
