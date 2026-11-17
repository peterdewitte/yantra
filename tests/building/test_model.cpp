// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building model (IND-04, IND-06..09): scale methods and frame
// conversions, alignment, floor order and validation messages. The wall
// classes, walls, antennas, slabs and wall geometry of 0.3 are removed with their
// requirements (Yantra 0.4).

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

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
    CHECK(b.find_cell("T1")->label == "Indoor DAS");
    CHECK(b.find_cell("none") == nullptr);
    CHECK(plan_name(b, *b.find_floor("f0")) == "Example office, Ground floor");
    Building unnamed;
    CHECK(plan_name(unnamed, basement) == "Basement");
    CHECK(b.validate().empty());
}

TEST_CASE("validation names the place of every problem") {
    Building b = example_office();
    b.floors[1].level = 0;
    CHECK(has_error(b, "floors[1]: duplicate level 0"));
    b = example_office();
    b.cells.push_back(b.cells.front());
    CHECK(has_error(b, "cells[2]: duplicate cell \"T1\""));
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
