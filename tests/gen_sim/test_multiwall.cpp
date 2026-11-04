// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Indoor multi-wall model (RAD-73, RAD-93) on fixture scenes: levels
// against hand-calculated values (free-space loss, walls crossed, the
// crossing rules, 3D distance, floors, patterns, reflections, the power
// sum over DAS antennas) and determinism of the seeded shadowing. The
// expected numbers were worked out by hand (20 log10(4 pi d f / c) with
// c = 299792458 m/s), not with this code.
#include <doctest/doctest.h>

#include "yantra/gen/multiwall.hpp"

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

using namespace yantra::gen;
using yantra::radios::Scene;
using yantra::radios::SceneAntenna;
using yantra::radios::ScenePattern;
using yantra::radios::SceneWall;

namespace {

// FSPL at 390 MHz: 10 m 44.26908 dB, 20 m 50.28968 dB.
constexpr double kFspl10 = 44.26907536241336;
constexpr double kFspl20 = 50.289675275692986;

Scene fixture()
{
    std::ifstream in(std::string(YANTRA_TEST_DATA_DIR) + "/scenes/multiwall_v1.json");
    std::ostringstream s;
    s << in.rdbuf();
    Scene scene;
    std::string error;
    REQUIRE_MESSAGE(yantra::radios::parse_scene(s.str(), scene, error), error);
    return scene;
}

// An empty 1-cell scene at 390 MHz with one antenna at (0, 5), 20 dBm,
// at the receiver height.
Scene open_room()
{
    Scene s;
    s.floor = {"r", "Room", true, 100.0, 100.0, std::nullopt, std::nullopt, {}};
    s.cells.push_back({"c", "C", 901, 1, 1, 1, 390000000});
    SceneAntenna a;
    a.id = "a";
    a.cell = "c";
    a.x_m = 0.0;
    a.y_m = 5.0;
    a.height_m = 1.5;
    a.eirp_dbm = 20.0;
    s.antennas.push_back(a);
    return s;
}

}  // namespace

TEST_CASE("multiwall: free space and walls crossed on the fixture scene")
{
    const Scene s = fixture();
    MultiWallModel m;

    // (8, 10): a1 3 m away in the open; a2 27 m away through the brick
    // wall (10 dB) and exactly through the joint of the two glass pieces
    // at (20, 10), which counts once (2 dB).
    const CellLevel near = m.cell(s, "das", {8.0, 10.0});
    REQUIRE(near.antennas.size() == 2);
    CHECK(near.carrier_hz == 390000000u);
    CHECK(near.antennas[0].antenna == "a1");
    REQUIRE(near.antennas[0].paths.size() == 1);
    CHECK(near.antennas[0].paths[0].walls == 0);
    CHECK(near.antennas[0].distance_m == doctest::Approx(3.0));
    CHECK(near.antennas[0].level_dbm == doctest::Approx(-13.811500456806606));
    CHECK(near.antennas[1].paths[0].walls == 2);
    CHECK(near.antennas[1].paths[0].wall_loss_db == doctest::Approx(12.0));
    CHECK(near.antennas[1].level_dbm == doctest::Approx(-44.8963506455931));
    CHECK(near.best == 0);

    // (15, 10): a1 10 m through brick, a2 20 m through glass; the cell is
    // the power sum.
    const CellLevel mid = m.cell(s, "das", {15.0, 10.0});
    CHECK(mid.antennas[0].level_dbm == doctest::Approx(20.0 - kFspl10 - 10.0));
    CHECK(mid.antennas[1].level_dbm == doctest::Approx(20.0 - kFspl20 - 2.0));
    CHECK(mid.level_dbm == doctest::Approx(-30.157268307048927));
    CHECK(mid.best == 1);

    // A receiver on a wall is on neither side of it: (10, 10) is 5 m from
    // a1 without the brick loss.
    CHECK(m.cell(s, "das", {10.0, 10.0}).antennas[0].level_dbm == doctest::Approx(-18.248475449133736));
    CHECK(m.cell(s, "das", {10.0, 10.0}).antennas[0].paths[0].walls == 0);

    // The antenna one storey up (scene: storey 4 m, slab 20 dB) right
    // above the receiver: 4 m at 390.025 MHz, one slab.
    const CellLevel up = m.cell(s, "up", {15.0, 10.0});
    REQUIRE(up.antennas.size() == 1);
    CHECK(up.antennas[0].distance_m == doctest::Approx(4.0));
    CHECK(up.antennas[0].floor_loss_db == doctest::Approx(20.0));
    CHECK(up.level_dbm == doctest::Approx(-33.31083195892492));

    // Every cell, in scene order; an unknown cell is refused.
    const auto all = m.cells(s, {15.0, 10.0});
    REQUIRE(all.size() == 2);
    CHECK(all[0].cell == "das");
    CHECK(all[1].level_dbm == up.level_dbm);
    CHECK_THROWS_AS(m.cell(s, "nope", {1.0, 1.0}), std::invalid_argument);
}

TEST_CASE("multiwall: crossing rules")
{
    Scene s = open_room();
    // An L of two walls meeting at (10, 10) at a right angle.
    s.walls.push_back({10.0, 0.0, 10.0, 10.0, "", 3.0, "", {}});
    s.walls.push_back({10.0, 10.0, 20.0, 10.0, "", 4.0, "", {}});
    // Through the corner: both walls count.
    WallCrossings c = walls_crossed(s, {5.0, 15.0}, {15.0, 5.0});
    CHECK(c.crossings.size() == 2);
    CHECK(c.total_db == doctest::Approx(7.0));
    // Along a wall: no crossing.
    CHECK(walls_crossed(s, {10.0, 1.0}, {10.0, 9.0}).crossings.empty());
    // Ending on a wall: no crossing; starting on one: no crossing.
    CHECK(walls_crossed(s, {5.0, 5.0}, {10.0, 5.0}).crossings.empty());
    CHECK(walls_crossed(s, {10.0, 5.0}, {15.0, 5.0}).crossings.empty());
    // Ordered along the path, skip leaves walls out.
    c = walls_crossed(s, {15.0, 15.0}, {5.0, 3.0});
    REQUIRE(c.crossings.size() == 2);
    CHECK(c.crossings[0].wall == 1);
    CHECK(c.crossings[1].wall == 0);
    CHECK(c.crossings[0].t < c.crossings[1].t);
    CHECK(walls_crossed(s, {15.0, 15.0}, {5.0, 3.0}, {1}).crossings.size() == 1);
    CHECK(segment_intersection({0, 0}, {2, 0}, {1, -1}, {1, 1}) == doctest::Approx(0.5));
    CHECK_FALSE(segment_intersection({0, 0}, {2, 0}, {0, 1}, {2, 1}).has_value());
    CHECK_FALSE(segment_intersection({0, 0}, {2, 0}, {3, -1}, {3, 1}).has_value());
}

TEST_CASE("multiwall: 3D distance, antenna patterns and floor defaults")
{
    Scene s = open_room();
    MultiWallModel m;
    // 2 m above the receiver height: sqrt(10^2 + 2^2) m.
    s.antennas[0].height_m = 3.5;
    CHECK(m.cell(s, "c", {10.0, 5.0}).level_dbm == doctest::Approx(-24.43940875540116));
    s.antennas[0].height_m = 1.5;
    MultiWallModel low({.receiver_height_m = 0.5});
    CHECK(low.cell(s, "c", {10.0, 5.0}).antennas[0].distance_m == doctest::Approx(std::sqrt(101.0)));

    // Sector towards +x (90 deg), 60 deg wide, 20 dB front to back:
    // 3 dB down at 30 deg off, front to back behind.
    s.antennas[0].pattern = {ScenePattern::Type::sector, 90.0, 60.0, 20.0, 0.0, {}};
    CHECK(m.cell(s, "c", {10.0, 5.0}).level_dbm == doctest::Approx(20.0 - kFspl10));
    const double r = 10.0;
    const double a = 60.0 * 3.14159265358979323846 / 180.0;
    CHECK(m.cell(s, "c", {r * std::sin(a), 5.0 - r * std::cos(a)}).level_dbm ==
          doctest::Approx(20.0 - kFspl10 - 3.0));
    CHECK(m.cell(s, "c", {-10.0, 5.0}).level_dbm == doctest::Approx(20.0 - kFspl10 - 20.0));
    CHECK(pattern_gain_db(s.antennas[0].pattern, 90.0 + 15.0) == doctest::Approx(-0.75));
    CHECK(pattern_gain_db(s.antennas[0].pattern, 90.0 - 15.0) == doctest::Approx(-0.75));

    // Table, clockwise from the azimuth (0 deg = up on the plan).
    const ScenePattern t{ScenePattern::Type::table, 0.0, 0.0, 0.0, 90.0, {0.0, -6.0, -15.0, -6.0}};
    CHECK(pattern_gain_db(t, 0.0) == doctest::Approx(0.0));
    CHECK(pattern_gain_db(t, 45.0) == doctest::Approx(-3.0));
    CHECK(pattern_gain_db(t, 180.0) == doctest::Approx(-15.0));
    CHECK(pattern_gain_db(t, 200.0) == doctest::Approx(-13.0));
    CHECK(pattern_gain_db(t, 315.0) == doctest::Approx(-3.0));
    const ScenePattern short_table{ScenePattern::Type::table, 90.0, 0.0, 0.0, 90.0, {0.0, -10.0}};
    CHECK(pattern_gain_db(short_table, 180.0) == doctest::Approx(-10.0));
    // 270 deg off: two thirds of the way from the last entry (90) back to
    // the first (360).
    CHECK(pattern_gain_db(short_table, 0.0) == doctest::Approx(-10.0 / 3.0));
    CHECK(bearing_deg({0, 0}, {0, -1}) == doctest::Approx(0.0));
    CHECK(bearing_deg({0, 0}, {1, 0}) == doctest::Approx(90.0));
    CHECK(bearing_deg({0, 0}, {-1, 0}) == doctest::Approx(270.0));

    // Without scene values the model's slab (15 dB) and storey (3 m).
    s.antennas[0].pattern = {};
    s.antennas[0].floor_offset = -2;
    const CellLevel below = m.cell(s, "c", {0.0, 5.0});
    CHECK(below.antennas[0].distance_m == doctest::Approx(6.0));
    CHECK(below.antennas[0].floor_loss_db == doctest::Approx(30.0));

    // DAS: two equal antennas give 3 dB more than one.
    s.antennas[0].floor_offset = 0;
    SceneAntenna b = s.antennas[0];
    b.id = "b";
    b.x_m = 20.0;
    s.antennas.push_back(b);
    CHECK(m.cell(s, "c", {10.0, 5.0}).level_dbm == doctest::Approx(20.0 - kFspl10 + 10.0 * std::log10(2.0)));

    // A cell without antennas has no signal.
    s.cells.push_back({"empty", "", 901, 1, 1, 1, 390050000});
    CHECK(m.cell(s, "empty", {1.0, 1.0}).level_dbm == -std::numeric_limits<double>::infinity());
}

TEST_CASE("multiwall: specular reflections (image method)")
{
    Scene s = open_room();
    s.wall_classes.push_back({"brick", "Brick", 10.0, 4.0});
    s.walls.push_back({-100.0, 0.0, 100.0, 0.0, "brick", 10.0, "", {}});
    // Without reflections the floor wall changes nothing.
    CHECK(MultiWallModel().cell(s, "c", {10.0, 5.0}).level_dbm == doctest::Approx(20.0 - kFspl10));

    // One bounce off y = 0: path length sqrt(200) m, the class's 4 dB.
    MultiWallModel one({.reflections = 1});
    CellLevel l = one.cell(s, "c", {10.0, 5.0});
    REQUIRE(l.antennas[0].paths.size() == 2);
    const PathContribution& p = l.antennas[0].paths[1];
    CHECK(p.bounces == 1);
    CHECK(p.reflected == std::vector<size_t>{0});
    CHECK(p.length_m == doctest::Approx(std::sqrt(200.0)));
    CHECK(p.reflection_loss_db == doctest::Approx(4.0));
    CHECK(p.walls == 0);  // the reflecting wall is not crossed
    CHECK(p.level_dbm == doctest::Approx(-31.279375319053173));
    CHECK(l.level_dbm == doctest::Approx(-23.48068944243412));

    // A second wall at y = 10 without a class: the model's 6 dB. Order 1:
    // the direct path and one bounce off each wall; order 2 adds y0-y10
    // and y10-y0 (sqrt(500) m, 10 dB each).
    s.walls.push_back({-100.0, 10.0, 100.0, 10.0, "", 3.0, "", {}});
    CHECK(one.cell(s, "c", {10.0, 5.0}).level_dbm == doctest::Approx(-23.04807078671538));
    MultiWallModel two({.reflections = 2});
    l = two.cell(s, "c", {10.0, 5.0});
    REQUIRE(l.antennas[0].paths.size() == 5);
    CHECK(l.antennas[0].paths[3].bounces == 2);
    CHECK(l.antennas[0].paths[3].length_m == doctest::Approx(std::sqrt(500.0)));
    CHECK(l.antennas[0].paths[3].reflection_loss_db == doctest::Approx(10.0));
    CHECK(l.level_dbm == doctest::Approx(-22.91886922839611));

    // A wall between the reflection point and the receiver is crossed on
    // that leg: a 5 dB wall at x = 7 from y = 0.5 to 2 (below the direct
    // path, on the bounce's second leg).
    Scene t = open_room();
    t.wall_classes.push_back({"brick", "Brick", 10.0, 4.0});
    t.walls.push_back({-100.0, 0.0, 100.0, 0.0, "brick", 10.0, "", {}});
    t.walls.push_back({7.0, 0.5, 7.0, 2.0, "", 5.0, "", {}});
    l = one.cell(t, "c", {10.0, 5.0});
    REQUIRE(l.antennas[0].paths.size() == 2);
    CHECK(l.antennas[0].paths[0].walls == 0);
    CHECK(l.antennas[0].paths[1].walls == 1);
    CHECK(l.antennas[0].paths[1].level_dbm == doctest::Approx(-31.279375319053173 - 5.0));

    CHECK_THROWS_AS(MultiWallModel({.reflections = 3}), std::invalid_argument);
}

TEST_CASE("multiwall: seeded shadowing is deterministic")
{
    const Scene s = fixture();
    const MultiWallParams p{.shadowing_sigma_db = 4.0, .shadowing_decorrelation_m = 3.0, .seed = 3};
    MultiWallModel a(p), b(p);
    MultiWallParams q = p;
    q.seed = 4;
    MultiWallModel c(q);
    const CellLevel plain = MultiWallModel().cell(s, "das", {15.0, 12.0});
    const CellLevel la = a.cell(s, "das", {15.0, 12.0});
    CHECK(la.level_dbm == b.cell(s, "das", {15.0, 12.0}).level_dbm);
    CHECK(la.level_dbm != c.cell(s, "das", {15.0, 12.0}).level_dbm);
    // Shadowing is per antenna and subtracted from its level.
    CHECK(la.antennas[0].shadowing_db != la.antennas[1].shadowing_db);
    CHECK(la.antennas[0].level_dbm == doctest::Approx(plain.antennas[0].level_dbm - la.antennas[0].shadowing_db));
    // Over the floor the shadowing has about its sigma.
    double sum2 = 0.0;
    int n = 0;
    for (double x = 0.5; x < 40.0; x += 1.7)
        for (double y = 0.5; y < 20.0; y += 1.3, ++n) {
            const double v = a.cell(s, "das", {x, y}).antennas[0].shadowing_db;
            sum2 += v * v;
        }
    CHECK(std::sqrt(sum2 / n) == doctest::Approx(4.0).epsilon(0.35));
}
