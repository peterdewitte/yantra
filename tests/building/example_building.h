// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small example building for the tests (IND-04..12, RAD-36): an office
// of two floors, 20 m x 10 m, drawn at 20 plan units per metre (0.05 m per
// unit), with walls of several classes, indoor antennas, an outdoor donor
// direction and two cells (one without a radio identity). Made-up data;
// no bundled or licence-restricted plan.
#pragma once

#include <yantra/core/building/model.h>

namespace yantra::tests {

inline building::Building example_office() {
    using namespace yantra::building;
    Building b;
    b.name = "Example office";
    b.floor_attenuation_db = 15.0;
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
    f0.slab_attenuation_db = 20.0;  // the slab between f0 and f1
    // Concrete along x = 10 m, a brick L, a glass wall with its own loss.
    f0.walls.push_back(Wall{"w1", "concrete", {{200, 0}, {200, 200}}, std::nullopt});
    f0.walls.push_back(Wall{"w2", "brick", {{300, 0}, {300, 100}, {400, 100}}, std::nullopt});
    f0.walls.push_back(Wall{"w3", "glass", {{360, 100}, {360, 200}}, 7.0});
    Antenna a1;
    a1.id = "a1";
    a1.name = "Hall";
    a1.position = {60, 100};
    a1.height_m = 2.8;
    a1.eirp_dbm = 10.0;
    a1.cell = "T1";
    f0.antennas.push_back(a1);
    Antenna a2;
    a2.id = "a2";
    a2.name = "Meeting room";
    a2.model = "panel";
    a2.position = {250, 50};
    a2.height_m = 2.5;
    a2.gain_dbi = 6.0;
    a2.pattern = AntennaPattern{AntennaPattern::Type::Sector, 90.0, 65.0, 20.0, 0.0, {}};
    a2.feed_power_dbm = 12.0;
    a2.cable_loss_db = 1.5;
    a2.splitter_loss_db = 3.5;
    a2.cell = "T1";
    f0.antennas.push_back(a2);
    Antenna donor;
    donor.id = "donor";
    donor.type = AntennaType::OutdoorDonor;
    donor.model = "Yagi";
    donor.position = {390, 10};
    donor.height_m = 12.0;
    donor.gain_dbi = 9.0;
    donor.pattern = AntennaPattern{AntennaPattern::Type::Sector, 45.0, 40.0, 25.0, 0.0, {}};
    donor.cell = "T1";
    donor.direction_deg = 45.0;
    donor.elevation_deg = 2.0;
    f0.antennas.push_back(donor);
    Antenna a4;
    a4.id = "a4";
    a4.position = {100, 150};
    a4.eirp_dbm = 5.0;
    a4.cell = "X9";
    f0.antennas.push_back(a4);
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
    f1.slab_attenuation_db = 18.0;
    f1.walls.push_back(Wall{"w1", "plasterboard", {{100, 0}, {100, 200}}, std::nullopt});
    Antenna a3;
    a3.id = "a3";
    a3.name = "Upstairs";
    a3.position = {110, 100};
    a3.height_m = 2.6;
    a3.eirp_dbm = 12.0;
    a3.cell = "T1";
    a3.pattern.type = AntennaPattern::Type::Table;
    a3.pattern.azimuth_deg = 180.0;
    a3.pattern.step_deg = 90.0;
    a3.pattern.gain_db = {0.0, -6.0, -15.0, -6.0};
    f1.antennas.push_back(a3);
    b.floors.push_back(f1);
    return b;
}

}  // namespace yantra::tests
