// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Editing the building model (core/building/edit.h): scale measurements
// and "not to scale" (IND-08), alignment between floors (IND-09), wall
// selection and vertex edits, the wall-class table (IND-11), antenna
// patterns and aiming (IND-12).

#include <doctest/doctest.h>

#include <cmath>

#include <yantra/core/building/edit.h>

#include "example_building.h"

using namespace yantra::building;
using yantra::tests::example_office;

TEST_CASE("edit: scales from two points, a scale bar or a factor; the measurement gives the distance back (IND-08)") {
    const auto s = two_point_scale({10, 20}, {13, 24}, 2.5);  // 5 units
    REQUIRE(s);
    CHECK(s->method == Scale::Method::TwoPoints);
    CHECK(*own_metres_per_unit(*s) == doctest::Approx(0.5));
    const auto bar = scale_bar({0, 0}, {0, 200}, 10.0);
    REQUIRE(bar);
    CHECK(bar->method == Scale::Method::ScaleBar);
    CHECK(*own_metres_per_unit(*bar) == doctest::Approx(0.05));
    CHECK(*own_metres_per_unit(*factor_scale(0.02)) == 0.02);
    CHECK_FALSE(factor_scale(0.0));
    CHECK_FALSE(factor_scale(-1.0));
    CHECK_FALSE(scale_bar({0, 0}, {0, 0}, 10.0));
    CHECK_FALSE(two_point_scale({0, 0}, {1, 0}, INFINITY));
    CHECK_FALSE(two_point_scale({0, NAN}, {1, 0}, 1.0));
    CHECK(shared_scale("f0").shared_floor == "f0");

    Building b = example_office();
    const Floor& f0 = *b.find_floor("f0");
    CHECK(*distance_m(b, f0, {0, 0}, {30, 40}) == doctest::Approx(2.5));
    CHECK(*length_m(b, f0, {{0, 0}, {20, 0}, {20, 20}}) == doctest::Approx(2.0));
    CHECK(*length_m(b, f0, {}) == 0.0);
    // A clockwise and an anticlockwise square have the same area.
    CHECK(*area_m2(b, f0, {{0, 0}, {20, 0}, {20, 20}, {0, 20}}) == doctest::Approx(1.0));
    CHECK(*area_m2(b, f0, {{0, 0}, {0, 20}, {20, 20}, {20, 0}}) == doctest::Approx(1.0));
    Floor& f1 = *b.find_floor("f1");
    f1.scale = Scale{};
    CHECK_FALSE(distance_m(b, f1, {0, 0}, {1, 1}));
    CHECK(format_length(length_m(b, f1, {{0, 0}, {1, 1}})) == "not to scale");
    CHECK(format_area(area_m2(b, f1, {{0, 0}, {1, 0}, {1, 1}})) == "not to scale");
    CHECK(format_length(1234.5678) == "1234.57 m");
    CHECK(format_area(0.04) == "0.0 m²");
}

TEST_CASE("edit: floors of one sheet share a scale; the sharing floors are listed (IND-08)") {
    Building b = example_office();
    const Floor& f0 = *b.find_floor("f0");
    Floor& f1 = *b.find_floor("f1");
    CHECK(same_sheet(f0, f1));  // the example's floors are trims of one sheet
    f1.page = 1;
    CHECK_FALSE(same_sheet(f0, f1));
    f1.page = f0.page;
    Floor none = f1;
    none.source.clear();
    CHECK_FALSE(same_sheet(none, none));  // no sheet: nothing to share
    f1.scale = shared_scale("f0");
    CHECK(floors_sharing_scale(b, "f0") == std::vector<std::string>{"f1"});
    CHECK(floors_sharing_scale(b, "f1").empty());
}

TEST_CASE("edit: a point maps to the same building point on another aligned floor (IND-09)") {
    Building b = example_office();
    Floor& f0 = *b.find_floor("f0");
    Floor& f1 = *b.find_floor("f1");
    f0.scale = *factor_scale(0.05);
    f1.scale = *factor_scale(0.1);
    f0.reference_point = Point2{20, 20};
    f1.reference_point = Point2{5, 15};
    REQUIRE(aligned(b, f0));
    const auto p = point_on_floor(b, f0, {120, 60}, f1);  // (5, 2) m from the corner
    REQUIRE(p);
    CHECK(p->x == doctest::Approx(55.0));
    CHECK(p->y == doctest::Approx(35.0));
    CHECK(*point_on_floor(b, f0, {120, 60}, f0) == Point2{120, 60});
    f1.reference_point.reset();
    CHECK_FALSE(aligned(b, f1));
    CHECK_FALSE(point_on_floor(b, f0, {120, 60}, f1));
    f1.reference_point = Point2{5, 15};
    f1.scale = Scale{};
    CHECK_FALSE(point_on_floor(b, f0, {120, 60}, f1));
}

TEST_CASE("edit: wall selection, vertex edits, split, move (IND-11)") {
    Floor f;
    f.walls.push_back(Wall{"a", "brick", {{0, 0}, {100, 0}}, std::nullopt});
    f.walls.push_back(Wall{"b", "glass", {{50, -50}, {50, 50}, {80, 50}}, std::nullopt});
    // Nearest segment within the tolerance; the later wall wins a tie.
    const auto hit = nearest_wall(f, {50, 0}, 1.0);
    REQUIRE(hit);
    CHECK(hit->wall == 1);
    CHECK(hit->distance == 0.0);
    const auto h2 = nearest_wall(f, {20, 3}, 5.0);
    REQUIRE(h2);
    CHECK(h2->wall == 0);
    CHECK(h2->t == doctest::Approx(0.2));
    CHECK(h2->nearest == Point2{20, 0});
    CHECK(h2->distance == doctest::Approx(3.0));
    const auto h3 = nearest_wall(f, {70, 52}, 5.0);
    REQUIRE(h3);
    CHECK(h3->segment == 1);
    CHECK_FALSE(nearest_wall(f, {20, 30}, 5.0));
    CHECK(nearest_vertex(f.walls[1], {51, 49}, 3.0) == std::size_t{1});
    CHECK_FALSE(nearest_vertex(f.walls[1], {65, 0}, 3.0));

    Wall w = f.walls[1];
    CHECK(insert_vertex(w, 3, {80, 80}));  // append
    CHECK(insert_vertex(w, 0, {50, -60}));
    CHECK_FALSE(insert_vertex(w, 9, {0, 0}));
    CHECK(w.points.size() == 5);
    CHECK(move_vertex(w, 4, {90, 90}));
    CHECK_FALSE(move_vertex(w, 5, {0, 0}));
    CHECK(remove_vertex(w, 0));
    CHECK(w.points == std::vector<Point2>{{50, -50}, {50, 50}, {80, 50}, {90, 90}});
    Wall two = f.walls[0];
    CHECK_FALSE(remove_vertex(two, 0));  // a wall keeps 2 points
    CHECK_FALSE(remove_vertex(two, 7));
    CHECK(wall_length_units(f.walls[1]) == doctest::Approx(130.0));

    const auto parts = split_wall(f.walls[1], 1, "b2");
    REQUIRE(parts);
    CHECK(parts->first == Wall{"b", "glass", {{50, -50}, {50, 50}}, std::nullopt});
    CHECK(parts->second == Wall{"b2", "glass", {{50, 50}, {80, 50}}, std::nullopt});
    CHECK_FALSE(split_wall(f.walls[1], 0));
    CHECK_FALSE(split_wall(f.walls[1], 2));
    CHECK_FALSE(split_wall(f.walls[0], 1));
    move_wall(two, {5, -5});
    CHECK(two.points == std::vector<Point2>{{5, -5}, {105, -5}});
}

TEST_CASE("edit: the wall-class table: add, replace, count users, reset the defaults (IND-11)") {
    Building b = example_office();
    std::vector<WallClass>& t = b.wall_classes;
    const std::size_t n = t.size();
    put_wall_class(t, WallClass{"steel", "Steel", {{400, 30}}, std::nullopt});
    CHECK(t.size() == n + 1);
    CHECK(t.back().id == "steel");
    put_wall_class(t, WallClass{"brick", "Brick (thick)", {{400, 15}, {900, 18}}, 3.0});
    CHECK(t.size() == n + 1);
    CHECK(b.find_class("brick")->name == "Brick (thick)");
    CHECK(walls_of_class(b, "concrete") >= 1);
    CHECK(walls_of_class(b, "steel") == 0);
    t.erase(t.begin());  // concrete removed
    reset_default_wall_classes(t);
    CHECK(*b.find_class("brick") == default_wall_classes()[1]);
    CHECK(b.find_class("steel") != nullptr);
    CHECK(t.back().id == "concrete");
    CHECK(b.validate().empty());
}

TEST_CASE("edit: directional patterns and aiming of antennas and donor directions (IND-12)") {
    const AntennaPattern p = directional_pattern(370.0, 90.0, 25.0);
    CHECK(p.type == AntennaPattern::Type::Sector);
    CHECK(p.azimuth_deg == doctest::Approx(10.0));
    CHECK(p.beamwidth_deg == 90.0);
    CHECK(p.front_to_back_db == 25.0);
    CHECK(directional_pattern(-90.0).azimuth_deg == 270.0);
    Antenna a;
    a.position = {10, 10};
    CHECK(aim_antenna(a, {20, 10}));
    CHECK(a.pattern.azimuth_deg == 0.0);  // omni: unchanged
    CHECK_FALSE(a.direction_deg);
    a.pattern = directional_pattern(0.0);
    CHECK(aim_antenna(a, {10, 20}));  // down the plan: 180 degrees
    CHECK(a.pattern.azimuth_deg == doctest::Approx(180.0));
    CHECK_FALSE(aim_antenna(a, {10, 10}));
    CHECK_FALSE(aim_antenna(a, {NAN, 0}));
    a.type = AntennaType::OutdoorDonor;
    CHECK(aim_antenna(a, {0, 10}));
    CHECK(*a.direction_deg == doctest::Approx(270.0));
    CHECK(a.pattern.azimuth_deg == doctest::Approx(270.0));
    Floor f;
    f.antennas = {a, a};
    f.antennas[1].position = {12, 10};
    CHECK(nearest_antenna(f, {11.5, 10}, 1.0) == std::size_t{1});
    CHECK_FALSE(nearest_antenna(f, {30, 30}, 1.0));
}
