// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Survey maths and walk tests on synthetic measurement cycles (TST-32,
// IND-20, IND-30, GEO-02, GEO-05, GEO-06, EXP-05, EXP-06, PP-02,
// docs/SURVEY.md): sample placement on clicked segments, continuous mode,
// pause and delete last segment, manual points averaged over N seconds, a
// floor change in the middle of a walk, the values kept per point,
// statistics against hand-computed values, and the one point list of the
// window and the report (TST-34).

#include "yantra/core/building/edit.h"
#include "yantra/core/project/project.h"
#include "yantra/core/survey/points.h"
#include "yantra/core/survey/recordings.h"
#include "yantra/core/survey/sample.h"
#include "yantra/core/survey/stats.h"
#include "yantra/core/survey/walk_test.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "survey_helpers.h"

TEST_CASE("TST-32 walk: the samples of a segment are spread evenly between the clicks; a sample at a click lies on it (GEO-02)") {
    const ys::WalkSegment s{1, {10 * kS, {100, 40}}, {14 * kS, {300, 140}}};
    CHECK(ys::place_on_segment(s, 10 * kS) == bm::Point2{100, 40});
    CHECK(ys::place_on_segment(s, 11 * kS) == bm::Point2{150, 65});
    CHECK(ys::place_on_segment(s, 12 * kS) == bm::Point2{200, 90});
    CHECK(ys::place_on_segment(s, 14 * kS) == bm::Point2{300, 140});
    CHECK(ys::segment_fraction(s, 13 * kS) == 0.75);

    Fixture fx("walk-placement");
    std::vector<std::optional<yantra::context::FloorPoint>> walker;
    auto w = fx.walk("walk", ys::WalkMode::PointAndClick, &walker);
    w->start(5 * kS);
    w->on_cycle(level_cycle(9 * kS, -60));  // before the start click: no position
    w->click(10 * kS, {100, 40});
    for (int i = 10; i <= 14; ++i) w->on_cycle(level_cycle(i * kS, -60 - i));
    CHECK(w->stored() == 0);  // waiting for the end click
    w->click(14 * kS + kS / 2, {300, 140});
    w->on_cycle(level_cycle(15 * kS, -50));  // after the end click: not placed
    CHECK(w->stored() == 5);
    const auto pts = fx.p->points(w->session_id());
    REQUIRE(pts.size() == 5);
    // T in [10, 14.5] s: fraction (T - 10) / 4.5 of the way.
    for (int i = 0; i < 5; ++i) {
        CAPTURE(i);
        const double f = i / 4.5;
        CHECK(pts[i].plan.x == doctest::Approx(100 + f * 200));
        CHECK(pts[i].plan.y == doctest::Approx(40 + f * 100));
        CHECK(pts[i].metres->x == doctest::Approx((100 + f * 200) * 0.05));
        CHECK(pts[i].sequence == i + 1);
        CHECK(pts[i].group == 1);
        CHECK(pts[i].kind == "walk");
        CHECK(pts[i].time_ns == (10 + i) * kS);
        CHECK(pts[i].utc_ns == kUtc0 + (10 + i) * kS);
        CHECK(json::parse(pts[i].sample)["carriers"][0]["level"] == -70.0 - i);
    }
    // The walk position follows the clicks.
    REQUIRE(walker.size() == 2);
    CHECK(walker[1]->plan == bm::Point2{300, 140});
    CHECK(walker[1]->floor_id == fx.ground);
    w->walker({200, 100});
    CHECK(walker.back()->plan == bm::Point2{200, 100});
    w->finish(16 * kS);
    CHECK_FALSE(walker.back());
    const auto session = fx.p->session(w->session_id());
    CHECK(session->kind == "walk");
    CHECK(session->walk_mode == "point_and_click");
    CHECK(!session->ended_utc.empty());
    CHECK(session->radio == "fake 0.1.0, fake:0");
    CHECK(json::parse(session->plugins) == json{{"fake", "0.1.0"}, {"test.tech", "1.0.0"}});
    CHECK(session->notes == "test");
    CHECK(session->calibration == R"({"key":"fake|0|"})");
    CHECK_THROWS_AS(w->click(17 * kS, {1, 1}), std::logic_error);
}

TEST_CASE("TST-32 walk: continuous mode, a late cycle, pause and delete last segment (GEO-02)") {
    Fixture fx("walk-continuous");
    auto w = fx.walk("walk", ys::WalkMode::Continuous);
    w->start(0);
    w->click(1 * kS, {0, 0});
    w->on_cycle(level_cycle(1 * kS, -60));
    w->on_cycle(level_cycle(2 * kS, -61));
    w->click(3 * kS, {200, 0});  // closes segment 1, opens segment 2 at the joint
    w->on_cycle(level_cycle(3 * kS, -62));  // late: arrives after the click, on the joint once
    w->on_cycle(level_cycle(4 * kS, -63));
    w->click(5 * kS, {200, 100});  // closes segment 2
    auto pts = fx.p->points(w->session_id());
    REQUIRE(pts.size() == 4);
    CHECK(plans(pts) == std::vector<bm::Point2>{{0, 0}, {100, 0}, {200, 0}, {200, 50}});
    CHECK(pts[2].group == 1);  // the joint belongs to the segment it ended
    CHECK(pts[3].group == 2);
    // Pause drops the open segment's waiting cycles.
    w->on_cycle(level_cycle(6 * kS, -64));
    w->pause(6 * kS + 1);
    w->click(8 * kS, {300, 100});  // a new start
    w->on_cycle(level_cycle(8 * kS, -65));
    w->on_cycle(level_cycle(9 * kS, -66));
    w->click(10 * kS, {300, 200});
    pts = fx.p->points(w->session_id());
    REQUIRE(pts.size() == 6);
    CHECK(pts[4].plan == bm::Point2{300, 100});
    CHECK(pts[5].plan == bm::Point2{300, 150});
    CHECK(pts[5].group == 3);
    // Delete last segment: its points go; the next click starts anew.
    CHECK(w->delete_last(10 * kS + 1));
    pts = fx.p->points(w->session_id());
    CHECK(pts.size() == 4);
    CHECK(w->stored() == 4);
    w->on_cycle(level_cycle(11 * kS, -67));  // no open start any more
    w->click(12 * kS, {0, 100});
    w->on_cycle(level_cycle(12 * kS, -68));
    w->click(13 * kS, {0, 200});
    pts = fx.p->points(w->session_id());
    REQUIRE(pts.size() == 5);
    CHECK(pts.back().group == 4);  // segment numbers are not reused
    CHECK(pts.back().sequence == 7);
    // Mode change back to point and click; the recorded mode is the session's start mode.
    w->set_mode(14 * kS, ys::WalkMode::PointAndClick);
    w->click(15 * kS, {10, 10});
    w->on_cycle(level_cycle(15 * kS, -69));
    w->click(16 * kS, {20, 10});
    w->on_cycle(level_cycle(16 * kS, -70));  // after: not in the closed segment (16 is its end: placed)
    w->on_cycle(level_cycle(17 * kS, -71));  // no open start in point and click
    CHECK(fx.p->points(w->session_id()).size() == 7);
    CHECK_THROWS_AS(w->click(14 * kS, {1, 1}), std::invalid_argument);  // back in time
}

TEST_CASE("TST-32 manual points: N seconds averaged as power per carrier; C1 and the newest identity kept (GEO-05)") {
    // 10 log10((1e-6 + 1e-7 + 1e-8) / 3) = 10 log10(3.7e-7) = -64.317983 dBm.
    CHECK(ys::power_mean_db({-60, -70, -80}) == doctest::Approx(-64.31798275933005).epsilon(1e-12));
    CHECK(ys::arithmetic_mean({1, 2, 6}) == 3.0);
    CHECK(ys::average_rule("dBm") == ys::AverageRule::Power);
    CHECK(ys::average_rule("%") == ys::AverageRule::Arithmetic);
    CHECK(std::isnan(ys::power_mean_db({})));

    Fixture fx("manual");
    auto w = fx.walk("manual");
    w->start(0);
    w->point(10 * kS, {50, 50}, 3.0, "Room 1");
    w->on_cycle(level_cycle(10 * kS, -50));  // at t itself: not in (t, t + 3]
    w->on_cycle(cycle(11 * kS, {Carrier{}.w_level(-60).w_c1(20.0), Carrier{}.w_hz(391'000'000).w_level(-90).w_role(ym::CarrierRole::Neighbour).w_cell(2)}));
    w->on_cycle(cycle(12 * kS, {Carrier{}.w_level(-70).w_c1(10.0).w_flags(ym::sample_flag::kStale)}));
    CHECK(w->stored() == 0);
    w->on_cycle(cycle(13 * kS, {Carrier{}.w_level(-80).w_cell(7)}));  // T = t + N: the last one, then stored
    REQUIRE(w->stored() == 1);
    w->on_cycle(level_cycle(14 * kS, -40));  // not in any point
    // The next point, ended by the end of the test with what it has.
    w->point(20 * kS, {150, 50}, 5.0, "Room 2");
    w->on_cycle(level_cycle(21 * kS, -75));
    w->finish(22 * kS);
    const auto pts = fx.p->points(w->session_id());
    REQUIRE(pts.size() == 2);
    CHECK(pts[0].plan == bm::Point2{50, 50});
    CHECK(pts[0].label == "Room 1");
    CHECK(pts[0].cycles == 3);
    CHECK(pts[0].kind == "manual");
    CHECK(pts[0].group == 1);
    CHECK(pts[0].time_ns == 11 * kS);
    const json s = json::parse(pts[0].sample);
    CHECK(s["cycles"] == 3);
    REQUIRE(s["carriers"].size() == 2);
    const json& serving = s["carriers"][0];
    CHECK(serving["level"].get<double>() == doctest::Approx(-64.31798275933005).epsilon(1e-12));
    CHECK(serving["n"] == 3);
    // C1: power mean of 20 and 10 dB = 10 log10((100 + 10) / 2) = 10 log10(55) = 17.403627 dB.
    CHECK(serving["c1"].get<double>() == doctest::Approx(17.403626894942438).epsilon(1e-12));
    CHECK(serving["cell"]["cell_id"] == "7");
    CHECK(serving["flags"] == "stale");
    CHECK(s["carriers"][1]["level"] == -90.0);
    CHECK(s["carriers"][1]["n"] == 1);
    CHECK(pts[1].cycles == 1);
    CHECK(json::parse(pts[1].sample)["carriers"][0]["level"] == -75.0);
    // Delete last removes the last point; a point being averaged is dropped first.
    CHECK_THROWS_AS(w->delete_last(23 * kS), std::logic_error);  // the test has ended
    Fixture fx2("manual-delete");
    auto w2 = fx2.walk("manual");
    w2->start(0);
    w2->point(1 * kS, {1, 1}, 1.0);
    w2->on_cycle(level_cycle(2 * kS, -60));
    w2->point(3 * kS, {2, 2}, 1.0);
    CHECK(w2->delete_last(3 * kS + 1));  // drops the point being averaged
    CHECK(w2->stored() == 1);
    CHECK(w2->delete_last(3 * kS + 2));
    CHECK(w2->stored() == 0);
    CHECK_THROWS_AS(w2->point(4 * kS, {1, 1}, 0.0), std::invalid_argument);
}

TEST_CASE("GEO-06 walk: a floor change in the middle of a walk goes on in a new session on the other floor") {
    Fixture fx("floor-change");
    auto w = fx.walk("walk", ys::WalkMode::Continuous);
    w->start(0);
    w->click(1 * kS, {0, 100});
    w->on_cycle(level_cycle(1 * kS, -60));
    w->on_cycle(level_cycle(2 * kS, -61));
    w->click(3 * kS, {200, 100});  // to the stairs
    w->on_cycle(level_cycle(3 * kS, -62));
    w->on_cycle(level_cycle(4 * kS, -63));  // on the stairs: open segment
    const std::int64_t ground_session = w->session_id();
    w->change_floor(5 * kS, fx.first);
    CHECK(w->floor_id() == fx.first);
    w->on_cycle(level_cycle(5 * kS, -64));  // no start on the new floor yet
    w->click(6 * kS, {200, 100});
    w->on_cycle(level_cycle(6 * kS, -70));
    w->on_cycle(level_cycle(7 * kS, -71));
    w->click(8 * kS, {200, 0});
    w->on_cycle(level_cycle(8 * kS, -72));  // late, at the end click
    w->finish(9 * kS);
    REQUIRE(w->sessions().size() == 2);
    const std::int64_t first_session = w->sessions()[1];
    CHECK(w->sessions()[0] == ground_session);
    CHECK(fx.p->points(ground_session).size() == 3);  // the open segment was dropped
    const auto up = fx.p->points(first_session);
    REQUIRE(up.size() == 3);
    CHECK(up[0].sequence == 1);
    CHECK(plans(up) == std::vector<bm::Point2>{{200, 100}, {200, 50}, {200, 0}});
    const auto s = fx.p->session(first_session);
    CHECK(s->floor_id == fx.first);
    CHECK(s->continues == ground_session);
    CHECK(s->walk_mode == "continuous");
    CHECK(!fx.p->session(ground_session)->ended_utc.empty());
    CHECK(fx.p->point_count(fx.ground) == 3);
    CHECK(fx.p->point_count(fx.first) == 3);
    CHECK(ys::point_counts(*fx.p) == std::vector<std::pair<std::string, std::size_t>>{{fx.ground, 3}, {fx.first, 3}});
}

TEST_CASE("RAD-19 simulated walk: each cycle with a simulator position on the floor is a point there") {
    Fixture fx("simulated");
    auto w = fx.walk("simulated");
    w->start(0);
    w->on_cycle(cycle(1 * kS, {Carrier{}.w_sim_floor(fx.ground).w_sim_x(2.5).w_sim_y(1.0)}));
    w->on_cycle(cycle(2 * kS, {Carrier{}.w_sim_floor(fx.first).w_sim_x(2.5).w_sim_y(1.0)}));  // other floor
    w->on_cycle(level_cycle(3 * kS, -60));  // no position
    w->on_cycle(cycle(4 * kS, {Carrier{}.w_sim_floor(fx.ground).w_sim_x(5.0).w_sim_y(2.0)}));
    const auto pts = fx.p->points(w->session_id());
    REQUIRE(pts.size() == 2);
    CHECK(pts[0].plan.x == doctest::Approx(50));
    CHECK(pts[0].plan.y == doctest::Approx(20));
    CHECK(pts[1].metres->x == doctest::Approx(5.0));
    CHECK(pts[1].kind == "simulated");
}

TEST_CASE("IND-30 sample values: what a point keeps of its cycle") {
    const ym::MeasurementCycle c =
        cycle(3 * kS, {Carrier{}.w_level(-71.5).w_c1(12.0).w_sim_floor("floor-1").w_sim_x(1).w_sim_y(2),
                       Carrier{}.w_hz(392'000'000).w_level(std::nan("")).w_role(ym::CarrierRole::NeighbourEntry),
                       Carrier{}.w_hz(393'000'000).w_level(-60).w_role(ym::CarrierRole::Neighbour).w_cell(3).w_flags(ym::sample_flag::kUncalibrated)});
    const json s = ys::sample_json(c);
    CHECK(s["cycle"] == 3);
    // The neighbour-list entry is kept without a level (the PP-01 readout
    // shows the whole announced list).
    REQUIRE(s["carriers"].size() == 3);
    CHECK(s["carriers"][1]["role"] == "neighbour_entry");
    CHECK(s["carriers"][1]["f"] == 392'000'000);
    CHECK(s["carriers"][1]["entry"] == 0);
    CHECK_FALSE(s["carriers"][1].contains("level"));
    const json& a = s["carriers"][0];
    CHECK(a["dev"] == "fake:0");
    CHECK(a["tech"] == "test");
    CHECK(a["schema"] == "test_cell");
    CHECK(a["role"] == "serving");
    CHECK(a["f"] == 390'000'000);
    CHECK(a["level"] == -71.5);
    CHECK(a["unit"] == "dBm");
    CHECK(a["c1"] == 12.0);
    CHECK(a["c1_src"] == "radio");
    CHECK(a["cell"] == json{{"cell_id", "1"}});
    CHECK(a["sim"] == json{{"floor", "floor-1"}, {"x_m", 1.0}, {"y_m", 2.0}});
    CHECK(a["age_ms"] == 200.0);
    CHECK(s["carriers"][2]["unit"] == "dBFS");
    CHECK(ys::sim_position(s)->x_m == 1.0);
    // Metrics: serving, strongest, a frequency; level, C1.
    CHECK(ys::metric_value(s, {})->value == -71.5);
    const auto strongest = ys::metric_value(s, MB{}.w_carrier(ys::Metric::Carrier::Strongest));
    CHECK(strongest->value == -60.0);
    CHECK(strongest->unit == "dBFS");
    CHECK(strongest->cell == "cell_id=3");
    CHECK(ys::metric_value(s, MB{}.w_value(ys::Metric::Value::C1))->value == 12.0);
    CHECK(ys::metric_value(s, MB{}.w_value(ys::Metric::Value::C1))->unit == "dB");
    CHECK(ys::metric_value(s, MB{}.w_carrier(ys::Metric::Carrier::Frequency).w_frequency_hz(393'000'000))->value == -60.0);
    CHECK_FALSE(ys::metric_value(s, MB{}.w_carrier(ys::Metric::Carrier::Frequency).w_frequency_hz(1)));
    CHECK_FALSE(ys::metric_value(s, MB{}.w_carrier(ys::Metric::Carrier::Frequency).w_frequency_hz(392'000'000)));
    CHECK_FALSE(ys::metric_value(s, MB{}.w_value(ys::Metric::Value::Quality).w_quality_field("mer_db")));
    CHECK(ys::serving_cell(s) == "cell_id=1");
    CHECK(ys::Metric{}.label() == "Level, serving carrier");
    CHECK(MB{}.w_carrier(ys::Metric::Carrier::Frequency).w_value(ys::Metric::Value::C1).w_frequency_hz(390'200'000).m.label() == "C1, 390.2000 MHz");
}

TEST_CASE("TST-32 coverage statistics against hand-computed values (EXP-05, EXP-06, PP-02)") {
    // 10 points: 8 with dBm values, one without a value, one in dBFS.
    std::vector<ys::SurveyPoint> pts;
    const double v[8] = {-60, -70, -75, -80, -85, -90, -95, -105};
    for (int i = 0; i < 8; ++i) pts.push_back(valued(v[i], 10.0 * i, 10, i < 5 ? "cell_id=1" : "cell_id=2"));
    ys::SurveyPoint none;
    none.session_id = 1;
    none.plan = {200, 200};
    pts.push_back(none);
    ys::SurveyPoint dbfs = valued(-40, 300, 300);
    dbfs.unit = "dBFS";
    pts.push_back(dbfs);
    ys::StatsOptions o;
    o.thresholds = {-85, -95};
    o.percentiles = {0, 10, 50, 100};
    const ys::CoverageStats s = ys::coverage_stats(pts, "dBm", o);
    CHECK(s.points == 10);
    CHECK(s.with_value == 8);
    CHECK(s.no_value == 1);
    CHECK(s.other_unit == 1);
    CHECK(s.min == -105.0);
    CHECK(s.max == -60.0);
    CHECK(*s.mean == doctest::Approx(-82.5));  // -660 / 8
    // Power mean: 10 log10(sum 10^(v/10) / 8).
    double sum = 0;
    for (double x : v) sum += std::pow(10.0, x / 10.0);
    CHECK(*s.power_mean == doctest::Approx(10 * std::log10(sum / 8)));
    CHECK(*s.power_mean == doctest::Approx(-68.43855).epsilon(1e-6));
    // R-7 median of 8 sorted values: h = 3.5, (-85 + -80) / 2.
    CHECK(*s.median == -82.5);
    // P10: h = 0.7, -105 + 0.7 (-95 - -105) = -98.
    REQUIRE(s.percentiles.size() == 4);
    CHECK(s.percentiles[0].second == -105.0);
    CHECK(s.percentiles[1].second == doctest::Approx(-98.0));
    CHECK(s.percentiles[3].second == -60.0);
    // >= -85: 5 of 10 points (the two without a dBm value count as not covered).
    REQUIRE(s.thresholds.size() == 2);
    CHECK(s.thresholds[0].at_or_above == 5);
    CHECK(s.thresholds[0].below == 3);
    CHECK(s.thresholds[0].percent_at_or_above == 50.0);
    CHECK(s.thresholds[0].percent_below == 30.0);
    CHECK(s.thresholds[1].at_or_above == 7);
    CHECK(s.thresholds[1].percent_at_or_above == 70.0);
    // Serving cells: 2 changes in session 1 (cell 1 -> 2 at the sixth
    // point, 2 -> 1 at the dBFS point; the point without a cell is
    // skipped); the shares by points.
    CHECK(s.serving_cell_changes == 2);
    REQUIRE(s.serving_cells.size() == 3);
    CHECK(s.serving_cells[0] == ys::CellShare{"cell_id=1", 6, 60.0});  // incl. the dBFS point
    CHECK(s.serving_cells[1] == ys::CellShare{"cell_id=2", 3, 30.0});
    CHECK(s.serving_cells[2] == ys::CellShare{"", 1, 10.0});
    CHECK(ys::percentile({}, 50) == std::nullopt);
    CHECK_THROWS_AS(ys::percentile({1}, 101), std::invalid_argument);

    // Areas: the square (0, 0)-(35, 20) holds the points at x 0, 10, 20, 30.
    const ys::Area room{"Room", {{-1, 0}, {35, 0}, {35, 20}, {-1, 20}}};
    CHECK(ys::points_in_area(pts, room.polygon).size() == 4);
    CHECK_FALSE(ys::point_in_polygon({40, 10}, room.polygon));
    // Acceptance (EXP-06): 50 % >= -85 passes, 80 % >= -95 fails (70 %).
    const ys::AcceptanceProfile pass{"Indoor", "dBm", {{-85, 50}}};
    const ys::AcceptanceProfile fail{"Strict", "dBm", {{-85, 50}, {-95, 80}}};
    const ys::AcceptanceResult r = ys::evaluate(fail, pts);
    CHECK(ys::evaluate(pass, pts).pass);
    CHECK_FALSE(r.pass);
    REQUIRE(r.criteria.size() == 2);
    CHECK(r.criteria[0].pass);
    CHECK_FALSE(r.criteria[1].pass);
    CHECK(r.criteria[1].percent == 70.0);
    CHECK_FALSE(ys::evaluate(pass, {}).pass);  // no points: not accepted

    ys::PointList list;
    list.floor_id = "floor-1";
    list.unit = "dBm";
    list.points = pts;
    const ys::FloorStats f = ys::floor_stats(list, o, {room}, {pass, fail});
    CHECK(f.all == s);
    REQUIRE(f.areas.size() == 1);
    CHECK(f.areas[0].first == "Room");
    CHECK(f.areas[0].second.points == 4);
    CHECK(*f.areas[0].second.mean == doctest::Approx(-71.25));  // (-60 - 70 - 75 - 80) / 4
    CHECK(f.areas[0].second.thresholds[0].percent_at_or_above == 100.0);
    REQUIRE(f.acceptance.size() == 2);
    CHECK(f.acceptance[1] == r);
    // Building: two floors together.
    ys::PointList other = list;
    other.floor_id = "floor-2";
    other.points = {valued(-50, 0, 0, "cell_id=9", 2), valued(-99, 0, 0, "cell_id=9", 2)};
    const ys::FloorStats all = ys::building_stats({list, other}, o, {pass});
    CHECK(all.all.points == 12);
    CHECK(all.all.thresholds[0].at_or_above == 6);
    CHECK(all.all.thresholds[0].percent_at_or_above == doctest::Approx(50.0));
    CHECK(all.all.serving_cell_changes == 2);  // per session: none across sessions
}

TEST_CASE("TST-34 one point list: the window and the report get every stored point, same order, position, value and colour") {
    Fixture fx("shared-list");
    auto w = fx.walk("walk", ys::WalkMode::Continuous);
    w->start(0);
    // A dense walk: 2000 cycles at 0.1 s over 40 segments.
    constexpr int kCycles = 2000;
    w->click(0, {0, 0});
    for (int i = 0; i <= kCycles; ++i) {
        const std::int64_t t = i * kS / 10;
        w->on_cycle(cycle(t, {Carrier{}.w_level(-60.0 - (i % 50)).w_cell(1 + (i / 500))}));
        if (i % 50 == 0 && i > 0) w->click(t, {static_cast<double>(i % 400), static_cast<double>((i / 400) * 40)});
    }
    w->finish(kCycles * kS / 10 + 1);
    const std::size_t stored = fx.p->point_count(fx.ground);
    CHECK(stored == kCycles + 1);
    // The "window" and the "report" are two consumers of the same API.
    auto window = [&] { return ys::point_list(*fx.p, ys::PointQuery{fx.ground, {}, {}, std::nullopt}); };
    auto report = [&](const ys::Metric& m) {
        ys::PointList l = ys::point_list(*fx.p, ys::PointQuery{fx.ground, {}, m, std::nullopt});
        return std::pair{l, ys::coverage_stats(l, ys::StatsOptions{{-85}, {50}})};
    };
    const ys::PointList shown = window();
    const auto [printed, stats] = report(ys::Metric{});
    CHECK(shown.count() == stored);
    CHECK(printed == shown);  // count, order, plan position, value, colour
    CHECK(stats.points == shown.count());
    CHECK(shown.to_scale);
    CHECK(shown.unit == "dBm");
    CHECK_FALSE(shown.mixed_units);
    for (std::size_t i = 1; i < shown.points.size(); ++i) REQUIRE(shown.points[i].sequence == shown.points[i - 1].sequence + 1);
    CHECK(shown.points[0].rgb == ys::default_colour_scale("dBm").colour_of(-60.0));
    CHECK(shown.points[0].rgb == 0x43a047);
    CHECK(shown.points[25].rgb == 0xfb8c00);  // -85 dBm: the -90 band
    CHECK(shown.points[45].rgb == shown.scale.outside_rgb);  // -105 is below every band
    CHECK(shown.points[49].rgb == shown.scale.outside_rgb);
    CHECK(stats.serving_cell_changes == 4);
    // A session selection and an own scale; a session of another floor is refused.
    ys::ColourScale scale = ys::default_colour_scale("dBm");
    scale.bands = {{-80, 0x00ff00, "ok"}};
    const ys::PointList own = ys::point_list(*fx.p, ys::PointQuery{fx.ground, {w->session_id()}, {}, scale});
    CHECK(own.count() == stored);
    CHECK(own.points[0].rgb == 0x00ff00);
    CHECK(own.points[45].rgb == scale.outside_rgb);
    pj::Session other;
    other.floor_id = fx.first;
    const std::int64_t up = fx.p->add_session(other);
    CHECK_THROWS_AS(ys::point_list(*fx.p, ys::PointQuery{fx.ground, {up}, {}, std::nullopt}), pj::ProjectError);
    CHECK_THROWS_AS(ys::point_list(*fx.p, ys::PointQuery{"floor-9", {}, {}, std::nullopt}), pj::ProjectError);
    // Default scales.
    CHECK(ys::default_colour_scale("%").colour_of(0.5) == 0x43a047);
    CHECK(ys::default_colour_scale("%").colour_of(20.0) == ys::default_colour_scale("%").outside_rgb);
    CHECK(ys::default_colour_scale("dB").colour_of(std::nullopt) == 0x9e9e9e);
}

TEST_CASE("REC-03 recording list: a file that is not a recording is refused, a missing one too") {
    Fixture fx("recordings");
    const fs::path dir = scratch("recordings-files");
    std::ofstream(dir / "notes.yrec") << "not a recording\n";
    try {
        ys::add_recording_file(*fx.p, dir / "notes.yrec");
        FAIL("no error");
    } catch (const pj::ProjectError& e) {
        CHECK(e.code() == pj::ProjectError::Code::Invalid);
    }
    try {
        ys::add_recording_file(*fx.p, dir / "missing.yrec");
        FAIL("no error");
    } catch (const pj::ProjectError& e) {
        CHECK(e.code() == pj::ProjectError::Code::Io);
    }
    CHECK(fx.p->recordings().empty());
}

TEST_CASE("IND-20 walk: a session that starts before the first cycle takes its UTC from the given clock (DEMO-06)") {
    Fixture fx("walk-clock");
    ys::WalkTestOptions o;
    o.project = fx.p;
    o.floor_id = fx.ground;
    o.name = "Walk";
    // 2026-11-02T10:00:00Z at monotonic 5 s; the walk starts at 7 s.
    o.clock = std::make_shared<yantra::platform::ManualClock>(5 * kS, std::int64_t{1'793'613'600} * kS);
    auto w = std::make_shared<ys::WalkTest>(o);
    w->start(7 * kS);
    const auto sessions = fx.p->sessions(fx.ground);
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].started_utc == "2026-11-02T10:00:02Z");
}
