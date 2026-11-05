// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Post-processing (PP-01..PP-05, EXP-06, TST-32, TST-34; docs/SURVEY.md
// "Post-processing") against hand-computed values: binning with the
// percentile per square, the coverage rate of the tested area, acceptance
// on the tested-area basis, good/bad, serving-cell changes, the level
// offset and base-station fail simulations, the hover readout, recording
// import refusals, and the point identity of generated many-floor
// projects through a save/reopen.

#include "yantra/core/building/edit.h"
#include "yantra/core/project/project.h"
#include "yantra/core/survey/bins.h"
#include "yantra/core/survey/points.h"
#include "yantra/core/survey/stats.h"
#include "yantra/core/survey/viewer.h"
#include "yantra/core/survey/views.h"
#include "yantra/core/survey/walk_test.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "survey_helpers.h"

namespace {

// A list point at plan-frame metres (x_m, y_m); plan units = metres x 20.
ys::SurveyPoint at(std::optional<double> v, std::optional<bm::Point2> m, const std::string& unit = "dBm") {
    ys::SurveyPoint p;
    p.session_id = 1;
    if (m) {
        p.metres = *m;
        p.plan = {m->x * 20, m->y * 20};
    }
    p.value = v;
    if (v) p.unit = unit;
    return p;
}

ys::PointList list_of(std::vector<ys::SurveyPoint> pts, const std::string& unit = "dBm") {
    ys::PointList l;
    l.floor_id = "floor-1";
    l.unit = unit;
    l.scale = ys::default_colour_scale(unit);
    l.points = std::move(pts);
    for (auto& p : l.points) p.rgb = l.scale.colour_of(p.unit == unit ? p.value : std::nullopt);
    return l;
}

// The binning example: 8 points, one without metres, one in dBFS.
std::vector<ys::SurveyPoint> bin_example() {
    return {at(-80, bm::Point2{0.5, 0.5}),  at(-70, bm::Point2{1.5, 1.9}),
            at(-90, bm::Point2{1.0, 1.0}),  at(-60, bm::Point2{2.0, 0.0}),  // on an edge: the square above
            at(std::nullopt, bm::Point2{3.9, 2.1}), at(-100, bm::Point2{-0.1, 0.0}),
            at(-75, bm::Point2{0.2, 0.2}, "dBFS"), at(-50, std::nullopt)};
}

ys::SurveyPoint served(std::int64_t session, const std::string& cell, std::uint64_t hz) {
    ys::SurveyPoint p;
    p.session_id = session;
    p.value = -80;
    p.unit = "dBm";
    p.serving_cell = cell;
    p.serving_hz = hz;
    return p;
}

// Three cycles walked from (0, 0) to (40, 0) plan units: points at x =
// 10, 20, 30 with `carriers` in every cycle.
void walk_three(Fixture& fx, const std::vector<Carrier>& carriers) {
    auto w = fx.walk();
    w->start(0);
    w->click(0, {0, 0});
    for (int i = 1; i <= 3; ++i) w->on_cycle(cycle(i * kS, carriers));
    w->click(4 * kS, {40, 0});
    w->finish(5 * kS);
    REQUIRE(fx.p->point_count(fx.ground) == 3);
}

ys::PointList floor_list(const Fixture& fx, const ys::Metric& m = {}) {
    return ys::point_list(*fx.p, ys::PointQuery{fx.ground, {}, m, std::nullopt});
}

}  // namespace

TEST_CASE("TST-32 PP-05 binning: squares on the plan, the percentile per square, a labelled extra view") {
    const ys::PointList list = list_of(bin_example());
    const ys::PointList before = list;
    const ys::BinnedView v = ys::bin_points(list, 2.0);
    CHECK(list == before);  // the measured points are not changed
    CHECK(v.label == "Binned view: 2 m squares, P50 per square (not the measured points)");
    CHECK(v.floor_id == "floor-1");
    CHECK(v.unit == "dBm");
    CHECK(v.points == 7);
    CHECK(v.left_out == 1);
    CHECK_FALSE(v.to_scale);
    REQUIRE(v.bins.size() == 4);  // by row, then column
    const ys::Bin& w = v.bins[0];
    CHECK((w.i == -1 && w.j == 0 && w.count == 1 && w.with_value == 1));
    CHECK(w.value == -100.0);
    CHECK(w.rgb == 0xe53935);  // the -100 band
    CHECK(w.centre_m == bm::Point2{-1.0, 1.0});
    const ys::Bin& o = v.bins[1];
    CHECK((o.i == 0 && o.j == 0 && o.count == 4 && o.with_value == 3));  // the dBFS point counts, has no value here
    CHECK(o.value == -80.0);  // median of -90, -80, -70
    CHECK(o.rgb == 0xfdd835);
    CHECK(o.centre_m == bm::Point2{1.0, 1.0});
    CHECK((v.bins[2].i == 1 && v.bins[2].j == 0 && v.bins[2].value == -60.0 && v.bins[2].rgb == 0x43a047));
    CHECK((v.bins[3].i == 1 && v.bins[3].j == 1 && v.bins[3].count == 1 && v.bins[3].with_value == 0));
    CHECK_FALSE(v.bins[3].value);
    CHECK(v.bins[3].rgb == 0x9e9e9e);
    // P90 of the (0, 0) square: h = 2 x 0.9 = 1.8, -80 + 0.8 x 10 = -72.
    const ys::BinnedView p90 = ys::bin_points(list, 2.0, 90.0);
    CHECK(p90.bins[1].value.value() == doctest::Approx(-72.0).epsilon(1e-12));
    CHECK(p90.label == "Binned view: 2 m squares, P90 per square (not the measured points)");
    // 1 m squares: (-1,0), (0,0), (2,0), (1,1) with (1.5, 1.9) and (1.0, 1.0), (3,2).
    const ys::BinnedView one = ys::bin_points(list, 1.0);
    CHECK(one.bins.size() == 5);
    // Refused: size and percentile out of range.
    CHECK_THROWS_AS(ys::bin_points(list, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(ys::bin_points(list, -1.0), std::invalid_argument);
    CHECK_THROWS_AS(ys::bin_points(list, std::nan("")), std::invalid_argument);
    CHECK_THROWS_AS(ys::bin_points(list, 2.0, 100.5), std::invalid_argument);
    // A floor without a scale: no squares.
    const ys::BinnedView none = ys::bin_points(list_of({at(-80, std::nullopt)}), 2.0);
    CHECK(none.bins.empty());
    CHECK(none.left_out == 1);
}

TEST_CASE("TST-32 PP-02 coverage rate of the tested area from the binned view") {
    const ys::BinnedView v = ys::bin_points(list_of(bin_example()), 2.0);
    const ys::AreaCoverage a = ys::area_coverage(v, -85.0);
    CHECK(a.label == "Coverage of the tested area (2 m squares, P50): >= -85 dBm");
    CHECK(a.higher_better);
    CHECK(a.squares == 4);
    CHECK(a.covered == 2);  // -80 and -60; -100 and the square without a value are not
    CHECK(a.tested_m2 == 16.0);
    CHECK(a.covered_m2 == 8.0);
    CHECK(a.percent == 50.0);
    CHECK(ys::area_coverage(v, -59.0).percent == 0.0);
    CHECK(ys::area_coverage(v, -100.0).percent == 75.0);
    CHECK_THROWS_AS(ys::area_coverage(v, std::nan("")), std::invalid_argument);
    // An error rate in %: lower is better.
    const ys::BinnedView e = ys::bin_points(
        list_of({at(1.5, bm::Point2{0.5, 0.5}, "%"), at(3.0, bm::Point2{2.5, 0.5}, "%")}, "%"), 2.0);
    const ys::AreaCoverage ea = ys::area_coverage(e, 2.0);
    CHECK_FALSE(ea.higher_better);
    CHECK(ea.label == "Coverage of the tested area (2 m squares, P50): <= 2 %");
    CHECK(ea.covered == 1);
    CHECK(ea.percent == 50.0);
    // No squares: 0 %.
    CHECK(ys::area_coverage(ys::BinnedView{}, -85.0).percent == 0.0);
}

TEST_CASE("TST-32 EXP-06 acceptance on the tested-area basis, per floor and for the building") {
    const ys::PointList a = list_of(bin_example());
    ys::PointList b = list_of({at(-90, bm::Point2{0.5, 0.5})});
    b.floor_id = "floor-2";
    ys::AcceptanceProfile area;
    area.name = "Area";
    area.unit = "dBm";
    area.criteria = {{-85, 50}, {-85, 60}};
    area.basis = ys::AcceptanceProfile::Basis::TestedArea;
    area.square_m = 2.0;
    const ys::AcceptanceResult r = ys::evaluate(area, a.points);
    CHECK(r.basis == ys::AcceptanceProfile::Basis::TestedArea);
    CHECK(r.points == 8);
    CHECK(r.squares == 4);
    CHECK(r.area_m2 == 16.0);
    REQUIRE(r.criteria.size() == 2);
    CHECK(r.criteria[0].percent == 50.0);
    CHECK(r.criteria[0].pass);  // 50 >= 50
    CHECK_FALSE(r.criteria[1].pass);
    CHECK_FALSE(r.pass);
    // The same profile on the points basis: 4 of 8 points >= -85 dBm (the
    // point without metres counts here).
    ys::AcceptanceProfile points = area;
    points.basis = ys::AcceptanceProfile::Basis::Points;
    CHECK(ys::evaluate(points, a.points).criteria[0].percent == 50.0);
    // The building: squares of two floors never merge (5 squares, 2 covered).
    const ys::AcceptanceResult both = ys::evaluate_floors(area, {&a.points, &b.points});
    CHECK(both.squares == 5);
    CHECK(both.points == 9);
    CHECK(both.criteria[0].percent == 40.0);
    const ys::FloorStats building = ys::building_stats({a, b}, {}, {area});
    REQUIRE(building.acceptance.size() == 1);
    CHECK(building.acceptance[0] == both);
    CHECK(ys::floor_stats(a, {}, {}, {area}).acceptance[0] == r);
    // Without a scale there is no tested area: not accepted.
    const ys::AcceptanceResult no_scale = ys::evaluate(area, list_of({at(-60, std::nullopt)}).points);
    CHECK(no_scale.squares == 0);
    CHECK_FALSE(no_scale.pass);
    CHECK(no_scale.criteria[0].percent == 0.0);
}

TEST_CASE("TST-32 PP-02 good/bad against a threshold and serving-cell changes") {
    const ys::PointList l = list_of({at(-80, bm::Point2{0, 0}), at(-90, bm::Point2{0, 0}), at(std::nullopt, bm::Point2{0, 0}),
                                     at(-85, bm::Point2{0, 0}), at(-75, bm::Point2{0, 0}, "dBFS")});
    const ys::Classified c = ys::classify(l, -85.0);
    CHECK(c.label == "Good: >= -85 dBm");
    CHECK(c.verdicts == std::vector<ys::Verdict>{ys::Verdict::Good, ys::Verdict::Bad, ys::Verdict::NoValue,
                                                 ys::Verdict::Good, ys::Verdict::NoValue});
    CHECK((c.good == 2 && c.bad == 1 && c.no_value == 2));
    CHECK(c.percent_good == 40.0);
    const ys::Classified low = ys::classify(l, -85.0, false);
    CHECK(low.label == "Good: <= -85 dBm");
    CHECK(low.verdicts[0] == ys::Verdict::Bad);
    CHECK(low.verdicts[1] == ys::Verdict::Good);
    const ys::Classified pct = ys::classify(list_of({at(1.5, bm::Point2{0, 0}, "%"), at(3.0, bm::Point2{0, 0}, "%")}, "%"), 2.0);
    CHECK(pct.label == "Good: <= 2 %");
    CHECK(pct.verdicts == std::vector<ys::Verdict>{ys::Verdict::Good, ys::Verdict::Bad});
    CHECK_THROWS_AS(ys::classify(l, std::nan("")), std::invalid_argument);

    // Serving-cell changes: per session, in list order; a cell on another
    // carrier is another cell; points without a serving cell are skipped.
    const std::vector<ys::SurveyPoint> s = {served(1, "A", 390), served(1, "A", 390), served(1, "B", 390),
                                            served(1, "", 0),    served(1, "B", 390), served(1, "A", 391),
                                            served(1, "A", 390), served(2, "B", 390), served(1, "A", 390)};
    const auto changes = ys::serving_changes(s);
    REQUIRE(changes.size() == 3);
    CHECK(changes[0] == ys::ServingChange{2, 1, "A", "B"});
    CHECK(changes[1] == ys::ServingChange{5, 1, "B", "A"});
    CHECK(changes[2] == ys::ServingChange{6, 1, "A", "A"});
    const ys::CoverageStats st = ys::coverage_stats(s, "dBm");
    CHECK(st.serving_cell_changes == changes.size());
    REQUIRE(st.serving_cells.size() == 4);
    CHECK(st.serving_cells[0] == ys::CellShare{"A", 4, 400.0 / 9.0, 390});
    CHECK(st.serving_cells[1] == ys::CellShare{"B", 3, 300.0 / 9.0, 390});
    CHECK(st.serving_cells[2] == ys::CellShare{"", 1, 100.0 / 9.0, 0});
    CHECK(st.serving_cells[3] == ys::CellShare{"A", 1, 100.0 / 9.0, 391});
}

TEST_CASE("TST-32 PP-04 level offset simulation: a labelled copy with moved values and colours") {
    const ys::PointList l = list_of({at(-80, bm::Point2{1, 1}), at(-94, bm::Point2{2, 1}), at(std::nullopt, bm::Point2{3, 1}),
                                     at(-60, bm::Point2{4, 1}, "dBFS")});
    const ys::PointList before = l;
    const ys::OffsetList o = ys::with_offset(l, -10.0);
    CHECK(l == before);
    CHECK(o.label == "Simulation: level -10 dB (not measured)");
    CHECK(o.offset_db == -10.0);
    REQUIRE(o.list.count() == 4);
    CHECK(o.list.points[0].value == -90.0);
    CHECK(o.list.points[0].rgb == 0xfb8c00);
    CHECK(o.list.points[1].value == -104.0);
    CHECK(o.list.points[1].rgb == o.list.scale.outside_rgb);
    CHECK_FALSE(o.list.points[2].value);
    CHECK(o.list.points[3].value == -60.0);  // another unit: not moved, no colour in this list
    CHECK(o.list.points[3].rgb == 0x9e9e9e);
    for (std::size_t i = 0; i < 4; ++i) CHECK(o.list.points[i].plan == l.points[i].plan);
    // Coverage recomputed: >= -95 dBm 50 % measured, 25 % simulated.
    CHECK(ys::coverage_stats(l, {{-95}, {}}).thresholds[0].percent_at_or_above == 50.0);
    CHECK(ys::coverage_stats(o.list, {{-95}, {}}).thresholds[0].percent_at_or_above == 25.0);
    // C1 moves dB for dB; quality values do not move.
    ys::PointList c1 = l;
    c1.metric.value = ys::Metric::Value::C1;
    CHECK(ys::with_offset(c1, 3.0).label == "Simulation: C1 +3 dB (not measured)");
    ys::PointList q = l;
    q.metric.value = ys::Metric::Value::Quality;
    CHECK_THROWS_AS(ys::with_offset(q, 3.0), std::invalid_argument);
    CHECK_THROWS_AS(ys::with_offset(l, std::nan("")), std::invalid_argument);
}

TEST_CASE("PP-03 base-station fail simulation: the cells switched off, the best remaining server and coverage") {
    Fixture fx("fail");
    walk_three(fx, {Carrier{}.w_level(-70).w_cell(1),
                    Carrier{}.w_hz(391'000'000).w_level(-80).w_cell(2).w_role(ym::CarrierRole::Neighbour),
                    Carrier{}.w_hz(392'000'000).w_level(-85).w_cell(3).w_role(ym::CarrierRole::Other)});
    const ys::PointList live = floor_list(fx);
    CHECK(live.points[0].value == -70.0);
    CHECK(live.points[0].serving_cell == "cell_id=1");
    CHECK(live.points[0].serving_hz == 390'000'000);
    ys::Metric m;
    m.failed_cells = {{390'000'000, "cell_id=1"}};
    const ys::PointList off = floor_list(fx, m);
    CHECK(off.metric.label() == "Level, serving carrier (simulation: 1 cell off, not measured)");
    REQUIRE(off.count() == live.count());
    for (const ys::SurveyPoint& p : off.points) {
        CHECK(p.value == -80.0);  // the neighbour serves now
        CHECK(p.serving_cell == "cell_id=2");
        CHECK(p.serving_hz == 391'000'000);
        CHECK(p.frequency_hz == 391'000'000);
    }
    // Coverage >= -75 dBm: all points measured, none with the site off.
    CHECK(ys::coverage_stats(live, {{-75}, {}}).thresholds[0].percent_at_or_above == 100.0);
    CHECK(ys::coverage_stats(off, {{-75}, {}}).thresholds[0].percent_at_or_above == 0.0);
    // Any cell on a carrier; two cells; all cells; a non-serving cell.
    m.failed_cells = {{390'000'000, ""}};
    CHECK(floor_list(fx, m).points[0].value == -80.0);
    m.failed_cells = {{390'000'000, ""}, {391'000'000, "cell_id=2"}};
    CHECK(floor_list(fx, m).points[0].value == -85.0);
    CHECK(floor_list(fx, m).metric.label() == "Level, serving carrier (simulation: 2 cells off, not measured)");
    m.failed_cells.push_back({392'000'000, ""});
    const ys::PointList dark = floor_list(fx, m);
    CHECK_FALSE(dark.points[0].value);
    CHECK(dark.points[0].serving_cell.empty());
    m.failed_cells = {{391'000'000, ""}};
    CHECK(floor_list(fx, m).points[0].value == -70.0);
    m.failed_cells = {{390'000'000, "cell_id=9"}};  // another cell on that carrier: nothing off
    CHECK(floor_list(fx, m).points[0].value == -70.0);
    ys::Metric strongest;
    strongest.carrier = ys::Metric::Carrier::Strongest;
    strongest.failed_cells = {{390'000'000, ""}};
    CHECK(floor_list(fx, strongest).points[0].value == -80.0);
    // The cells to choose from.
    const auto cells = ys::cells_seen(*fx.p, ys::PointQuery{fx.ground, {}, {}, std::nullopt});
    CHECK(cells == std::vector<ys::CellRef>{{390'000'000, "cell_id=1"}, {391'000'000, "cell_id=2"}, {392'000'000, "cell_id=3"}});
}

TEST_CASE("PP-01 hover readout: the nearest point, its plan position, strongest and serving cell and every neighbour") {
    Fixture fx("hover");
    walk_three(fx, {Carrier{}.w_level(-70).w_cell(1).w_c1(12.0),
                    Carrier{}.w_hz(391'000'000).w_level(-60).w_cell(2).w_role(ym::CarrierRole::Neighbour),
                    Carrier{}.w_hz(393'000'000).w_level(-82).w_cell(4).w_role(ym::CarrierRole::Neighbour),
                    Carrier{}.w_hz(392'000'000).w_level(std::nan("")).w_role(ym::CarrierRole::NeighbourEntry).w_entry(1),
                    Carrier{}.w_hz(394'000'000).w_level(std::nan("")).w_role(ym::CarrierRole::NeighbourEntry).w_entry(0),
                    Carrier{}.w_hz(395'000'000).w_level(-85).w_cell(3).w_role(ym::CarrierRole::Other)});
    const ys::PointList l = floor_list(fx);
    REQUIRE(l.count() == 3);
    CHECK(l.points[1].plan == bm::Point2{20, 0});
    // Hit test: nearest within the radius, the first on a tie.
    CHECK(ys::nearest_point(l.points, {19, 1}, 5) == 1u);
    CHECK(ys::nearest_point(l.points, {25, 0}, 5) == 1u);
    CHECK_FALSE(ys::nearest_point(l.points, {25, 0}, 4));
    CHECK_FALSE(ys::nearest_point(l.points, {20, 0}, -1));
    CHECK_FALSE(ys::nearest_point({}, {20, 0}, 5));

    const ys::PointDetail d = ys::point_detail(*fx.p, l.points[1]);
    CHECK(d.point == l.points[1]);
    CHECK(d.point.metres == bm::Point2{1.0, 0.0});
    CHECK(d.floor_id == fx.ground);
    CHECK(d.session_name == "Walk");
    CHECK(d.cycles == 1);
    REQUIRE(d.serving);
    CHECK(d.serving->cell == "cell_id=1");
    CHECK(d.serving->level == -70.0);
    CHECK(d.serving->unit == "dBm");
    CHECK(d.serving->c1 == 12.0);
    CHECK(d.serving->frequency_hz == 390'000'000);
    REQUIRE(d.strongest);
    CHECK(d.strongest->frequency_hz == 391'000'000);  // a neighbour is stronger than the serving cell
    CHECK(d.strongest->role == "neighbour");
    REQUIRE(d.neighbours.size() == 4);
    CHECK(d.neighbours[0].frequency_hz == 391'000'000);
    CHECK(d.neighbours[1].frequency_hz == 393'000'000);
    CHECK(d.neighbours[1].cell == "cell_id=4");
    CHECK(d.neighbours[2].frequency_hz == 394'000'000);  // the list entries by list position
    CHECK(d.neighbours[2].entry == 0u);
    CHECK_FALSE(d.neighbours[2].level);
    CHECK(d.neighbours[3].frequency_hz == 392'000'000);
    CHECK(d.neighbours[3].entry == 1u);
    REQUIRE(d.others.size() == 1);
    CHECK(d.others[0].frequency_hz == 395'000'000);
    CHECK(ys::point_detail(*fx.p, l.points[1], "other:0").serving == std::nullopt);
    fx.p->remove_session(l.points[1].session_id);
    CHECK_THROWS_AS(ys::point_detail(*fx.p, l.points[1]), pj::ProjectError);
}

TEST_CASE("PP-01 recording import: files that are not recordings are refused and the project stays unchanged") {
    Fixture fx("import");
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / "import";
    using Code = pj::ProjectError::Code;
    auto code_of = [&](const std::vector<fs::path>& files) {
        try {
            ys::import_recordings(fx.p, files);
        } catch (const pj::ProjectError& e) {
            return e.code();
        }
        return Code::Closed;  // not refused
    };
    CHECK(code_of({dir / "missing.yrec"}) == Code::Io);
    {
        std::ofstream(dir / "notes.yrec") << "not a recording\n";
    }
    CHECK(code_of({dir / "notes.yrec"}) == Code::Invalid);
    CHECK(ys::import_recordings(fx.p, {}).empty());
    CHECK(fx.p->sessions().empty());
    CHECK(fx.p->recordings().empty());
    CHECK_THROWS_AS(ys::import_recordings(nullptr, {}), std::invalid_argument);
}

TEST_CASE("TST-34 generated projects with many floors and dense walks: every floor's list survives a save and reopen point for point") {
    const fs::path dir = scratch("many-floors");
    const fs::path file = dir / "m.yantra";
    constexpr int kFloors = 6;
    constexpr int kCycles = 1200;
    std::vector<std::string> floors;
    std::vector<ys::Metric> metrics(3);
    metrics[1].carrier = ys::Metric::Carrier::Strongest;
    metrics[2].value = ys::Metric::Value::C1;
    std::vector<std::vector<ys::PointList>> before(kFloors);
    {
        auto p = std::shared_ptr<pj::Project>(pj::Project::create(file, pj::ProjectInfo{"Tower", "", "", ""}));
        for (int f = 0; f < kFloors; ++f) {
            bm::Floor fl;
            fl.name = "Floor " + std::to_string(f);
            fl.level = f;
            fl.plan_width = 400;
            fl.plan_height = 200;
            floors.push_back(p->add_floor(fl));
            p->set_scale(floors.back(), *bm::factor_scale(0.05));
        }
        for (int f = 0; f < kFloors; ++f) {
            // A dense continuous walk (0.1 s cycles) and manual points.
            ys::WalkTestOptions o;
            o.project = p;
            o.floor_id = floors[f];
            o.mode = ys::WalkMode::Continuous;
            auto w = std::make_shared<ys::WalkTest>(o);
            w->start(0);
            w->click(0, {0, 0});
            for (int i = 0; i <= kCycles; ++i) {
                const std::int64_t t = i * kS / 10;
                w->on_cycle(cycle(t, {Carrier{}.w_level(-55.0 - ((i * 7 + f) % 53) * 0.37).w_cell(1 + (i / 300)).w_c1(20.0 - (i % 11)),
                                      Carrier{}.w_hz(391'000'000).w_level(-62.0 - (i % 17)).w_cell(9).w_role(ym::CarrierRole::Neighbour)}));
                if (i % 40 == 0 && i > 0) w->click(t, {static_cast<double>((i * 13) % 400), static_cast<double>((i / 40) % 200)});
            }
            w->finish(kCycles * kS / 10 + 1);
            o.kind = "manual";
            auto m = std::make_shared<ys::WalkTest>(o);
            m->start(0);
            for (int k = 0; k < 20; ++k) {
                m->point(k * 3 * kS, {10.0 + k * 15, 50.0 + k}, 2.0, "Room " + std::to_string(k));
                for (int c = 1; c <= 3; ++c) m->on_cycle(cycle(k * 3 * kS + c * kS, {Carrier{}.w_level(-70.0 - k - c).w_c1(10.0)}));
            }
            m->finish(61 * kS);
        }
        for (int f = 0; f < kFloors; ++f)
            for (const ys::Metric& mt : metrics) before[f].push_back(ys::point_list(*p, ys::PointQuery{floors[f], {}, mt, std::nullopt}));
        p->save();
    }
    auto p = pj::Project::open(file);
    const auto counts = ys::point_counts(*p);
    REQUIRE(counts.size() == static_cast<std::size_t>(kFloors));
    for (int f = 0; f < kFloors; ++f) {
        CAPTURE(f);
        CHECK(counts[f].first == floors[f]);
        for (std::size_t k = 0; k < metrics.size(); ++k) {
            CAPTURE(k);
            const ys::PointList after = ys::point_list(*p, ys::PointQuery{floors[f], {}, metrics[k], std::nullopt});
            CHECK(after.count() == counts[f].second);
            CHECK(after == before[f][k]);  // count, order, plan position, value, colour
            // The stored points, one for one: no thinning or re-sampling.
            std::vector<pj::StoredPoint> stored;
            for (std::int64_t s : after.sessions)
                for (auto& sp : p->points(s)) stored.push_back(sp);
            REQUIRE(stored.size() == after.count());
            for (std::size_t i = 0; i < stored.size(); ++i) {
                REQUIRE(after.points[i].plan == stored[i].plan);
                REQUIRE(after.points[i].sequence == stored[i].sequence);
                const auto v = ys::metric_value(json::parse(stored[i].sample), metrics[k]);
                REQUIRE(after.points[i].value == (v ? std::optional<double>(v->value) : std::nullopt));
            }
            // The extra views leave the list as it is.
            (void)ys::bin_points(after, 1.0);
            (void)ys::with_offset(after, -6.0);
            CHECK(after == before[f][k]);
        }
        CHECK(counts[f].second == static_cast<std::size_t>(kCycles + 1 + 20));
    }
}
