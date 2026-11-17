// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Editing the building model (core/building/edit.h): scale measurements
// and "not to scale" (IND-08), alignment between floors (IND-09). The
// wall, wall-class and antenna edits of 0.3 (IND-11, IND-12) are removed
// with their requirements.

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
