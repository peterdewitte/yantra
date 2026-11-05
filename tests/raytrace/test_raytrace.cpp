// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ray-tracing propagation model (IND-40, IND-44; the TST-36 parts of step
// 8.1): free space, a single wall, a reflection and a corner against
// hand-calculated values, reflection count limits, per-cell carriers, the
// inter-floor term, grid versus points, SIMD versus scalar and the same
// map in every build tree, threads, cancellation and progress. The
// comparison with the simulator's multi-wall model (RAD-73) is in
// tests/measure_host/test_measure_raytrace.cpp.
// Copied from yantra-v2 68ea20a: tests/raytrace/test_raytrace.cpp,
// adapted (building model, per-cell carriers, corner case, free space
// below d0, golden map digest).

#include "office.h"
#include "yantra/core/building/geometry.h"
#include "yantra/core/raytrace/model.h"

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <stdexcept>

using namespace yantra;
using building::Point2;
using raytrace::ModelParams;

namespace {

// Hand-calculated free-space losses 20 log10(4 pi d f / c), dB, at 400 MHz.
constexpr double kFspl1m400 = 24.48898304844262;
constexpr double kFspl10m400 = 44.48898304844263;
constexpr double kFspl6m400 = 40.0520080561155;
constexpr double kFsplSqrt52m400 = 41.64901648479061;

// The office map digest (see its test case), the same in every tree.
constexpr std::size_t kOfficeRays = 549532;
constexpr std::int64_t kOfficeDigest = -264450208462;

// The same formula, for distances the cases below work out by hand.
double fspl(double d_m, double hz) { return 20.0 * std::log10(4.0 * std::numbers::pi * d_m * hz / 299'792'458.0); }

double power_sum_dbm(std::initializer_list<double> dbm) {
    double mw = 0.0;
    for (double x : dbm) mw += std::pow(10.0, x / 10.0);
    return 10.0 * std::log10(mw);
}

/// A floor in metres (1 plan unit = 1 m, no reference point), 20 x 20 m.
building::Floor metre_floor(const std::string& id = "f", int level = 0) {
    building::Floor f;
    f.id = id;
    f.level = level;
    f.scale = test::factor_scale(1.0);
    f.plan_width = 20.0;
    f.plan_height = 20.0;
    return f;
}

/// A building with one metre floor and a cell at `mhz`.
building::Building metre_building(double mhz = 400.0) {
    building::Building b;
    b.cells.push_back(test::tetra_cell("cell", static_cast<std::uint64_t>(mhz * 1e6)));
    b.floors.push_back(metre_floor());
    return b;
}

building::Antenna antenna(const std::string& id, Point2 pos, double eirp = 10.0, double height = 1.5,
                          const std::string& cell = "cell") {
    building::Antenna a;
    a.id = id;
    a.position = pos;
    a.height_m = height;
    a.eirp_dbm = eirp;
    a.cell = cell;
    return a;
}

building::Wall wall(const std::string& id, const std::string& cls, std::vector<Point2> pts) {
    return {id, cls, std::move(pts), std::nullopt};
}

ModelParams params(int reflections = 0) {
    ModelParams p;
    p.max_reflections = reflections;
    p.grid_m = 1.0;
    return p;
}

double level_at(const building::Building& b, const std::string& floor, Point2 pt, const ModelParams& p) {
    auto set = raytrace::trace_points(b, floor, {pt}, p);
    return raytrace::evaluate(b, set, p).best_dbm.at(0);
}

}  // namespace

TEST_CASE("free space: the level of a grid cell is EIRP minus the hand-calculated free-space loss (IND-40)") {
    building::Building b = metre_building();
    b.floors[0].antennas.push_back(antenna("a", {0.5, 0.5}));
    auto m = raytrace::compute_map(b, "f", params());
    REQUIRE(m.grid.cols == 20);
    REQUIRE(m.grid.rows == 20);
    REQUIRE(m.groups.size() == 1);
    CHECK(m.groups[0].key == "cell:cell");
    CHECK(m.groups[0].frequency_hz == 400e6);
    CHECK(m.grid.centre(10, 0).x == doctest::Approx(10.5));
    // Cell (10, 0): 10 m from the antenna at the same height.
    CHECK(m.best_dbm[m.grid.index(10, 0)] == doctest::Approx(10.0 - kFspl10m400).epsilon(1e-12));
    CHECK(m.level_dbm[0][m.grid.index(10, 0)] == m.best_dbm[m.grid.index(10, 0)]);
    CHECK(m.best_group[m.grid.index(10, 0)] == 0);
    // At the antenna: the distance is held at 1 cm (as RAD-73).
    CHECK(m.best_dbm[m.grid.index(0, 0)] == doctest::Approx(10.0 - (kFspl10m400 - 60.0)).epsilon(1e-12));
    // Exponent 3 beyond the 1 m reference distance (log-distance model),
    // free space up to it.
    auto p3 = params();
    p3.path_loss_exponent = 3.0;
    CHECK(level_at(b, "f", {10.5, 0.5}, p3) == doctest::Approx(10.0 - kFspl1m400 - 30.0).epsilon(1e-12));
    CHECK(level_at(b, "f", {1.0, 0.5}, p3) == doctest::Approx(10.0 - (kFspl1m400 + 20.0 * std::log10(0.5))).epsilon(1e-12));
    // Height difference: the 3D distance counts.
    b.floors[0].antennas[0].height_m = 1.5 + 10.0;
    CHECK(level_at(b, "f", {10.5, 0.5}, params()) ==
          doctest::Approx(10.0 - kFspl10m400 - 20.0 * std::log10(std::sqrt(2.0))).epsilon(1e-12));
    // A fixed frequency for every cell instead of the cell's carrier.
    auto p8 = params();
    p8.frequency_hz = 800e6;
    CHECK(level_at(b, "f", {10.5, 0.5}, p8) ==
          doctest::Approx(10.0 - kFspl10m400 - 20.0 * std::log10(2.0 * std::sqrt(2.0))).epsilon(1e-12));
}

TEST_CASE("single wall: the hand-calculated wall loss at the cell's carrier (IND-40, IND-11)") {
    building::Building b = metre_building();
    b.floors[0].walls.push_back(wall("w", "concrete", {{5.0, -5.0}, {5.0, 25.0}}));
    b.floors[0].antennas.push_back(antenna("a", {0.5, 0.5}));
    auto m = raytrace::compute_map(b, "f", params());
    // Concrete: 12 dB at 400 MHz (default table).
    CHECK(m.best_dbm[m.grid.index(10, 0)] == doctest::Approx(10.0 - kFspl10m400 - 12.0).epsilon(1e-12));
    CHECK(m.best_dbm[m.grid.index(2, 0)] == doctest::Approx(10.0 - (kFspl1m400 + 20.0 * std::log10(2.0))).epsilon(1e-12));
    // At 600 MHz: linear in log f between 12 dB (400 MHz) and 23 dB (900 MHz).
    b.cells[0].carrier_hz = 600'000'000;
    const double wall600 = 12.0 + std::log(600.0 / 400.0) / std::log(900.0 / 400.0) * 11.0;
    CHECK(level_at(b, "f", {10.5, 0.5}, params()) ==
          doctest::Approx(10.0 - fspl(10.0, 600e6) - wall600).epsilon(1e-12));
    b.cells[0].carrier_hz = 400'000'000;
    // A fitted class loss replaces the table; a wall's own value stays.
    auto pf = params();
    pf.wall_class_loss_db["concrete"] = 7.5;
    CHECK(level_at(b, "f", {10.5, 0.5}, pf) == doctest::Approx(10.0 - kFspl10m400 - 7.5).epsilon(1e-12));
    b.floors[0].walls[0].attenuation_db = 4.0;
    CHECK(level_at(b, "f", {10.5, 0.5}, pf) == doctest::Approx(10.0 - kFspl10m400 - 4.0).epsilon(1e-12));
    // No reflection reaches the far side of a single wall.
    b.floors[0].walls[0].attenuation_db.reset();
    CHECK(level_at(b, "f", {10.5, 0.5}, params(2)) == doctest::Approx(10.0 - kFspl10m400 - 12.0).epsilon(1e-12));
    // A polyline through a vertex on the ray counts once (walls_crossed rule).
    b.floors[0].walls[0] = wall("w", "concrete", {{4.0, -5.0}, {5.0, 0.5}, {4.0, 25.0}});
    CHECK(level_at(b, "f", {10.5, 0.5}, params()) == doctest::Approx(10.0 - kFspl10m400 - 12.0).epsilon(1e-12));
}

TEST_CASE("single reflection: direct and image ray summed in power (hand-calculated, IND-40)") {
    building::Building b = metre_building();
    b.floors[0].walls.push_back(wall("w", "concrete", {{-10.0, 12.0}, {30.0, 12.0}}));
    b.floors[0].antennas.push_back(antenna("a", {2.0, 10.0}));
    auto p = params(1);
    auto set = raytrace::trace_points(b, "f", {{8.0, 10.0}}, p);
    REQUIRE(set.rays[0].size() == 2);
    CHECK(set.rays[0][0].reflections == 0);
    CHECK(set.rays[0][0].length_m == doctest::Approx(6.0));
    CHECK(set.rays[0][1].reflections == 1);
    CHECK(set.rays[0][1].default_reflections == 1);
    CHECK(set.rays[0][1].length_m == doctest::Approx(std::sqrt(52.0)));
    CHECK(set.rays[0][1].class_crossings.empty());  // the reflecting wall is not crossed
    const double want = power_sum_dbm({10.0 - kFspl6m400, 10.0 - kFsplSqrt52m400 - 6.0});
    CHECK(raytrace::evaluate(b, set, p).best_dbm[0] == doctest::Approx(want).epsilon(1e-12));
    CHECK(want == doctest::Approx(-29.355697659308962).epsilon(1e-12));
    // The grid gives the same at that point's cell centre.
    CHECK(raytrace::compute_map(b, "f", [] {
              auto q = params(1);
              q.grid_m = 2.0;
              return q;
          }()).best_dbm[raytrace::Grid{{0, 0}, 2.0, 10, 10}.index(4, 5)] ==
          doctest::Approx(power_sum_dbm({10.0 - fspl(std::hypot(7.0, 1.0), 400e6),
                                         10.0 - fspl(std::hypot(7.0, 3.0), 400e6) - 6.0}))
              .epsilon(1e-12));
    // A reflection loss of 10 dB instead of 6.
    p.reflection_loss_db = 10.0;
    CHECK(raytrace::evaluate(b, set, p).best_dbm[0] ==
          doctest::Approx(power_sum_dbm({10.0 - kFspl6m400, 10.0 - kFsplSqrt52m400 - 10.0})).epsilon(1e-12));
    // The wall class's own reflection loss wins over the parameter.
    for (auto& c : b.wall_classes)
        if (c.id == "concrete") c.reflection_loss_db = 3.0;
    auto set_c = raytrace::trace_points(b, "f", {{8.0, 10.0}}, p);
    CHECK(set_c.rays[0][1].default_reflections == 0);
    CHECK(set_c.rays[0][1].class_reflection_db == 3.0);
    CHECK(raytrace::evaluate(b, set_c, p).best_dbm[0] ==
          doctest::Approx(power_sum_dbm({10.0 - kFspl6m400, 10.0 - kFsplSqrt52m400 - 3.0})).epsilon(1e-12));
    for (auto& c : b.wall_classes) c.reflection_loss_db.reset();
    // A wall between the reflection point and the receiver attenuates the
    // second leg only.
    b.floors[0].walls.push_back(wall("g", "glass", {{7.0, 10.5}, {7.0, 11.9}}));
    p.reflection_loss_db = 6.0;
    auto set2 = raytrace::trace_points(b, "f", {{8.0, 10.0}}, p);
    REQUIRE(set2.rays[0].size() == 2);
    CHECK(set2.rays[0][0].class_crossings.empty());
    REQUIRE(set2.rays[0][1].class_crossings.size() == 1);
    CHECK(set2.wall_classes[static_cast<size_t>(set2.rays[0][1].class_crossings[0].first)] == "glass");
    CHECK(raytrace::evaluate(b, set2, p).best_dbm[0] ==
          doctest::Approx(power_sum_dbm({10.0 - kFspl6m400, 10.0 - kFsplSqrt52m400 - 6.0 - 2.0})).epsilon(1e-12));
}

TEST_CASE("corner: two walls meeting at a right angle, every ray worked out by hand (IND-40)") {
    // Walls along y = 12 (x -10 .. 12) and x = 12 (y -10 .. 12) meet at the
    // corner (12, 12). Antenna (4, 8), receiver (8, 4), same height:
    // direct 4 sqrt(2) m; one bounce off y = 12 (image (4, 16)) and off
    // x = 12 (image (20, 8)), both sqrt(160) m; two bounces y = 12 then
    // x = 12 (image (20, 16), via (8, 12) and (12, 8)), sqrt(288) m. The
    // other order (x then y) would reflect at (16, 12), beyond the wall's
    // end: no ray.
    building::Building b = metre_building();
    b.floors[0].walls.push_back(wall("y12", "concrete", {{-10.0, 12.0}, {12.0, 12.0}}));
    b.floors[0].walls.push_back(wall("x12", "brick", {{12.0, -10.0}, {12.0, 12.0}}));
    b.floors[0].antennas.push_back(antenna("a", {4.0, 8.0}));
    const Point2 rx{8.0, 4.0};
    const double direct = 10.0 - fspl(4.0 * std::sqrt(2.0), 400e6);
    const double one = 10.0 - fspl(std::sqrt(160.0), 400e6) - 6.0;
    const double two = 10.0 - fspl(std::sqrt(288.0), 400e6) - 12.0;
    const std::size_t rays_per_order[] = {1, 3, 4};
    const double want[] = {direct, power_sum_dbm({direct, one, one}), power_sum_dbm({direct, one, one, two})};
    for (int order = 0; order <= 2; ++order) {
        CAPTURE(order);
        auto p = params(order);
        auto set = raytrace::trace_points(b, "f", {rx}, p);
        CHECK(set.rays[0].size() == rays_per_order[order]);
        for (const auto& r : set.rays[0]) CHECK(r.class_crossings.empty());
        CHECK(raytrace::evaluate(b, set, p).best_dbm[0] == doctest::Approx(want[order]).epsilon(1e-12));
        CHECK(level_at(b, "f", rx, p) == doctest::Approx(want[order]).epsilon(1e-12));
    }
    // Behind the corner (15, 4): the direct ray crosses the brick wall
    // (8 dB); no single bounce gets there; the bounce off y = 12 crosses
    // x = 12 on its second leg.
    auto set = raytrace::trace_points(b, "f", {{15.0, 4.0}}, params(1));
    double mw = std::pow(10.0, (10.0 - fspl(std::hypot(11.0, 4.0), 400e6) - 8.0) / 10.0);
    // Image (4, 16) to (15, 4): reflection at x = 4 + 11 * 4 / 12 = 7.67 on
    // y = 12 (inside the wall), the second leg crosses x = 12.
    mw += std::pow(10.0, (10.0 - fspl(std::hypot(11.0, 12.0), 400e6) - 6.0 - 8.0) / 10.0);
    CHECK(set.rays[0].size() == 2);
    CHECK(raytrace::evaluate(b, set, params(1)).best_dbm[0] == doctest::Approx(10.0 * std::log10(mw)).epsilon(1e-12));
}

TEST_CASE("reflection count limits: a corridor of two parallel walls (IND-40)") {
    building::Building b = metre_building();
    b.floors[0].walls.push_back(wall("n", "concrete", {{-100.0, 12.0}, {100.0, 12.0}}));
    b.floors[0].walls.push_back(wall("s", "concrete", {{-100.0, 8.0}, {100.0, 8.0}}));
    b.floors[0].antennas.push_back(antenna("a", {0.0, 10.0}));
    // Image distances to (10, 10): order k images lie 4k m off the axis.
    const double d[] = {10.0, std::sqrt(100.0 + 16.0), std::sqrt(100.0 + 64.0), std::sqrt(100.0 + 144.0)};
    double mw = 0.0;
    double previous = -1e9;
    for (int order = 0; order <= raytrace::kMaxReflections; ++order) {
        CAPTURE(order);
        auto p = params(order);
        auto set = raytrace::trace_points(b, "f", {{10.0, 10.0}}, p);
        REQUIRE(set.rays[0].size() == static_cast<size_t>(1 + 2 * order));
        int highest = 0;
        for (const auto& r : set.rays[0]) {
            highest = std::max(highest, r.reflections);
            CHECK(r.length_m == doctest::Approx(d[r.reflections]));
            CHECK(r.class_crossings.empty());
        }
        CHECK(highest == order);
        // Hand-calculated sum: two image rays per order, 6 dB per bounce.
        mw += (order == 0 ? 1.0 : 2.0) * std::pow(10.0, (10.0 - fspl(d[order], 400e6) - 6.0 * order) / 10.0);
        const double level = raytrace::evaluate(b, set, p).best_dbm[0];
        CHECK(level == doctest::Approx(10.0 * std::log10(mw)).epsilon(1e-12));
        CHECK(level > previous);
        previous = level;
    }
    CHECK_THROWS_AS(raytrace::compute_map(b, "f", params(raytrace::kMaxReflections + 1)), std::invalid_argument);
    CHECK(ModelParams{}.max_reflections == 2);  // IND-40 default
    CHECK(ModelParams{}.grid_m == 0.25);        // IND-40 default
}

TEST_CASE("each cell at its own carrier; antennas of one cell summed in power (IND-40)") {
    building::Building b = metre_building();
    b.cells.push_back(test::tetra_cell("hi", 800'000'000));
    b.floors[0].walls.push_back(wall("w", "concrete", {{5.0, -5.0}, {5.0, 25.0}}));
    b.floors[0].antennas.push_back(antenna("a1", {0.5, 0.5}));
    b.floors[0].antennas.push_back(antenna("a2", {20.5, 0.5}));
    b.floors[0].antennas.push_back(antenna("b", {0.5, 0.5}, 10.0, 1.5, "hi"));
    auto m = raytrace::compute_map(b, "f", params());
    REQUIRE(m.groups.size() == 2);
    CHECK(m.groups[0].antenna_ids == std::vector<std::string>{"a1", "a2"});
    CHECK(m.groups[1].frequency_hz == 800e6);
    const std::size_t at = m.grid.index(10, 0);  // (10.5, 0.5)
    CHECK(m.level_dbm[0][at] ==
          doctest::Approx(power_sum_dbm({10.0 - kFspl10m400 - 12.0, 10.0 - kFspl10m400})).epsilon(1e-12));
    // Concrete at 800 MHz: 12 + log(2) / log(2.25) * 11 dB.
    CHECK(m.level_dbm[1][at] ==
          doctest::Approx(10.0 - fspl(10.0, 800e6) - (12.0 + std::log(2.0) / std::log(2.25) * 11.0)).epsilon(1e-12));
    CHECK(m.best_group[at] == 0);
    // An antenna without a cell is its own group; without a carrier and
    // without ModelParams::frequency_hz the map is refused.
    b.floors[0].antennas.push_back(antenna("x", {3.0, 3.0}, 10.0, 1.5, ""));
    CHECK_THROWS_AS(raytrace::compute_map(b, "f", params()), std::invalid_argument);
    auto p = params();
    p.frequency_hz = 400e6;
    auto m2 = raytrace::compute_map(b, "f", p);
    REQUIRE(m2.groups.size() == 3);
    CHECK(m2.groups[2].key == "antenna:x");
    CHECK(m2.groups[1].frequency_hz == 400e6);
}

TEST_CASE("inter-floor term (IND-44): dB per floor, building slabs, alignment (IND-09), heights, on/off") {
    building::Building b = metre_building();
    b.floors.push_back(metre_floor("u", 1));
    b.floors[0].height_m = 3.0;
    b.floors[1].height_m = 3.0;
    // The upper floor's plan is shifted by (5, 0) m: its reference point
    // makes plan (15.5, 0.5) the building point (10.5, 0.5).
    b.floors[1].reference_point = Point2{5.0, 0.0};
    b.floors[1].antennas.push_back(antenna("up", {15.5, 0.5}));
    // A wall on the upper floor does not count on the ground floor.
    b.floors[1].walls.push_back(wall("w", "metal", {{10.0, -5.0}, {10.0, 25.0}}));
    const double d = std::sqrt(100.0 + 9.0);  // 10 m across, 3 m up
    auto p = params();
    CHECK(level_at(b, "f", {0.5, 0.5}, p) == doctest::Approx(10.0 - fspl(d, 400e6) - 15.0).epsilon(1e-12));
    b.floor_attenuation_db = 18.0;  // the editable dB per floor
    CHECK(level_at(b, "f", {0.5, 0.5}, p) == doctest::Approx(10.0 - fspl(d, 400e6) - 18.0).epsilon(1e-12));
    b.floors[0].slab_attenuation_db = 9.0;  // the ground floor's own slab
    CHECK(level_at(b, "f", {0.5, 0.5}, p) == doctest::Approx(10.0 - fspl(d, 400e6) - 9.0).epsilon(1e-12));
    p.floor_loss_db = 11.0;  // fitted value
    CHECK(level_at(b, "f", {0.5, 0.5}, p) == doctest::Approx(10.0 - fspl(d, 400e6) - 11.0).epsilon(1e-12));
    // Seen from the upper floor, the ground-floor antenna is 3 m below.
    b.floors[0].antennas.push_back(antenna("down", {0.5, 0.5}));
    auto pu = params();
    pu.other_floors = true;
    auto set = raytrace::trace_points(b, "u", {{15.5, 0.5}}, pu);
    REQUIRE(set.antennas.size() == 2);
    CHECK(set.antennas[0] == "down");  // floors by level
    REQUIRE(!set.rays[0].empty());
    CHECK(set.rays[0][0].length_m == doctest::Approx(d));
    CHECK(set.rays[0][0].slabs == 1);
    CHECK(set.rays[0][0].slab_db == 9.0);
    CHECK(set.rays[0][0].class_crossings.size() == 1);  // the upper floor's metal wall
    p.other_floors = false;
    b.floors[0].antennas.clear();
    auto m = raytrace::compute_map(b, "f", p);
    CHECK(m.groups.empty());
    CHECK(m.best_dbm[0] == raytrace::kNoLevel);
    CHECK(m.best_group[0] == -1);
    // An antenna on a floor without scale is skipped and named.
    p.other_floors = true;
    b.floors[1].scale = {};
    auto m2 = raytrace::compute_map(b, "f", p);
    REQUIRE(m2.skipped.size() == 1);
    CHECK(m2.skipped[0].find("up") == 0);
    // Outdoor donor directions are not traced (listed).
    b.floors[0].antennas.push_back(antenna("donor", {1.0, 1.0}));
    b.floors[0].antennas.back().type = building::AntennaType::OutdoorDonor;
    auto m3 = raytrace::compute_map(b, "f", p);
    CHECK(m3.skipped.size() == 2);
    CHECK(m3.antennas_used.empty());
}

TEST_CASE("pattern: a sector antenna loses the hand-calculated pattern loss towards the first leg (IND-12, IND-40)") {
    building::Building b = metre_building();
    auto a = antenna("a", {10.0, 10.0});
    a.pattern.type = building::AntennaPattern::Type::Sector;
    a.pattern.azimuth_deg = 90.0;  // towards +x
    a.pattern.beamwidth_deg = 60.0;
    a.pattern.front_to_back_db = 25.0;
    b.floors[0].antennas.push_back(a);
    // Towards +x: main beam. Towards -y (bearing 0): 90 degrees off,
    // 12 (90 / 60)^2 = 27 dB, capped at 25.  At bearing 120: 30 degrees off,
    // 12 (30 / 60)^2 = 3 dB.
    CHECK(level_at(b, "f", {15.0, 10.0}, params()) == doctest::Approx(10.0 - fspl(5.0, 400e6)).epsilon(1e-12));
    CHECK(level_at(b, "f", {10.0, 5.0}, params()) == doctest::Approx(10.0 - fspl(5.0, 400e6) - 25.0).epsilon(1e-12));
    const double r = 5.0;
    const Point2 at120{10.0 + r * std::sin(120.0 * std::numbers::pi / 180.0),
                       10.0 - r * std::cos(120.0 * std::numbers::pi / 180.0)};
    CHECK(level_at(b, "f", at120, params()) == doctest::Approx(10.0 - fspl(5.0, 400e6) - 3.0).epsilon(1e-9));
}

TEST_CASE("grid levels equal the traced points evaluated, also for changed parameters (IND-40, IND-41)") {
    building::Building b = test::office_building();
    b.cells.push_back(test::tetra_cell("cell-2", 390'100'000));
    b.floors[0].antennas[3].eirp_dbm.reset();  // gets default_eirp_dbm
    b.floors[0].antennas[3].cell = "cell-2";
    auto p = params(2);
    p.grid_m = 2.0;
    auto m = raytrace::compute_map(b, "office", p);
    REQUIRE(m.groups.size() == 2);
    CHECK(m.groups[1].antenna_ids == std::vector<std::string>{"office_das4"});
    CHECK(m.default_eirp == std::vector<std::string>{"office_das4"});
    std::vector<Point2> pts;
    std::vector<size_t> cells;
    for (int r = 0; r < m.grid.rows; r += 3)
        for (int c = 0; c < m.grid.cols; c += 2) {
            pts.push_back(m.grid.centre(c, r));
            cells.push_back(m.grid.index(c, r));
        }
    auto set = raytrace::trace_points(b, "office", pts, p);
    auto lv = raytrace::evaluate(b, set, p);
    size_t reflected = 0;
    for (const auto& rays : set.rays)
        for (const auto& r : rays) reflected += r.reflections > 0;
    CHECK(reflected > pts.size());
    for (size_t i = 0; i < pts.size(); ++i)
        for (size_t g = 0; g < 2; ++g) CHECK(std::fabs(lv.level_dbm[g][i] - m.level_dbm[g][cells[i]]) < 1e-9);

    // The fit's parameters (IND-41): evaluate() on the same rays equals a
    // new map with those parameters.
    auto q = p;
    q.path_loss_exponent = 2.6;
    q.reflection_loss_db = 9.0;
    q.wall_class_loss_db = {{"brick", 5.0}, {"plasterboard", 4.5}};
    q.antenna_eirp_dbm = {{"office_das1", 13.0}, {"office_das4", 7.0}};
    q.default_eirp_dbm = 0.0;
    auto m2 = raytrace::compute_map(b, "office", q);
    auto lv2 = raytrace::evaluate(b, set, q);
    for (size_t i = 0; i < pts.size(); ++i) {
        CHECK(std::fabs(lv2.best_dbm[i] - m2.best_dbm[cells[i]]) < 1e-9);
        CHECK(lv2.best_group[i] == m2.best_group[cells[i]]);
    }
}

TEST_CASE("SIMD and scalar paths and any thread count give the same map (TST-36)") {
    CHECK(raytrace::simd_built() == static_cast<bool>(YANTRA_TEST_EXPECT_SIMD));
    building::Building b = test::office_building();
    auto p = params(2);
    p.grid_m = 1.0;
    raytrace::RunControl scalar;
    scalar.isa = raytrace::Isa::Scalar;
    scalar.threads = 1;
    raytrace::RunControl simd;
    simd.isa = raytrace::Isa::Simd;
    simd.threads = 5;
    auto a = raytrace::compute_map(b, "office", p, scalar);
    auto c = raytrace::compute_map(b, "office", p, simd);
    CHECK(a.rays == c.rays);
    CHECK(a.best_dbm == c.best_dbm);  // bit for bit
    raytrace::RunControl scalar8 = scalar;
    scalar8.threads = 8;
    CHECK(raytrace::compute_map(b, "office", p, scalar8).best_dbm == a.best_dbm);
}

TEST_CASE("the office map is the same in every build tree, with and without SIMD (TST-36)") {
    // A digest of the office map (1 m grid, 2 reflections) to 1e-6 dB: the
    // default tree (SIMD), the scalar tree (YANTRA_SIMD=OFF) and the
    // sanitizer trees run this same check. FP contraction is off in the
    // ray tracer's files, so the trees compute the same doubles.
    const building::Building b = test::office_building();
    auto p = params(2);
    p.grid_m = 1.0;
    const auto m = raytrace::compute_map(b, "office", p);
    std::int64_t digest = 0;
    for (std::size_t i = 0; i < m.best_dbm.size(); ++i)
        digest += static_cast<std::int64_t>(i % 7 + 1) * std::llround(m.best_dbm[i] * 1e6);
    MESSAGE("office map digest " << digest << ", " << m.rays << " rays");
    CHECK(m.rays == kOfficeRays);
    CHECK(digest == kOfficeDigest);
}

TEST_CASE("the wall-crossing kernels follow the walls_crossed rules on random walls (IND-40, IND-11)") {
    // Random walls and points: with no reflections and exponent 2, the
    // loss above free space is exactly the walls_crossed() sum.
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(0.0, 20.0);
    for (int round = 0; round < 5; ++round) {
        building::Building b = metre_building();
        auto& f = b.floors[0];
        for (int k = 0; k < 40; ++k)
            f.walls.push_back(wall("", k % 2 ? "brick" : "glass", {{u(rng), u(rng)}, {u(rng), u(rng)}, {u(rng), u(rng)}}));
        // Grid-aligned walls and vertices (touching and vertex cases).
        f.walls.push_back(wall("", "metal", {{4.0, 0.0}, {4.0, 4.5}, {8.0, 4.5}}));
        f.walls.push_back(wall("", "door", {{4.0, 4.5}, {4.0, 9.0}}));
        f.antennas.push_back(antenna("a", {u(rng), u(rng)}));
        for (raytrace::Isa isa : {raytrace::Isa::Scalar, raytrace::Isa::Simd}) {
            raytrace::RunControl run;
            run.isa = isa;
            auto m = raytrace::compute_map(b, "f", params(), run);
            const Point2 a = f.antennas[0].position;
            for (int r = 0; r < m.grid.rows; ++r)
                for (int c = 0; c < m.grid.cols; ++c) {
                    const Point2 x = m.grid.centre(c, r);
                    const double d = std::hypot(x.x - a.x, x.y - a.y);
                    const double walls = building::walls_crossed(b, f, a, x, 400.0)->total_db;
                    const double want = 10.0 - fspl(std::max(d, raytrace::kMinDistanceM), 400e6) - walls;
                    REQUIRE(std::fabs(m.best_dbm[m.grid.index(c, r)] - want) < 1e-9);
                }
        }
    }
}

TEST_CASE("progress, cancellation and parameter checks (IND-45 hooks)") {
    building::Building b = test::office_building();
    auto p = params(1);
    std::vector<double> seen;
    raytrace::RunControl run;
    run.threads = 3;
    run.progress = [&](double f) { seen.push_back(f); };
    auto m = raytrace::compute_map(b, "office", p, run);
    CHECK_FALSE(m.cancelled);
    REQUIRE(!seen.empty());
    CHECK(seen.back() == 1.0);
    for (size_t i = 1; i < seen.size(); ++i) CHECK(seen[i] >= seen[i - 1]);
    CHECK(seen.size() <= 202);

    // Cancelled before the start, and from the progress callback.
    std::atomic<bool> stop{true};
    run.cancel = &stop;
    run.progress = nullptr;
    CHECK(raytrace::compute_map(b, "office", p, run).cancelled);
    CHECK(raytrace::trace_points(b, "office", {{1.0, 1.0}, {2.0, 2.0}}, p, run).cancelled);
    stop = false;
    run.threads = 1;
    p.grid_m = 0.25;
    run.progress = [&](double) { stop = true; };
    CHECK(raytrace::compute_map(b, "office", p, run).cancelled);

    ModelParams bad;
    CHECK(bad.validate().empty());
    bad.frequency_hz = 0.0;
    auto problems = bad.validate();
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("frequency_hz") == 0);
    bad.frequency_hz = 400e6;
    bad.grid_m = 0.0;
    bad.path_loss_exponent = 0.5;
    bad.wall_class_loss_db["glass"] = -1.0;
    CHECK(bad.validate().size() == 3);
    CHECK_THROWS_AS(raytrace::compute_map(b, "office", bad), std::invalid_argument);
    CHECK_THROWS_AS(raytrace::compute_map(b, "nowhere", params()), std::invalid_argument);
    b.floors[0].walls[0].wall_class = "steel";
    CHECK_THROWS_AS(raytrace::compute_map(b, "office", params()), std::invalid_argument);
    b.floors[0].walls[0].wall_class = "concrete";
    b.floors[0].scale = {};
    CHECK_THROWS_AS(raytrace::compute_map(b, "office", params()), std::logic_error);
}

TEST_CASE("floor grid: the plan rectangle in plan-frame metres, else the walls and antennas plus 1 m (IND-40)") {
    building::Building b;
    building::Floor f = metre_floor();
    f.scale = test::factor_scale(0.5);
    f.reference_point = Point2{4.0, 2.0};  // the grid stays in the plan frame
    auto g = raytrace::floor_grid(b, f, 0.25);
    CHECK(g.origin_m == Point2{0.0, 0.0});
    CHECK(g.cols == 40);
    CHECK(g.rows == 40);
    CHECK(g.cell_at({0.0, 0.0}) == std::optional<size_t>{0});
    CHECK(g.cell_at({9.99, 9.99}) == std::optional<size_t>{g.size() - 1});
    CHECK_FALSE(g.cell_at({10.0, 0.0}).has_value());
    f.plan_width = 0.0;
    f.walls.push_back(wall("", "brick", {{4.0, 2.0}, {14.0, 2.0}}));
    f.antennas.push_back(antenna("a", {4.0, 12.0}));
    auto h = raytrace::floor_grid(b, f, 0.5);
    CHECK(h.origin_m == Point2{1.0, 0.0});
    CHECK(h.cols == 14);
    CHECK(h.rows == 14);
    CHECK_THROWS_AS(raytrace::floor_grid(b, f, 0.01), std::invalid_argument);
    CHECK_THROWS_AS(raytrace::floor_grid(b, metre_floor(), 0.05 * 0.999), std::invalid_argument);
    f.scale = {};
    CHECK_THROWS_AS(raytrace::floor_grid(b, f, 0.5), std::logic_error);
}
