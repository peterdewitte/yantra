// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building model (IND-04, IND-06..12, IND-44 data): wall-class table at
// the measured frequency, scale methods and frame conversions, alignment,
// patterns and EIRP, floor order, validation messages, and the wall
// geometry used by the propagation model (IND-40). Geometry cases copied
// from yantra-v2 68ea20a: tests/building/test_building.cpp, adapted.

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

#include <yantra/core/building/geometry.h>
#include <yantra/core/building/model.h>

#include "example_building.h"

using namespace yantra::building;
using yantra::tests::example_office;

namespace {

bool has_error(const Building& b, const std::string& text) {
    for (const auto& e : b.validate())
        if (e == text) return true;
    return false;
}

}  // namespace

TEST_CASE("the example building is valid") {
    const Building b = example_office();
    CHECK(b.validate().empty());
    CHECK(Building{}.validate().empty());
}

TEST_CASE("default wall classes and their attenuation per band (IND-11)") {
    const auto classes = default_wall_classes();
    REQUIRE(classes.size() == 8);
    const Building b;
    const WallClass* concrete = b.find_class("concrete");
    REQUIRE(concrete);
    CHECK(concrete->attenuation_at_db(400.0) == 12.0);
    CHECK(concrete->attenuation_at_db(900.0) == 23.0);
    // Below and above the table: the end values; no frequency: the first band.
    CHECK(concrete->attenuation_at_db(380.0) == 12.0);
    CHECK(concrete->attenuation_at_db(2400.0) == 23.0);
    CHECK(concrete->attenuation_at_db(0.0) == 12.0);
    // Linear in log(f): the geometric mean of the band ends lies half way.
    CHECK(concrete->attenuation_at_db(600.0) == doctest::Approx(17.5));
    for (const WallClass& c : classes) {
        INFO(c.id);
        CHECK(!c.name.empty());
        CHECK(c.bands.size() == 2);
        CHECK(c.attenuation_at_db(400.0) >= 0.0);
        CHECK(c.attenuation_at_db(900.0) >= c.attenuation_at_db(400.0));
    }
    // An edited table with three bands.
    WallClass w{"x", "X", {{100.0, 1.0}, {1000.0, 3.0}, {10000.0, 4.0}}, 6.0};
    CHECK(w.attenuation_at_db(std::sqrt(1000.0 * 10000.0)) == doctest::Approx(3.5));
    CHECK(WallClass{}.attenuation_at_db(400.0) == 0.0);
    // A wall's own attenuation holds at every frequency.
    const Wall glass{"g", "glass", {{0, 0}, {1, 0}}, 7.0};
    CHECK(*b.wall_attenuation_db(glass, 400.0) == 7.0);
    CHECK(*b.wall_attenuation_db(glass, 900.0) == 7.0);
    CHECK(!b.wall_attenuation_db(Wall{"s", "steel", {{0, 0}, {1, 0}}, std::nullopt}, 400.0));
}

TEST_CASE("scale per floor: two points, scale bar, factor, shared, none (IND-08)") {
    Scale s;
    CHECK(!own_metres_per_unit(s));
    s.method = Scale::Method::TwoPoints;
    s.a = {10, 10};
    s.b = {13, 14};  // 5 units
    s.distance_m = 2.5;
    CHECK(*own_metres_per_unit(s) == doctest::Approx(0.5));
    s.method = Scale::Method::ScaleBar;
    CHECK(*own_metres_per_unit(s) == doctest::Approx(0.5));
    s.b = s.a;
    CHECK(!own_metres_per_unit(s));
    s.b = {13, 14};
    s.distance_m = 0.0;
    CHECK(!own_metres_per_unit(s));
    s.distance_m = std::numeric_limits<double>::quiet_NaN();
    CHECK(!own_metres_per_unit(s));
    Scale f;
    f.method = Scale::Method::Factor;
    f.metres_per_unit = 0.02;
    CHECK(*own_metres_per_unit(f) == 0.02);
    f.metres_per_unit = -1.0;
    CHECK(!own_metres_per_unit(f));

    Building b = example_office();
    const Floor& f0 = *b.find_floor("f0");
    const Floor& f1 = *b.find_floor("f1");
    CHECK(*b.metres_per_unit(f0) == doctest::Approx(0.05));
    CHECK(*b.metres_per_unit(f1) == *b.metres_per_unit(f0));  // shared (one sheet)
    CHECK(b.has_scale(f1));
    b.find_floor("f0")->scale = Scale{};
    CHECK(!b.has_scale(*b.find_floor("f0")));
    CHECK(!b.has_scale(*b.find_floor("f1")));  // its shared floor has none
    CHECK(!b.to_plan_metres(*b.find_floor("f0"), {1, 1}));
    CHECK(!b.plan_size_m(*b.find_floor("f0")));
    CHECK(b.validate().empty());  // a floor without scale is allowed
}

TEST_CASE("plan units, plan-frame metres and building metres (IND-08, IND-09)") {
    const Building b = example_office();
    const Floor& f0 = *b.find_floor("f0");
    const Floor& f1 = *b.find_floor("f1");
    // Plan frame: plan units x metres per unit, exactly.
    const Point2 m = *b.to_plan_metres(f0, {123.0, 45.0});
    CHECK(m.x == 123.0 * 0.05);
    CHECK(m.y == 45.0 * 0.05);
    const Point2 back = *b.from_plan_metres(f0, m);
    CHECK(back.x == doctest::Approx(123.0));
    CHECK(back.y == doctest::Approx(45.0));
    const Point2 size = *b.plan_size_m(f0);
    CHECK(size.x == doctest::Approx(20.0));
    CHECK(size.y == doctest::Approx(10.0));
    // Building frame: minus the reference point; the same building point
    // on both floors (f1's reference is 10 units to the right).
    const Point2 on0 = *b.to_building_metres(f0, {100, 100});
    const Point2 on1 = *b.to_building_metres(f1, {110, 100});
    CHECK(on0.x == doctest::Approx(on1.x));
    CHECK(on0.y == doctest::Approx(on1.y));
    const Point2 p1 = *b.from_building_metres(f1, on0);
    CHECK(p1.x == doctest::Approx(110.0));
    CHECK(p1.y == doctest::Approx(100.0));
}

TEST_CASE("floors ordered by level, lookups and plan names (IND-04)") {
    Building b = example_office();
    Floor basement;
    basement.id = "b1";
    basement.name = "Basement";
    basement.level = -1;
    b.floors.push_back(basement);
    const auto order = b.floors_by_level();
    REQUIRE(order.size() == 3);
    CHECK(b.floors[order[0]].id == "b1");
    CHECK(b.floors[order[1]].id == "f0");
    CHECK(b.floors[order[2]].id == "f1");
    CHECK(b.floor_at_level(1)->id == "f1");
    CHECK(b.floor_at_level(7) == nullptr);
    const auto [floor, antenna] = b.find_antenna("a3");
    REQUIRE(floor);
    CHECK(floor->id == "f1");
    CHECK(antenna->name == "Upstairs");
    CHECK(b.find_antenna("none").first == nullptr);
    CHECK(plan_name(b, *b.find_floor("f0")) == "Example office, Ground floor");
    Building unnamed;
    CHECK(plan_name(unnamed, basement) == "Basement");
    CHECK(b.validate().empty());
}

TEST_CASE("slabs between floors (IND-44)") {
    const Building b = example_office();
    CHECK(b.inter_floor_attenuation_db(0, 0) == 0.0);
    CHECK(b.inter_floor_attenuation_db(0, 1) == 20.0);  // f0's slab
    CHECK(b.inter_floor_attenuation_db(1, 0) == 20.0);
    CHECK(b.inter_floor_attenuation_db(1, 2) == 18.0);  // f1's slab
    CHECK(b.inter_floor_attenuation_db(-2, 0) == 30.0); // missing levels: the default
}

TEST_CASE("antenna patterns, bearings and EIRP (IND-12)") {
    CHECK(bearing_deg({0, 0}, {0, -1}) == doctest::Approx(0.0));   // up
    CHECK(bearing_deg({0, 0}, {1, 0}) == doctest::Approx(90.0));   // right
    CHECK(bearing_deg({0, 0}, {0, 1}) == doctest::Approx(180.0));  // down
    CHECK(bearing_deg({0, 0}, {-1, 0}) == doctest::Approx(270.0));
    const AntennaPattern omni;
    CHECK(pattern_gain_db(omni, 123.0) == 0.0);
    const AntennaPattern sector{AntennaPattern::Type::Sector, 90.0, 60.0, 20.0, 0.0, {}};
    CHECK(pattern_gain_db(sector, 90.0) == 0.0);
    CHECK(pattern_gain_db(sector, 120.0) == doctest::Approx(-3.0));  // half the beamwidth
    CHECK(pattern_gain_db(sector, 60.0) == doctest::Approx(-3.0));
    CHECK(pattern_gain_db(sector, 270.0) == doctest::Approx(-20.0)); // capped
    const AntennaPattern table{AntennaPattern::Type::Table, 180.0, 0.0, 0.0, 90.0, {0.0, -6.0, -15.0, -6.0}};
    CHECK(pattern_gain_db(table, 180.0) == 0.0);
    CHECK(pattern_gain_db(table, 225.0) == doctest::Approx(-3.0));
    CHECK(pattern_gain_db(table, 0.0) == doctest::Approx(-15.0));
    CHECK(pattern_gain_db(table, 135.0) == doctest::Approx(-3.0));  // last entry back to the first
    const Building b = example_office();
    CHECK(*b.find_antenna("a1").second->effective_eirp_dbm() == 10.0);
    CHECK(*b.find_antenna("a2").second->effective_eirp_dbm() == doctest::Approx(12.0 - 1.5 - 3.5 + 6.0));
    Antenna unknown;
    CHECK(!unknown.effective_eirp_dbm());
}

TEST_CASE("validation names the place of every problem") {
    Building b = example_office();
    b.wall_classes.push_back(b.wall_classes.front());
    CHECK(has_error(b, "wall_classes[8]: duplicate wall class \"concrete\""));
    b = example_office();
    b.wall_classes[0].bands = {{900.0, 1.0}, {400.0, 2.0}};
    CHECK(has_error(b, "wall_classes[0].bands[1]: frequencies must be ascending"));
    b.wall_classes[0].bands.clear();
    CHECK(has_error(b, "wall_classes[0]: needs at least one band"));
    b = example_office();
    b.floors[0].walls[1].wall_class = "steel";
    CHECK(has_error(b, "floors[0].walls[1]: unknown wall class \"steel\""));
    b.floors[0].walls[1].wall_class.clear();
    CHECK(has_error(b, "floors[0].walls[1]: a wall without class needs its own attenuation_db"));
    b.floors[0].walls[1].attenuation_db = 5.0;
    CHECK(b.validate().empty());
    b.floors[0].walls[0].points.resize(1);
    CHECK(has_error(b, "floors[0].walls[0]: a wall needs at least 2 points"));
    b = example_office();
    b.floors[1].level = 0;
    CHECK(has_error(b, "floors[1]: duplicate level 0"));
    b = example_office();
    b.floors[1].antennas[0].id = "a1";
    CHECK(has_error(b, "floors[1].antennas[0]: duplicate antenna \"a1\""));
    b = example_office();
    b.floors[0].antennas[0].cell = "nope";
    CHECK(has_error(b, "floors[0].antennas[0]: unknown cell \"nope\""));
    b = example_office();
    b.floors[0].antennas[1].pattern.beamwidth_deg = 0.0;
    CHECK(has_error(b, "floors[0].antennas[1].pattern: beamwidth_deg must be in (0, 360]"));
    b = example_office();
    b.floors[1].antennas[0].pattern.step_deg = 120.0;
    CHECK(has_error(b, "floors[1].antennas[0].pattern: gain_db covers more than 360 degrees"));
    b = example_office();
    b.floors[1].scale.shared_floor = "f1";
    CHECK(has_error(b, "floors[1].scale: a floor cannot share its own scale"));
    b.floors[1].scale.shared_floor = "zz";
    CHECK(has_error(b, "floors[1].scale: unknown floor \"zz\""));
    b = example_office();
    b.floors[0].scale.distance_m = 0.0;
    CHECK(has_error(b, "floors[0].scale: needs two different finite points and a distance > 0 m"));
    b = example_office();
    b.floors[0].source = "plan-9";
    CHECK(has_error(b, "floors[0]: unknown source \"plan-9\""));
    b = example_office();
    b.floors[0].trim.outline.pop_back();
    CHECK(has_error(b, "floors[0].trim: a rectangle has 4 corners"));
    b = example_office();
    b.floors[0].trim.quarter_turns = 4;
    CHECK(has_error(b, "floors[0].trim: quarter_turns must be 0..3"));
    b = example_office();
    b.sources[0].sha256 = "ABC";
    CHECK(has_error(b, "sources[0]: sha256 must be 64 lower-case hex digits"));
    b.sources[0].sha256.clear();
    b.sources[0].embedded = false;
    CHECK(has_error(b, "sources[0]: a referenced (not embedded) source needs its sha256"));
    b = example_office();
    b.sources[0].file_name = "plans/office.png";
    CHECK(has_error(b, "sources[0]: file_name must not hold a folder"));
}

TEST_CASE("segment intersection known answers") {
    auto t = segment_intersection({0, 0}, {10, 0}, {4, -1}, {4, 1});
    REQUIRE(t);
    CHECK(*t == doctest::Approx(0.4));
    CHECK_FALSE(segment_intersection({0, 0}, {10, 0}, {4, 1}, {4, 2}));   // misses
    CHECK_FALSE(segment_intersection({0, 0}, {10, 0}, {0, 1}, {10, 1}));  // parallel
    CHECK_FALSE(segment_intersection({0, 0}, {10, 0}, {2, 0}, {6, 0}));   // collinear
    auto touch = segment_intersection({0, 0}, {10, 0}, {10, 0}, {10, 5});
    REQUIRE(touch);
    CHECK(*touch == doctest::Approx(1.0));
}

TEST_CASE("walls crossed by a path with their attenuation (IND-40 input)") {
    const Building b = example_office();
    const Floor& f = *b.find_floor("f0");
    // Walls in metres: concrete x = 10; brick L (15, 0)-(15, 5)-(20, 5);
    // glass x = 18 from y = 5 to 10 with 7 dB.
    SUBCASE("concrete and glass with its own loss, at 400 MHz") {
        const auto w = *walls_crossed(b, f, {3, 8}, {19, 8}, 400.0);
        REQUIRE(w.crossings.size() == 2);
        CHECK(w.crossings[0].wall == 0);
        CHECK(w.crossings[0].attenuation_db == 12.0);
        CHECK(w.crossings[0].point.x == doctest::Approx(10.0));
        CHECK(w.crossings[0].t == doctest::Approx(7.0 / 16.0));
        CHECK(w.crossings[1].wall == 2);
        CHECK(w.total_db == doctest::Approx(19.0));
        CHECK(walls_crossed(b, f, {3, 8}, {19, 8}, 900.0)->total_db == doctest::Approx(23.0 + 7.0));
    }
    SUBCASE("both segments of the brick L") {
        const auto w = *walls_crossed(b, f, {14, 2}, {17, 6}, 400.0);
        REQUIRE(w.crossings.size() == 2);
        CHECK(w.crossings[0].segment == 0);
        CHECK(w.crossings[1].segment == 1);
        CHECK(w.total_db == doctest::Approx(16.0));
    }
    SUBCASE("a path through the polyline vertex counts once") {
        const auto w = *walls_crossed(b, f, {14, 6}, {16, 4}, 400.0);  // through (15, 5)
        REQUIRE(w.crossings.size() == 1);
        CHECK(w.total_db == doctest::Approx(8.0));
    }
    SUBCASE("along a wall, or ending on it, is no crossing") {
        CHECK(walls_crossed(b, f, {10, 1}, {10, 9}, 400.0)->crossings.empty());
        CHECK(walls_crossed(b, f, {3, 5}, {10, 5}, 400.0)->crossings.empty());
        CHECK(walls_crossed(b, f, {10, 5}, {12, 5}, 400.0)->crossings.empty());
    }
    SUBCASE("direction does not matter") {
        const auto ab = *walls_crossed(b, f, {3, 8}, {19, 4}, 400.0);
        const auto ba = *walls_crossed(b, f, {19, 4}, {3, 8}, 400.0);
        CHECK(ab.crossings.size() == ba.crossings.size());
        CHECK(ab.total_db == doctest::Approx(ba.total_db));
    }
    SUBCASE("segments in metres; none without scale") {
        const auto segs = *wall_segments_m(b, f, 400.0);
        REQUIRE(segs.size() == 4);
        CHECK(segs[1].a.x == doctest::Approx(15.0));
        CHECK(segs[2].b.x == doctest::Approx(20.0));
        CHECK(segs[2].b.y == doctest::Approx(5.0));
        CHECK(segs[3].attenuation_db == 7.0);
        Building nos = b;
        nos.find_floor("f0")->scale = Scale{};
        CHECK(!wall_segments_m(nos, *nos.find_floor("f0"), 400.0));
        CHECK(!walls_crossed(nos, *nos.find_floor("f0"), {0, 0}, {1, 1}, 400.0));
    }
}
