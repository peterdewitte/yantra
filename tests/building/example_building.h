// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small example building for the tests (IND-04..09): an office of two
// floors, 20 m x 10 m, drawn at 20 plan units per metre (0.05 m per unit),
// with two cells (one without a radio identity). Made-up data; no bundled
// or licence-restricted plan.
#pragma once

#include <yantra/core/building/model.h>

namespace yantra::tests {

inline building::Building example_office() {
    using namespace yantra::building;
    Building b;
    b.name = "Example office";
    b.cells.push_back(Cell{"T1", "Indoor DAS", "tetra", "tetra_cell", 390'000'000,
                           {{"mcc", 901}, {"mnc", 1}, {"la", 100}, {"colour_code", 1}}});
    b.cells.push_back(Cell{"X9", "Unknown cell", "", "", 0, {}});
    b.sources.push_back(PlanSource{"plan-1", "office.png", std::string(64, 'a'), true});

    Floor f0;
    f0.id = "f0";
    f0.name = "Ground floor";
    f0.level = 0;
    f0.height_m = 3.2;
    f0.source = "plan-1";
    f0.page = 0;
    f0.trim = Trim{Trim::Shape::Rectangle, {{10, 10}, {410, 10}, {410, 210}, {10, 210}}, 0, 0.0};
    f0.plan_width = 400;
    f0.plan_height = 200;
    f0.scale.method = Scale::Method::TwoPoints;
    f0.scale.a = {0, 0};
    f0.scale.b = {400, 0};
    f0.scale.distance_m = 20.0;
    f0.reference_point = Point2{0, 0};
    b.floors.push_back(f0);

    Floor f1;
    f1.id = "f1";
    f1.name = "First floor";
    f1.level = 1;
    f1.source = "plan-1";
    f1.page = 0;
    f1.trim = Trim{Trim::Shape::Polygon, {{10, 220}, {410, 220}, {410, 420}, {10, 420}, {5, 300}}, 0, 0.5};
    f1.plan_width = 400;
    f1.plan_height = 200;
    f1.scale.method = Scale::Method::Shared;
    f1.scale.shared_floor = "f0";
    // The same building point is 10 plan units further right on this floor.
    f1.reference_point = Point2{10, 0};
    b.floors.push_back(f1);
    return b;
}

}  // namespace yantra::tests
