// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator plugin (yantra-radios RAD-61) with its scene and
// positions from Yantra's building model (RAD-36, PLG-27, IND-11/12,
// IND-08): the RadioHost answers get_floor_plan, get_scene and get_cursor
// from a BuildingContext, and the simulated levels follow the model. The
// floor uses the example id "fraunces-1" only to select the simulator's
// bundled Fraunces scenario (cell T1, 390 MHz, no reflections, no fading,
// no shadowing); no plan image is involved. Waits are on observable
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
#include <yantra/core/radio/scene_export.h>
#include <yantra/radio.h>

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
yb::Building building(bool wall) {
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
    yb::Antenna a;
    a.id = "a1";
    a.position = {60, 100};
    a.height_m = 2.8;
    a.eirp_dbm = 10.0;
    a.cell = "T1";
    f.antennas.push_back(a);
    if (wall) f.walls.push_back(yb::Wall{"w1", "concrete", {{160, 0}, {160, 200}}, std::nullopt});
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

    explicit Sim(const yantra::radio::SceneExportOptions& options = {}) : context(publisher, options) {}

    bool start() {
        const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator.so";
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
        // The levels these cases model are dBFS (kDbfsOffset): the
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

double mean_level(const std::vector<Sample>& s) {
    double sum = 0.0;
    for (const Sample& x : s) sum += x.level;
    return s.empty() ? 0.0 : sum / static_cast<double>(s.size());
}

// Free-space path loss at T1's carrier over a 3D distance (yantra-radios
// docs/CHANNEL_MODEL.md).
double fspl_db(double d_m) {
    return 20.0 * std::log10(4.0 * std::numbers::pi * d_m * static_cast<double>(kT1Hz) / 299'792'458.0);
}

// The Fraunces scenario's levels are dBFS = dBm + 20 (reference level
// -20 dBm, gain 0: the simulator's true_offset_db sensor).
constexpr double kDbfsOffset = 20.0;
// The receiver is 1.5 m above its floor (CHANNEL_MODEL.md).
constexpr double kRxHeightM = 1.5;

// The mean level of 10 T1 records at plan metres (x, y) after 10 more
// that let the filters settle (after a lock or a scene reload).
double settled_level_at(Sim& sim, double x, double y) {
    auto at = [&](const Sample& s) { return s.x == x && s.y == y; };
    sim.next(10, at);
    const auto got = sim.next(10, at);
    REQUIRE(got.size() == 10);
    return mean_level(got);
}

}  // namespace

TEST_CASE("the simulator places its records at the cursor converted with the floor scale (PLG-27)") {
    Sim sim;
    sim.context.set_building(building(false));
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
    // It asked for the floor plan and the scene of the building model.
    const auto calls = sim.host->context_calls(sim.id);
    CHECK(calls.count("get_floor_plan:OK") == 1);
    CHECK(calls.count("get_scene:OK") == 1);
    CHECK(calls.count("get_cursor:OK") == 1);
}

TEST_CASE("a wall added to the model lowers the simulated level by its attenuation (RAD-36, IND-11)") {
    Sim sim;
    sim.context.set_building(building(false));
    sim.context.show_floor("sim-ground");
    sim.context.set_cursor(yb::Point2{240.0, 100.0});  // (12, 5) m, 9 m from the antenna
    if (!sim.start()) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    auto at_cursor = [](const Sample& s) { return s.x == 12.0 && s.y == 5.0; };
    // Skip the first second of records (filters settling after the lock).
    sim.next(10, at_cursor);
    const auto open = sim.next(10, at_cursor);
    REQUIRE(open.size() == 10);
    const double before = mean_level(open);
    // Free space from the model's antenna (EIRP 10 dBm, 2.8 m high) to the
    // receiver 1.5 m above the floor (yantra-radios docs/CHANNEL_MODEL.md);
    // the Fraunces scenario's levels are dBFS = dBm + 20 (reference level
    // -20 dBm, gain 0: the simulator's true_offset_db sensor).
    const double d = std::hypot(9.0, 2.8 - 1.5);
    const double fspl = 20.0 * std::log10(4.0 * std::numbers::pi * d * static_cast<double>(kT1Hz) / 299'792'458.0);
    CHECK(before - 20.0 == doctest::Approx(10.0 - fspl).epsilon(0.5 / 35.0));

    // The concrete wall between antenna and position: SCENE_CHANGED.
    const double wall_db = *building(true).wall_attenuation_db(building(true).floors[0].walls[0], kT1Hz / 1e6);
    CHECK(wall_db == 12.0);  // the 400 MHz band of the default table
    CHECK(sim.context.set_building(building(true)) == yc::kSceneChanged);
    const auto dropped = sim.next(1, [&](const Sample& s) { return at_cursor(s) && s.level < before - wall_db / 2; });
    REQUIRE_MESSAGE(dropped.size() == 1, "the level did not follow the new wall");
    sim.next(10, at_cursor);  // settling after the reload
    const auto behind = sim.next(10, at_cursor);
    REQUIRE(behind.size() == 10);
    const double after = mean_level(behind);
    MESSAGE("level without the wall " << before << " dBFS, with it " << after << " dBFS");
    // The simulator's levels follow its multi-wall model within 0.5 dB
    // (yantra-radios RAD-94); a difference of two levels within 1 dB.
    CHECK(before - after == doctest::Approx(wall_db).epsilon(1.0 / wall_db));
    CHECK(sim.host->context_calls(sim.id)["get_scene:OK"] >= 2);
}

// Scene format 1.1 end to end (yantra-radios v0.2.0): the simulator's
// levels follow the model's per-floor slabs and heights, the class table
// at the simulated carrier, and a polyline crossed at a vertex once. Each
// within the simulator's 0.5 dB of its multi-wall model (RAD-94); the 1.0
// reading of the same scene would be off by 10 dB or more.

TEST_CASE("an antenna on the floor above reaches the receiver through that storey's own slab (IND-44, scene 1.1)") {
    yb::Building b = building(false);
    yb::Floor& g = b.floors[0];
    g.height_m = 3.6;
    g.reference_point = yb::Point2{0, 0};
    g.slab_attenuation_db = 25.0;  // the scene's uniform slab stays 15 dB (floor_attenuation_db)
    g.antennas.clear();
    yb::Floor up;
    up.id = "sim-first";
    up.name = "First floor";
    up.level = 1;
    up.plan_width = 400;
    up.plan_height = 200;
    up.scale.method = yb::Scale::Method::Shared;
    up.scale.shared_floor = "sim-ground";
    up.reference_point = yb::Point2{0, 0};
    yb::Antenna a;
    a.id = "a-up";
    a.position = {60, 100};  // (3, 5) m
    a.height_m = 2.8;
    a.eirp_dbm = 10.0;
    a.cell = "T1";
    up.antennas.push_back(a);
    b.floors.push_back(up);
    REQUIRE(b.validate().empty());

    Sim sim;
    sim.context.set_building(b);
    sim.context.show_floor("sim-ground");
    sim.context.set_cursor(yb::Point2{120.0, 100.0});  // (6, 5) m: 3 m beside the antenna
    if (!sim.start()) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    const double level = settled_level_at(sim, 6.0, 5.0);
    const double d = std::hypot(3.0, 3.6 + 2.8 - kRxHeightM);
    const double expected = 10.0 - fspl_db(d) - b.inter_floor_attenuation_db(0, 1) + kDbfsOffset;
    MESSAGE("level " << level << " dBFS, model " << expected << " dBFS");
    CHECK(b.inter_floor_attenuation_db(0, 1) == 25.0);
    CHECK(std::abs(level - expected) <= 0.5);
}

TEST_CASE("a wall of the class table attenuates at the simulated carrier, not at the scene frequency (IND-11, scene 1.1)") {
    // The scene frequency 900 MHz makes attenuation_db the concrete 900 MHz
    // value (23 dB, what a 1.0 reader takes); the bands give 12 dB at T1's
    // 390 MHz, which the simulator must use.
    yantra::radio::SceneExportOptions options;
    options.frequency_hz = 900e6;
    Sim sim(options);
    const yb::Building b = building(true);
    sim.context.set_building(b);
    sim.context.show_floor("sim-ground");
    sim.context.set_cursor(yb::Point2{240.0, 100.0});  // (12, 5) m, behind the wall at x = 8 m
    if (!sim.start()) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    const double at_carrier = *b.wall_attenuation_db(b.floors[0].walls[0], kT1Hz / 1e6);
    CHECK(at_carrier == 12.0);
    CHECK(*b.wall_attenuation_db(b.floors[0].walls[0], 900.0) == 23.0);
    const double level = settled_level_at(sim, 12.0, 5.0);
    const double expected = 10.0 - fspl_db(std::hypot(9.0, 2.8 - kRxHeightM)) - at_carrier + kDbfsOffset;
    MESSAGE("level " << level << " dBFS, model " << expected << " dBFS");
    CHECK(std::abs(level - expected) <= 0.5);
}

TEST_CASE("a path through a vertex of a wall polyline crosses it once; the edit reloads the scene (IND-11, scene 1.1)") {
    Sim sim;
    yb::Building b = building(false);
    sim.context.set_building(b);
    sim.context.show_floor("sim-ground");
    sim.context.set_cursor(yb::Point2{240.0, 100.0});  // (12, 5) m
    if (!sim.start()) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    const double free_space = 10.0 - fspl_db(std::hypot(9.0, 2.8 - kRxHeightM)) + kDbfsOffset;
    const double open = settled_level_at(sim, 12.0, 5.0);
    CHECK(std::abs(open - free_space) <= 0.5);

    // A concrete ">" whose vertex (8, 5) m lies on the path from the
    // antenna (3, 5) m to the receiver: two segments, one wall.
    b.floors[0].walls.push_back(yb::Wall{"v1", "concrete", {{120, 40}, {160, 100}, {120, 160}}, std::nullopt});
    const auto calls = sim.host->context_calls(sim.id)["get_scene:OK"];
    CHECK(sim.context.set_building(b) == yc::kSceneChanged);
    const double wall_db = *b.wall_attenuation_db(b.floors[0].walls[0], kT1Hz / 1e6);
    const auto dropped = sim.next(1, [&](const Sample& s) {
        return s.x == 12.0 && s.y == 5.0 && s.level < open - wall_db / 2;
    });
    REQUIRE_MESSAGE(dropped.size() == 1, "the level did not follow the new wall");
    CHECK(sim.host->context_calls(sim.id)["get_scene:OK"] > calls);  // the simulator reloaded
    const double behind = settled_level_at(sim, 12.0, 5.0);
    MESSAGE("level without the wall " << open << " dBFS, through its vertex " << behind << " dBFS");
    // Once (12 dB), not once per segment (24 dB).
    CHECK(std::abs(behind - (free_space - wall_db)) <= 0.5);
}
