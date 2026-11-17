// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator plugin (yantra-radios RAD-61) with its positions from
// Yantra's floors (PLG-27, IND-08): the RadioHost answers get_floor_plan,
// get_scene and get_cursor from a BuildingContext whose scene is a
// checked-in test scene (tests/fixture_scene.hpp; Yantra makes no scenes
// of its own, DEMO-05), and the simulated records sit at the cursor
// converted with the floor scale. The floor uses the example id
// "fraunces-1" only to select the simulator's bundled Fraunces scenario
// (cell T1, 390 MHz); no plan image is involved. Waits are on observable
// conditions with generous limits.
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <yantra/core/building/model.h>
#include <yantra/core/context/building_context.h>
#include <yantra/core/measure/pipeline.h>
#include <yantra/core/plugin/state_store.h>
#include <yantra/core/radio/radio_host.h>
#include <yantra/radio.h>

#include "fixture_scene.hpp"

namespace fs = std::filesystem;
namespace yb = yantra::building;
namespace yc = yantra::context;
namespace ym = yantra::measure;
namespace yr = yantra::radio;

namespace {

const std::chrono::milliseconds kWait{60'000 * YANTRA_TEST_TIME_SCALE};
constexpr std::uint64_t kT1Hz = 390'000'000;

// A 20 m x 10 m floor at 0.05 m per plan unit with T1's antenna at
// (3, 5) m; `wall` adds a concrete wall along x = 8 m.
yb::Building building() {
    yb::Building b;
    b.name = "Simulated office";
    b.cells.push_back(yb::Cell{"T1", "Indoor DAS", "tetra", "tetra_cell", kT1Hz,
                               {{"mcc", 901}, {"mnc", 1}, {"la", 100}, {"colour_code", 1}}});
    yb::Floor f;
    f.id = "sim-ground";
    f.name = "Ground floor";
    f.example_id = "fraunces-1";
    f.plan_width = 400;
    f.plan_height = 200;
    f.scale.method = yb::Scale::Method::Factor;
    f.scale.metres_per_unit = 0.05;
    b.floors.push_back(f);
    return b;
}

struct Fields {
    std::size_t frequency = 0, level = 0, x = 0, y = 0, floor = 0;
    bool found = false;
};

Fields fields_of(const ym::RadioRecord& r) {
    Fields f;
    if (!r.field_names) return f;
    int n = 0;
    for (std::size_t i = 0; i < r.field_names->size(); ++i) {
        const std::string& name = (*r.field_names)[i];
        if (name == YR_FIELD_FREQUENCY_HZ) f.frequency = i, ++n;
        if (name == YR_FIELD_LEVEL) f.level = i, ++n;
        if (name == YR_FIELD_SIM_X_M) f.x = i, ++n;
        if (name == YR_FIELD_SIM_Y_M) f.y = i, ++n;
        if (name == YR_FIELD_SIM_FLOOR_ID) f.floor = i, ++n;
    }
    f.found = n == 5;
    return f;
}

// A T1 record with a simulated position: level (dBFS), x, y, floor.
struct Sample {
    double level = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string floor;
};

std::optional<Sample> sample(const ym::RadioRecord& r) {
    if (r.schema_kind != YR_SCHEMA_MEASUREMENT) return std::nullopt;
    const Fields f = fields_of(r);
    if (!f.found) return std::nullopt;
    const auto& v = r.record.values;
    const auto* hz = std::get_if<std::uint64_t>(&v[f.frequency]);
    const auto* level = std::get_if<double>(&v[f.level]);
    const auto* x = std::get_if<double>(&v[f.x]);
    const auto* y = std::get_if<double>(&v[f.y]);
    const auto* floor = std::get_if<std::string>(&v[f.floor]);
    if (!hz || *hz != kT1Hz || !level || !x || !y || !floor) return std::nullopt;
    return Sample{*level, *x, *y, *floor};
}

// The simulator's "level_unit" item dbfs (yantra-radios RAD-61).
constexpr std::int64_t kLevelUnitDbfs = 1;

struct Sim {
    std::shared_ptr<ym::RecordQueue> queue = std::make_shared<ym::RecordQueue>();
    std::shared_ptr<yantra::plugin::MemoryStateStore> state = std::make_shared<yantra::plugin::MemoryStateStore>();
    std::shared_ptr<yc::ContextPublisher> publisher = std::make_shared<yc::ContextPublisher>();
    yc::BuildingContext context;
    std::unique_ptr<yr::RadioHost> host;
    std::shared_ptr<yr::RadioDevice> device;
    std::string id;
    std::size_t seen = 0;  // records already looked at

    Sim() : context(publisher, yantra::test::fixture_scene("building/office_wall.scene.json")) {}

    bool start() {
        const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
        if (!fs::exists(path)) return false;
        yr::RadioHostConfig c;
        c.state = state;
        c.pipeline = queue;
        c.context = publisher;
        host = std::make_unique<yr::RadioHost>(c);
        const yr::RadioPluginEntry e = host->load_file(path);
        INFO(e.message);
        REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
        REQUIRE((e.capabilities & YR_CAP_SCENE) != 0);
        id = e.id;
        const yr::EnumerateResult en = host->enumerate(e.id);
        REQUIRE(en.status == YR_OK);
        REQUIRE(!en.devices.empty());
        const std::string key = en.devices.front().key;
        state->set_device_setting(e.id, key, "record_rate_hz", 10.0);
        state->set_device_setting(e.id, key, "playback_speed", 10.0);
        // dBFS levels: the
        // simulator's "Levels" setting dBFS (RAD-61; its default is dBm).
        state->set_device_setting(e.id, key, "level_unit", yantra::plugin::EnumValue{kLevelUnitDbfs});
        std::string error;
        device = host->open(e.id, key, error);
        REQUIRE_MESSAGE(device, error);
        yr::Plan plan;
        plan.type = YR_PLAN_SERVING;
        plan.serving_hz = kT1Hz;
        REQUIRE(device->start(plan) == YR_OK);
        return true;
    }

    // The next `n` T1 samples after those already seen that satisfy `pred`.
    template <class Pred>
    std::vector<Sample> next(std::size_t n, Pred pred) {
        std::vector<Sample> out;
        const auto until = std::chrono::steady_clock::now() + kWait;
        while (out.size() < n && std::chrono::steady_clock::now() < until) {
            queue->wait_records(seen + 1, std::chrono::milliseconds(500));
            const auto recs = queue->records();
            for (; seen < recs.size() && out.size() < n; ++seen)
                if (auto s = sample(recs[seen]); s && pred(*s)) out.push_back(*s);
        }
        return out;
    }

    ~Sim() {
        if (device) {
            device->stop();
            device->close();
        }
    }
};

}  // namespace

TEST_CASE("the simulator places its records at the cursor converted with the floor scale (PLG-27)") {
    Sim sim;
    sim.context.set_building(building());
    sim.context.show_floor("sim-ground");
    const yb::Point2 mouse{241.0, 77.0};  // plan units
    sim.context.set_cursor(mouse);
    if (!sim.start()) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    const auto at = sim.next(3, [](const Sample&) { return true; });
    REQUIRE(at.size() == 3);
    for (const Sample& s : at) {
        CHECK(s.floor == "sim-ground");
        CHECK(s.x == mouse.x * 0.05);  // exactly the converted position (TST-50)
        CHECK(s.y == mouse.y * 0.05);
    }
    // It asked for the floor plan and the scene.
    const auto calls = sim.host->context_calls(sim.id);
    CHECK(calls.count("get_floor_plan:OK") == 1);
    CHECK(calls.count("get_scene:OK") == 1);
    CHECK(calls.count("get_cursor:OK") == 1);
}
