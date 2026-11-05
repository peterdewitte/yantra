// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A typical office floor for the ray-tracer tests and benchmark (IND-40):
// 60 x 30 m, a 4 m corridor along the middle, 12 rooms of 5 m on each
// side, outer walls of concrete piers and glass, brick corridor walls with
// doors, plasterboard partitions (120 walls), and 4 ceiling DAS antennas
// in the corridor serving one cell at 390 MHz. An invented building.
// Copied from yantra-v2 68ea20a: tests/raytrace/office.h, adapted (scale,
// cells of the building model).
#pragma once

#include "yantra/core/building/model.h"

#include <string>

namespace yantra::test {

inline constexpr const char* kOfficeCell = "cell-1";

/// A Factor scale of `mpu` metres per plan unit.
inline building::Scale factor_scale(double mpu) {
    building::Scale s;
    s.method = building::Scale::Method::Factor;
    s.metres_per_unit = mpu;
    return s;
}

inline building::Cell tetra_cell(const std::string& id, std::uint64_t carrier_hz) {
    building::Cell c;
    c.id = id;
    c.label = "Cell " + id;
    c.technology_id = "tetra";
    c.schema_id = "tetra_cell";
    c.carrier_hz = carrier_hz;
    return c;
}

inline building::Floor office_floor(const std::string& id = "office", int level = 0) {
    using building::Point2;
    building::Floor f;
    f.id = id;
    f.name = "Office floor";
    f.level = level;
    f.height_m = 3.5;
    f.scale = factor_scale(0.05);  // 20 plan units per metre
    f.plan_width = 60.0 * 20;
    f.plan_height = 30.0 * 20;
    auto pu = [](double x, double y) { return Point2{x * 20.0, y * 20.0}; };
    int n = 0;
    auto wall = [&](const std::string& cls, Point2 a, Point2 b) {
        f.walls.push_back({id + "_w" + std::to_string(++n), cls, {a, b}, std::nullopt});
    };
    for (int r = 0; r < 12; ++r) {
        const double x = 5.0 * r;
        for (double y : {0.0, 30.0}) {
            wall("concrete", pu(x, y), pu(x + 2.0, y));
            wall("glass", pu(x + 2.0, y), pu(x + 5.0, y));
        }
        for (double y : {13.0, 17.0}) {
            wall("door", pu(x, y), pu(x + 1.0, y));
            wall("brick", pu(x + 1.0, y), pu(x + 5.0, y));
        }
        if (r > 0) {
            wall("plasterboard", pu(x, 0.0), pu(x, 13.0));
            wall("plasterboard", pu(x, 17.0), pu(x, 30.0));
        }
    }
    wall("concrete", pu(0.0, 0.0), pu(0.0, 30.0));
    wall("concrete", pu(60.0, 0.0), pu(60.0, 30.0));
    for (int k = 0; k < 4; ++k) {
        building::Antenna a;
        a.id = id + "_das" + std::to_string(k + 1);
        a.name = "DAS " + std::to_string(k + 1);
        a.position = pu(7.5 + 15.0 * k, 15.0);
        a.height_m = 2.7;
        a.eirp_dbm = 10.0;
        a.cell = kOfficeCell;
        f.antennas.push_back(a);
    }
    return f;
}

/// A building with one office floor and its cell.
inline building::Building office_building() {
    building::Building b;
    b.name = "Office";
    b.cells.push_back(tetra_cell(kOfficeCell, 390'000'000));
    b.floors.push_back(office_floor());
    return b;
}

}  // namespace yantra::test
