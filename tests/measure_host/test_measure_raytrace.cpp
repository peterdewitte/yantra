// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TST-36: the ray tracer (IND-40) with no reflections against the
// simulator's multi-wall model (RAD-73) on the same drawn floor. The
// simulator's calibrated TETRA records at a point carry the level of that
// model (RAD-61), the walls it counted and the cell; the ray tracer gives
// the same level from Yantra's building model. And the fit (IND-41):
// the simulator's levels on a floor with other wall losses and EIRP than
// the plan's give those values back. Skips without the simulator.

#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include <yantra/core/building/model.h>
#include <yantra/core/context/building_context.h>
#include <yantra/core/raytrace/fit.h>
#include <yantra/core/raytrace/model.h>

#include "e2e_helpers.h"
#include "measure_rig.h"

using namespace mh;
namespace yb = yantra::building;
namespace rt = yantra::raytrace;

namespace {

// 20 x 10 m (0.05 m per unit) with a concrete wall at x = 8 m, a brick
// wall from (12, 0) to (16, 10) m and a glass pane at y = 8 m from x = 1 to
// 6 m; cell T1 at 390.0 MHz (the default scenario's D0 carrier, replaced
// by the plan's cell) from an antenna at (3, 5) m, 2.8 m high.
yb::Building office() {
    yb::Building b;
    b.name = "Simulated office";
    b.cells.push_back(yb::Cell{"T1", "Indoor DAS", "tetra", "tetra_cell", 390'000'000,
                               {{"mcc", 901}, {"mnc", 1}, {"la", 100}, {"colour_code", 1}}});
    yb::Floor f;
    f.id = "sim-ground";
    f.name = "Ground floor";
    f.plan_width = 400;
    f.plan_height = 200;
    f.scale.method = yb::Scale::Method::Factor;
    f.scale.metres_per_unit = 0.05;
    yb::Antenna a;
    a.id = "a1";
    a.position = {60, 100};
    a.height_m = 2.8;
    a.eirp_dbm = 10.0;
    a.cell = "T1";
    f.antennas.push_back(a);
    f.walls.push_back(yb::Wall{"w1", "concrete", {{160, 0}, {160, 200}}, std::nullopt});
    f.walls.push_back(yb::Wall{"w2", "brick", {{240, 0}, {320, 200}}, std::nullopt});
    f.walls.push_back(yb::Wall{"w3", "glass", {{20, 160}, {120, 160}}, std::nullopt});
    b.floors.push_back(f);
    return b;
}

}  // namespace

TEST_CASE("TST-36 ray tracer with no reflections gives the simulator's multi-wall level at drawn points (IND-40, RAD-73)") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator.so";
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    const yb::Building b = office();
    rt::ModelParams p;
    p.max_reflections = 0;  // the multi-wall model has none by default
    const std::vector<yb::Point2> points_m{{5.0, 5.0}, {7.0, 9.5}, {10.0, 5.0}, {12.0, 2.0}, {15.5, 4.0}, {19.0, 9.0}};
    const rt::PathSet set = rt::trace_points(b, "sim-ground", points_m, p);
    const rt::PointLevels want = rt::evaluate(b, set, p);

    Rig rig{true};
    TetraPlugin tetra{rig};
    yc::BuildingContext context(rig.publisher, yr::SceneExportOptions{});
    context.set_building(b);
    context.show_floor("sim-ground");
    context.set_cursor(yb::Point2{points_m[0].x * 20.0, points_m[0].y * 20.0});
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(!en.devices.empty());
    rig.state->set_device_setting(e.id, en.devices.front().key, "record_rate_hz", 10.0);
    rig.state->set_device_setting(e.id, en.devices.front().key, "gain_db", 0.0);
    std::string error;
    auto dev = rig.host->open(e.id, en.devices.front().key, error);
    REQUIRE_MESSAGE(dev, error);
    REQUIRE(dev->start(serving(390'000'000)) == YR_OK);
    // The simulator's true calibration offset at gain 0 (RAD-61).
    auto table = std::make_shared<ycal::Table>();
    table->put(flat_entry(*dev->calibration_key(), -20.0, true));
    rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table, ycal::AntennaSettings{}));
    for (std::size_t i = 0; i < points_m.size(); ++i) {
        const yb::Point2 m = points_m[i];
        CAPTURE(m.x);
        CAPTURE(m.y);
        int walls = 0;
        for (const auto& [cls, n] : set.rays[i].at(0).class_crossings) walls += n;
        std::size_t mark = 0;
        {
            const std::lock_guard lock(rig.out->m);
            mark = rig.out->records.size();
        }
        context.set_cursor(yb::Point2{m.x * 20.0, m.y * 20.0});
        // Mean calibrated level of 10 T1 records at the point, after 5 that
        // let the receiver settle (the first may hold part of the previous
        // point's interval).
        std::vector<ym::MappedRecord> got;
        REQUIRE(rig.out->wait([&] {
            got.clear();
            std::size_t seen = 0;
            for (std::size_t k = mark; k < rig.out->records.size(); ++k) {
                const ym::MappedRecord& r = rig.out->records[k];
                if (r.schema->id != "tetra_cell" || r.frequency_hz != 390'000'000 || num(r, "sim_x_m") != m.x ||
                    num(r, "sim_y_m") != m.y)
                    continue;
                if (++seen > 5) got.push_back(r);
            }
            return got.size() >= 10;
        }));
        double sum = 0.0;
        for (const auto& r : got) {
            CHECK(r.correction.calibrated);
            CHECK(text(r, "cell_id") == "T1");
            CHECK(num(r, "walls") == static_cast<double>(walls));
            sum += r.level;
        }
        const double sim_dbm = sum / static_cast<double>(got.size());
        MESSAGE("(" << m.x << ", " << m.y << ") m, " << walls << " walls: ray tracer " << want.best_dbm[i]
                    << " dBm, simulator " << sim_dbm << " dBm");
        // As the TST-10 wall cases: the decoded level within 0.5 dB.
        CHECK(std::fabs(want.best_dbm[i] - sim_dbm) <= 0.5);
    }
    dev->stop();
    dev->close();
}

TEST_CASE("TST-36 fit recovery: the fit gives back the simulator's wall losses and EIRP from its levels (IND-41, RAD-73)") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator.so";
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    // The truth the simulator gets: other wall losses than the default
    // table and 16 dBm instead of 10. The fit sees the plan with the
    // default table and no EIRP; the exponent stays at 2 (the multi-wall
    // model's free space).
    yb::Building truth = office();
    for (auto& c : truth.wall_classes)
        for (auto& band : c.bands) {
            if (c.id == "concrete") band.attenuation_db = 15.0;
            if (c.id == "brick") band.attenuation_db = 6.0;
            if (c.id == "glass") band.attenuation_db = 3.5;
        }
    truth.floors[0].antennas[0].eirp_dbm = 16.0;
    yb::Building plan = office();
    plan.floors[0].antennas[0].eirp_dbm.reset();
    // Points with no wall, behind the glass, behind the concrete, behind
    // the concrete and the brick.
    const std::vector<yb::Point2> points_m{{5.0, 5.0}, {6.0, 3.0}, {2.0, 2.0}, {1.0, 5.0},  {2.0, 9.5},
                                           {4.0, 9.5}, {5.0, 9.25}, {10.0, 5.0}, {11.0, 2.0}, {9.0, 8.0},
                                           {15.5, 4.0}, {19.0, 9.0}, {18.0, 2.0}};

    Rig rig{true};
    TetraPlugin tetra{rig};
    yc::BuildingContext context(rig.publisher, yr::SceneExportOptions{});
    context.set_building(truth);
    context.show_floor("sim-ground");
    context.set_cursor(yb::Point2{points_m[0].x * 20.0, points_m[0].y * 20.0});
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(!en.devices.empty());
    rig.state->set_device_setting(e.id, en.devices.front().key, "record_rate_hz", 10.0);
    rig.state->set_device_setting(e.id, en.devices.front().key, "gain_db", 0.0);
    std::string error;
    auto dev = rig.host->open(e.id, en.devices.front().key, error);
    REQUIRE_MESSAGE(dev, error);
    REQUIRE(dev->start(serving(390'000'000)) == YR_OK);
    auto table = std::make_shared<ycal::Table>();
    table->put(flat_entry(*dev->calibration_key(), -20.0, true));
    rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table, ycal::AntennaSettings{}));
    std::vector<double> measured;
    for (const yb::Point2 m : points_m) {
        CAPTURE(m.x);
        CAPTURE(m.y);
        std::size_t mark = 0;
        {
            const std::lock_guard lock(rig.out->m);
            mark = rig.out->records.size();
        }
        context.set_cursor(yb::Point2{m.x * 20.0, m.y * 20.0});
        // Mean calibrated level of 10 T1 records at the point, after 5.
        std::vector<ym::MappedRecord> got;
        REQUIRE(rig.out->wait([&] {
            got.clear();
            std::size_t seen = 0;
            for (std::size_t k = mark; k < rig.out->records.size(); ++k) {
                const ym::MappedRecord& r = rig.out->records[k];
                if (r.schema->id != "tetra_cell" || r.frequency_hz != 390'000'000 || num(r, "sim_x_m") != m.x ||
                    num(r, "sim_y_m") != m.y)
                    continue;
                if (++seen > 5) got.push_back(r);
            }
            return got.size() >= 10;
        }));
        double sum = 0.0;
        for (const auto& r : got) sum += r.level;
        measured.push_back(sum / static_cast<double>(got.size()));
    }
    dev->stop();
    dev->close();

    rt::ModelParams p;
    p.max_reflections = 0;
    rt::FitOptions o;
    o.fit_exponent = false;
    const rt::FitResult r = rt::fit_model(plan, rt::trace_points(plan, "sim-ground", points_m, p), measured, p, o);
    INFO(r.stop_reason);
    MESSAGE("fit: EIRP " << r.params.antenna_eirp_dbm.at("a1") << " dBm, concrete "
                         << r.params.wall_class_loss_db.at("concrete") << " dB, brick "
                         << r.params.wall_class_loss_db.at("brick") << " dB, glass "
                         << r.params.wall_class_loss_db.at("glass") << " dB, rms " << r.quality.rms_db << " dB");
    CHECK(r.stop_reason.rfind("converged", 0) == 0);
    CHECK(r.quality.points == points_m.size());
    CHECK(r.quality.start_rms_db > 0.5);  // the default wall table does not fit
    // Stated tolerances (the simulator's levels are means of 10 records,
    // within 0.5 dB of its model, TST-10): EIRP 0.5 dB, wall losses 1 dB,
    // rms 0.5 dB.
    CHECK(std::fabs(r.params.antenna_eirp_dbm.at("a1") - 16.0) <= 0.5);
    CHECK(std::fabs(r.params.wall_class_loss_db.at("concrete") - 15.0) <= 1.0);
    CHECK(std::fabs(r.params.wall_class_loss_db.at("brick") - 6.0) <= 1.0);
    CHECK(std::fabs(r.params.wall_class_loss_db.at("glass") - 3.5) <= 1.0);
    CHECK(r.quality.rms_db <= 0.5);
}
