// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator plugin (RAD-61, RAD-94) through the test host's emulated
// context callbacks (RAD-18, RAD-91): levels against the multi-wall model
// (RAD-73) at each record's sim_position, a cursor move followed within
// 1 s, scene reload, the scenario of each example id, the fallback without
// plan or position, ground truth (identity, neighbours, C1), seeds,
// pacing, the practice calibration, a slow sink (RAD-24, RAD-25),
// pause/resume, and every cell of each bundled scenario measured with a
// given or the strongest serving cell (RAD-102, RAD-112).
#include <doctest/doctest.h>

#include <yantra/gen/multiwall.hpp>
#include <yantra/gen/scenario.hpp>
#include <yantra/radios/tetra_schemas.hpp>

#include "host.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace yantra;
using namespace yantra::radio_host;
using radios::Record;

namespace {

constexpr uint64_t kT1Hz = 390'000'000; // fraunces T1: band 3, carrier 3600
constexpr uint64_t kM1Hz = 390'200'000; // fraunces M1: carrier 3608

// A plan of the Fraunces example (RAD-18 example id "fraunces-1"): T1's
// antenna and two drywalls; M1 has no antenna (its scenario level).
std::string fraunces_scene(double eirp_dbm = 10.0)
{
    return R"({"format": "yantra-radio-scene", "version": 1,
  "floor": {"id": "fr-ground", "name": "Ground floor", "scale_set": true,
            "width_m": 20.0, "height_m": 10.0},
  "wall_classes": [{"id": "drywall", "label": "Drywall", "attenuation_db": 3.0}],
  "walls": [{"x1_m": 8.0, "y1_m": 0.0, "x2_m": 8.0, "y2_m": 10.0, "class": "drywall",
             "attenuation_db": 3.0},
            {"x1_m": 13.0, "y1_m": 0.0, "x2_m": 13.0, "y2_m": 10.0, "class": "drywall",
             "attenuation_db": 3.0}],
  "cells": [{"id": "T1", "label": "Indoor DAS", "mcc": 901, "mnc": 1, "la": 100,
             "colour_code": 1, "carrier_hz": 390000000}],
  "antennas": [{"id": "a1", "cell": "T1", "x_m": 3.0, "y_m": 5.0, "height_m": 2.8,
                "eirp_dbm": )" +
           std::to_string(eirp_dbm) + R"(, "pattern": {"type": "omni"}}]})";
}

gen::Scenario fraunces()
{
    return gen::load_scenario_file(std::string(YANTRA_SOURCE_DIR) + "/data/scenarios/fraunces.json");
}

// The multi-wall formula (RAD-73) for T1 at (x, y), as the scenario sets it up.
double formula_t1(const std::string& scene_json, double x, double y)
{
    const gen::Scenario s = fraunces();
    radios::Scene scene;
    std::string error;
    REQUIRE(radios::parse_scene(scene_json, scene, error));
    gen::MultiWallParams p = s.channel.multiwall;
    REQUIRE(p.shadowing_sigma_db == 0.0); // deterministic: no shadowing field
    p.receiver_height_m = s.receiver.height_m;
    const gen::MultiWallModel model(p);
    return model.cell(gen::apply_overrides(s, scene), "T1", gen::PlanPoint{x, y}).level_dbm;
}

struct Sim {
    std::unique_ptr<Host> host;
    yr_device* device = nullptr;

    explicit Sim(const ContextOptions& ctx = {})
    {
        HostConfig cfg;
        cfg.context = ctx;
        std::string error;
        host = Host::load(SIMULATOR_PLUGIN, cfg, error);
        REQUIRE_MESSAGE(host != nullptr, error);
        REQUIRE(host->descriptor_problems().empty());
        REQUIRE(host->descriptor().stream->open(host->plugin(), "simulator:0", &device) == YR_OK);
    }
    ~Sim()
    {
        if (device != nullptr)
            host->descriptor().stream->close(device);
    }
    int field(const char* name) const { return host->header().schemas[0].field_index(name); }
    const radios::Value& v(const Record& r, const char* name) const
    {
        const int i = field(name);
        REQUIRE(i >= 0);
        return r.values[static_cast<std::size_t>(i)];
    }
    void set(const std::string& key, const Value& value)
    {
        REQUIRE_MESSAGE(write_value(*host, device, key, value) == YR_OK, host->last_error(device));
    }
    void set_double(const std::string& key, double x)
    {
        Value v;
        v.type = YR_TYPE_DOUBLE;
        v.f64 = x;
        set(key, v);
    }
    void set_uint(const std::string& key, uint64_t x)
    {
        Value v;
        v.type = YR_TYPE_UINT;
        v.u64 = x;
        set(key, v);
    }
    void set_bool(const std::string& key, bool on)
    {
        Value v;
        v.type = YR_TYPE_BOOL;
        v.i64 = on ? 1 : 0;
        set(key, v);
    }
    // An enum setting by item name.
    void set_enum(const std::string& key, const std::string& item)
    {
        std::vector<SettingDef> defs;
        REQUIRE(list_settings(*host, device, false, defs) == YR_OK);
        for (const SettingDef& d : defs)
            if (d.key == key)
                for (const radios::EnumItem& it : d.items)
                    if (it.name == item) {
                        Value v;
                        v.type = YR_TYPE_ENUM;
                        v.i64 = it.value;
                        set(key, v);
                        return;
                    }
        FAIL("no item " << item << " in " << key);
    }
    Value sensor(const std::string& key)
    {
        Value v;
        REQUIRE(read_value(*host, device, key, true, v) == YR_OK);
        return v;
    }
    double offset() { return sensor("true_offset_db").f64; }

    // Streams with a serving plan until `done` says so (or `seconds` times
    // YANTRA_TEST_TIME_SCALE pass).
    // `serving_hz` 0: the strongest carrier.
    StreamResult stream(const std::function<bool(const Record&)>& on_record, double seconds,
                        const std::function<void(const radios::Event&)>& on_event = {},
                        const SinkOptions& sink = {}, uint64_t serving_hz = 0)
    {
        yr_plan plan{};
        plan.struct_size = sizeof plan;
        plan.type = YR_PLAN_SERVING;
        plan.serving_hz = serving_hz;
        std::atomic<bool> stop{false};
        StreamLimits limits;
        // A hang guard only (the sanitizer trees run 4-13x slower).
        limits.max_seconds = seconds * YANTRA_TEST_TIME_SCALE;
        limits.interrupt = &stop;
        return run_stream(
            *host, device, plan, limits,
            [&](const Record& r) {
                if (on_record(r))
                    stop = true;
            },
            on_event, sink);
    }
    bool is_cell(const Record& r, uint64_t hz) const
    {
        return r.schema_index == 0 && r.values[YR_CORE_FREQUENCY_HZ].u64 == hz;
    }
    bool has_position(const Record& r) const { return (r.present & YR_GROUP_SIM_POSITION) != 0; }
    double x(const Record& r) const { return v(r, YR_FIELD_SIM_X_M).f64; }
    double y(const Record& r) const { return v(r, YR_FIELD_SIM_Y_M).f64; }
};

ContextOptions fraunces_plan(std::vector<PlanPoint> cursor = {}, uint32_t step = 0,
                             const std::string& scene = fraunces_scene())
{
    ContextOptions c;
    c.plan = true;
    c.plan_name = "Fraunces test";
    c.example_id = "fraunces-1";
    c.scene_json = {scene};
    c.cursor = std::move(cursor);
    c.path_step_records = step;
    return c;
}

} // namespace

TEST_CASE("simulator: each example id selects its scenario (RAD-61, RAD-74)")
{
    struct Case {
        bool plan;
        std::string example;
        std::string scenario;
    };
    std::vector<Case> cases = {{false, "", ""}, {true, "", ""}, {true, "kit-unknown-9", ""}};
    std::string default_id;
    for (const gen::ScenarioInfo& s :
         gen::list_scenarios(std::string(YANTRA_SOURCE_DIR) + "/data/scenarios")) {
        if (s.is_default)
            default_id = s.id;
        for (std::string e : s.example_ids) {
            if (!e.empty() && e.back() == '*')
                e = e.substr(0, e.size() - 1) + "2";
            cases.push_back({true, e, s.id});
        }
    }
    REQUIRE(!default_id.empty());
    REQUIRE(cases.size() >= 7); // fraunces and the three MAK buildings
    for (Case& c : cases) {
        if (c.scenario.empty())
            c.scenario = default_id;
        ContextOptions ctx;
        ctx.plan = c.plan;
        ctx.example_id = c.example;
        Sim sim(ctx);
        const StreamResult r = sim.stream([](const Record&) { return true; }, 10.0);
        REQUIRE(r.records >= 1);
        CHECK_MESSAGE(sim.sensor("scenario_id").str == c.scenario, "example id " << c.example);
    }
}

TEST_CASE("simulator: levels follow the multi-wall model at each sim_position (RAD-94)")
{
    const std::vector<PlanPoint> path = {{4, 4},  {5, 5},  {6, 6},  {7, 5},  {9, 5},
                                         {10, 4}, {11, 5}, {12, 6}, {14, 5}, {16, 4}};
    Sim sim(fraunces_plan(path, 12));
    std::set<std::pair<double, double>> seen;
    int checked = 0;
    double worst = 0.0;
    const double offset = -20.0; // reference level of the scenario, gain 0
    sim.stream(
        [&](const Record& r) {
            if (!sim.is_cell(r, kT1Hz) || !sim.has_position(r))
                return false;
            CHECK(sim.v(r, YR_FIELD_SIM_FLOOR_ID).str == "fr-ground");
            const double want = formula_t1(fraunces_scene(), sim.x(r), sim.y(r));
            const double got = sim.v(r, YR_FIELD_LEVEL).f64 + offset;
            worst = std::max(worst, std::abs(got - want));
            CHECK_MESSAGE(std::abs(got - want) <= 0.5,
                          "at " << sim.x(r) << ", " << sim.y(r) << ": " << got << " dBm, model "
                                << want << " dBm");
            seen.insert({sim.x(r), sim.y(r)});
            ++checked;
            return seen.size() >= path.size();
        },
        60.0);
    CHECK(sim.offset() == doctest::Approx(offset));
    MESSAGE(checked << " records checked, worst " << worst << " dB");
    CHECK(seen.size() == path.size());
    CHECK(checked >= 20);
}

// The Fraunces plan as a scene 1.1 (RAD-36): the first drywall is a
// polyline with a vertex at (8, 5) on the line from a1, the second has
// per-band values (3 dB at 380 MHz, 9 dB at 400 MHz), and a second T1
// antenna is one floor up at its own level and slab sum.
std::string fraunces_scene_v1_1()
{
    return R"({"format": "yantra-radio-scene", "version": 1, "minor_version": 1,
  "floor": {"id": "fr-ground", "name": "Ground floor", "scale_set": true,
            "width_m": 20.0, "height_m": 10.0,
            "storeys": [{"floor_offset": 1, "level_m": 4.0, "slab_attenuation_db": 10.0}]},
  "wall_classes": [{"id": "drywall", "label": "Drywall", "attenuation_db": 3.0}],
  "walls": [{"x1_m": 6.0, "y1_m": 0.0, "x2_m": 8.0, "y2_m": 5.0, "class": "drywall",
             "attenuation_db": 3.0, "polyline": "d1"},
            {"x1_m": 8.0, "y1_m": 5.0, "x2_m": 6.0, "y2_m": 10.0, "class": "drywall",
             "attenuation_db": 3.0, "polyline": "d1"},
            {"x1_m": 13.0, "y1_m": 0.0, "x2_m": 13.0, "y2_m": 10.0, "class": "drywall",
             "attenuation_db": 3.0, "bands": [{"frequency_hz": 380000000, "attenuation_db": 3.0},
                                              {"frequency_hz": 400000000, "attenuation_db": 9.0}]}],
  "cells": [{"id": "T1", "label": "Indoor DAS", "mcc": 901, "mnc": 1, "la": 100,
             "colour_code": 1, "carrier_hz": 390000000}],
  "antennas": [{"id": "a1", "cell": "T1", "x_m": 3.0, "y_m": 5.0, "height_m": 2.8,
                "eirp_dbm": 10.0, "pattern": {"type": "omni"}},
               {"id": "a2", "cell": "T1", "x_m": 17.0, "y_m": 5.0, "height_m": 2.8,
                "eirp_dbm": 10.0, "floor_offset": 1, "pattern": {"type": "omni"}}]})";
}

TEST_CASE("simulator: a scene 1.1 is simulated with its polylines, bands and storeys (RAD-36, RAD-61, RAD-73)")
{
    const std::string scene = fraunces_scene_v1_1();
    // The same scene read as 1.0 would be: polyline ids, bands and storeys
    // dropped.
    radios::Scene v10;
    std::string error;
    REQUIRE(radios::parse_scene(scene, v10, error));
    for (radios::SceneWall& w : v10.walls) {
        w.polyline.clear();
        w.bands.clear();
    }
    v10.floor.storeys.clear();
    v10.minor_version = 0;
    const std::string scene10 = radios::scene_to_json(v10);

    const std::vector<PlanPoint> path = {{12, 5}, {15, 5}};
    for (const PlanPoint& p : path) {
        // Each point differs by more than 2 dB from the 1.0 reading.
        CAPTURE(p.x_m);
        CHECK(std::abs(formula_t1(scene, p.x_m, p.y_m) - formula_t1(scene10, p.x_m, p.y_m)) > 2.0);
    }
    Sim sim(fraunces_plan(path, 12, scene));
    std::set<std::pair<double, double>> seen;
    int checked = 0;
    const double offset = -20.0;
    sim.stream(
        [&](const Record& r) {
            if (!sim.is_cell(r, kT1Hz) || !sim.has_position(r))
                return false;
            const double want = formula_t1(scene, sim.x(r), sim.y(r));
            const double got = sim.v(r, YR_FIELD_LEVEL).f64 + offset;
            CHECK_MESSAGE(std::abs(got - want) <= 0.5,
                          "at " << sim.x(r) << ", " << sim.y(r) << ": " << got << " dBm, model " << want
                                << " dBm");
            seen.insert({sim.x(r), sim.y(r)});
            ++checked;
            return seen.size() >= path.size() && checked >= 6;
        },
        60.0);
    CHECK(sim.offset() == doctest::Approx(offset));
    CHECK(seen.size() == path.size());
}

// An own plan (no example id: the default scenario) whose cell uses the
// carrier of the default scenario's D0 (390.0 MHz), with another LA and
// colour code, an antenna at (3, 5) m and a concrete wall at x = 8 m.
std::string own_plan_scene()
{
    return R"({"format": "yantra-radio-scene", "version": 1,
  "floor": {"id": "own-ground", "name": "Ground floor", "scale_set": true,
            "width_m": 20.0, "height_m": 10.0},
  "wall_classes": [{"id": "concrete", "label": "Concrete", "attenuation_db": 12.0}],
  "walls": [{"x1_m": 8.0, "y1_m": 0.0, "x2_m": 8.0, "y2_m": 10.0, "class": "concrete",
             "attenuation_db": 12.0}],
  "cells": [{"id": "cell-1", "label": "Indoor DAS", "mcc": 901, "mnc": 1, "la": 321,
             "colour_code": 5, "carrier_hz": 390000000}],
  "antennas": [{"id": "antenna-1", "cell": "cell-1", "x_m": 3.0, "y_m": 5.0, "height_m": 2.8,
                "eirp_dbm": 10.0, "pattern": {"type": "omni"}}]})";
}

TEST_CASE("simulator: an own plan's cell on a default-scenario carrier is calculated from the plan's scene (RAD-61, RAD-73)")
{
    gen::Scenario def;
    for (const gen::ScenarioInfo& i : gen::list_scenarios(std::string(YANTRA_SOURCE_DIR) + "/data/scenarios"))
        if (i.is_default)
            def = gen::load_scenario_file(i.path);
    REQUIRE(def.find_cell("D0") != nullptr);
    REQUIRE(gen::cell_frequency_hz(*def.find_cell("D0")) == static_cast<double>(kT1Hz));
    radios::Scene scene;
    std::string error;
    REQUIRE(radios::parse_scene(own_plan_scene(), scene, error));
    gen::MultiWallParams p = def.channel.multiwall;
    REQUIRE(p.shadowing_sigma_db == 0.0);
    p.receiver_height_m = def.receiver.height_m;
    const gen::MultiWallModel model(p);

    ContextOptions c;
    c.plan = true;
    c.plan_name = "Own office";
    c.scene_json = {own_plan_scene()};
    c.cursor = {{7, 5}, {12, 5}};
    c.path_step_records = 60; // the first point outlasts the receiver's acquisition
    Sim sim(c);
    std::map<double, int> checked; // x -> records checked there
    std::map<double, int> arrived; // x -> records seen there
    sim.stream(
        [&](const Record& r) {
            if (!sim.is_cell(r, kT1Hz) || !sim.has_position(r) || (r.flags & YR_RECORD_NO_SYNC) != 0)
                return false;
            const double x = sim.x(r);
            // The first record at a new point can still hold some of the
            // interval before the cursor moved (the filters' delay).
            if (++arrived[x] <= 1)
                return false;
            const int walls = x > 8.0 ? 1 : 0;
            CHECK(sim.v(r, "cell_id").str == "cell-1");
            CHECK(sim.v(r, YR_FIELD_LA).u64 == 321);
            CHECK(sim.v(r, YR_FIELD_COLOUR_CODE).u64 == 5);
            CHECK(sim.v(r, "walls").u64 == static_cast<uint64_t>(walls));
            const double want = model.cell(scene, "cell-1", gen::PlanPoint{x, sim.y(r)}).level_dbm;
            const double got = sim.v(r, YR_FIELD_LEVEL).f64 + sim.offset();
            CHECK_MESSAGE(std::abs(got - want) <= 0.5, "at x " << x << ": " << got << " dBm, model " << want);
            ++checked[x];
            return checked.size() >= 2 && std::all_of(checked.begin(), checked.end(),
                                                      [](const auto& e) { return e.second >= 3; });
        },
        60.0);
    CHECK(checked.size() == 2);
    // Only one cell is on the carrier: the default scenario's D0 is gone,
    // its first-floor DAS and the macro site stay.
    CHECK(sim.sensor("scenario_id").str == def.id);
}

TEST_CASE("simulator: walk position and position sources (RAD-61)")
{
    ContextOptions ctx = fraunces_plan({{4, 4}});
    ctx.walk = {{6, 6}};
    Sim sim(ctx);
    auto first_position = [&] {
        std::pair<double, double> p{-1, -1};
        sim.stream(
            [&](const Record& r) {
                if (!sim.is_cell(r, kT1Hz) || !sim.has_position(r))
                    return false;
                p = {sim.x(r), sim.y(r)};
                return true;
            },
            20.0);
        return p;
    };
    CHECK(first_position() == std::pair<double, double>{6, 6}); // auto: the walk runs
    sim.set_enum("position_source", "mouse");
    CHECK(first_position() == std::pair<double, double>{4, 4});
    sim.set_enum("position_source", "fixed");
    sim.set_double("fixed_x_m", 9.5);
    sim.set_double("fixed_y_m", 2.25);
    CHECK(first_position() == std::pair<double, double>{9.5, 2.25});
}

TEST_CASE("simulator: a scene change is reloaded (RAD-36, RAD-94)")
{
    const PlanPoint p{6, 5};
    Sim sim(fraunces_plan({p}));
    const double before = formula_t1(fraunces_scene(10.0), p.x_m, p.y_m);
    const double after = formula_t1(fraunces_scene(20.0), p.x_m, p.y_m);
    REQUIRE(after - before == doctest::Approx(10.0));
    int phase = 0, ok_before = 0;
    bool reloaded = false;
    sim.stream(
        [&](const Record& r) {
            if (!sim.is_cell(r, kT1Hz) || !sim.has_position(r))
                return false;
            const double got = sim.v(r, YR_FIELD_LEVEL).f64 - 20.0;
            if (phase == 0) {
                CHECK(std::abs(got - before) <= 0.5);
                if (++ok_before == 3) {
                    std::string error;
                    REQUIRE(sim.host->context().configure(fraunces_plan({p}, 0, fraunces_scene(20.0)),
                                                          error));
                    sim.host->notify({YR_CONTEXT_SCENE_CHANGED});
                    phase = 1;
                }
                return false;
            }
            if (std::abs(got - after) <= 0.5) {
                reloaded = true;
                return true;
            }
            return false;
        },
        30.0);
    CHECK(reloaded);
    CHECK(sim.host->context().call_counts()["get_scene:OK"] >= 2);
}

TEST_CASE("simulator: no plan or no position gives the scenario levels (RAD-61, RAD-94)")
{
    const double scenario_t1 = fraunces().find_cell("T1")->level_dbm; // -65 dBm
    for (int with_plan = 0; with_plan < 2; ++with_plan) {
        CAPTURE(with_plan);
        // With a plan but no cursor and no walk: NO_POSITION.
        Sim sim(with_plan != 0 ? fraunces_plan() : ContextOptions{});
        if (with_plan == 0)
            sim.set_enum("scenario", "fraunces");
        int n = 0;
        sim.stream(
            [&](const Record& r) {
                if (!sim.is_cell(r, kT1Hz))
                    return false;
                CHECK_FALSE(sim.has_position(r));
                if ((r.flags & YR_RECORD_NO_SYNC) == 0) {
                    CHECK(sim.v(r, YR_FIELD_LEVEL).f64 + sim.offset() ==
                          doctest::Approx(scenario_t1).epsilon(0.5 / 65.0));
                    CHECK(sim.v(r, "level_source").str == "fixed");
                }
                return ++n >= 8;
            },
            30.0);
        CHECK(n >= 8);
        if (with_plan != 0)
            CHECK(sim.host->context().call_counts()["get_cursor:NO_POSITION"] >= 1);
    }
}

TEST_CASE("simulator: records match the ground truth: identity, neighbours, C1 (RAD-61)")
{
    const gen::Scenario s = fraunces();
    const gen::ScenarioCell& t1 = *s.find_cell("T1");
    const PlanPoint p{5, 5};
    Sim sim(fraunces_plan({p}));
    const int rxlev = -125 + 5 * t1.rxlev_access_min;
    const int txpwr = t1.ms_txpwr_max_cell == 0 ? 0 : 15 + 5 * (t1.ms_txpwr_max_cell - 1);
    for (double ms_max : {30.0, 40.0}) {
        CAPTURE(ms_max);
        if (ms_max != 30.0)
            sim.set_double("ms_max_power_dbm", ms_max);
        bool cell = false, neighbour = false;
        sim.stream(
            [&](const Record& r) {
                if (sim.is_cell(r, kT1Hz) && (r.flags & YR_RECORD_NO_SYNC) == 0 &&
                    (r.present & YR_GROUP_BROADCAST) != 0) {
                    CHECK(r.values[YR_CORE_MCC].u64 == s.mcc);
                    CHECK(r.values[YR_CORE_MNC].u64 == s.mnc);
                    CHECK(r.values[YR_CORE_LA].u64 == t1.location_area);
                    CHECK(r.values[YR_CORE_COLOUR_CODE].u64 == t1.colour_code);
                    CHECK(sim.v(r, "cell_id").str == "T1");
                    CHECK(sim.v(r, "best_antenna").str == "a1");
                    CHECK(sim.v(r, "rxlev_access_min_dbm").i64 == rxlev);
                    CHECK(sim.v(r, "ms_txpwr_max_cell_dbm").i64 == txpwr);
                    const double level = sim.v(r, YR_FIELD_LEVEL).f64 + sim.offset();
                    const double truth = formula_t1(fraunces_scene(), p.x_m, p.y_m);
                    // C1 from the decoded level and broadcast, and against the truth.
                    CHECK(sim.v(r, YR_FIELD_C1_DB).f64 ==
                          doctest::Approx(radios::tetra_c1_db(level, rxlev, txpwr, ms_max)));
                    CHECK(std::abs(sim.v(r, YR_FIELD_C1_DB).f64 -
                                   radios::tetra_c1_db(truth, rxlev, txpwr, ms_max)) <= 0.5);
                    cell = true;
                }
                if (r.schema_index == 1) {
                    const int la = sim.host->header().schemas[1].field_index("neighbour_la");
                    const int mc = sim.host->header().schemas[1].field_index("main_carrier");
                    REQUIRE(la >= 0);
                    REQUIRE(mc >= 0);
                    const gen::ScenarioCell& m1 = *s.find_cell("M1");
                    if (r.values[static_cast<std::size_t>(mc)].u64 == m1.main_carrier) {
                        if (r.values[static_cast<std::size_t>(la)].type == YR_TYPE_UINT)
                            CHECK(r.values[static_cast<std::size_t>(la)].u64 == m1.location_area);
                        neighbour = true;
                    }
                }
                return cell && neighbour;
            },
            30.0);
        CHECK(cell);
        CHECK(neighbour);
    }
}

namespace {

// What a stream of a scenario delivered, per record interval.
struct CellRecord {
    int64_t t_ns = 0;
    uint64_t hz = 0;
    uint64_t role = 0;
    double level_dbm = 0.0; ///< decoded level, dBm (true offset)
    std::optional<double> truth_dbm; ///< level_inst_dbm of its interval
};

gen::Scenario scenario_file(const std::string& id)
{
    return gen::load_scenario_file(std::string(YANTRA_SOURCE_DIR) + "/data/scenarios/" + id + ".json");
}

// The span plan_ranges offers.
std::pair<uint64_t, uint64_t> offered_span(Sim& sim)
{
    std::pair<uint64_t, uint64_t> span{0, 0};
    const auto cb = [](void* user, const yr_plan_ranges* r) {
        REQUIRE(r->range_count == 1);
        *static_cast<std::pair<uint64_t, uint64_t>*>(user) = {r->ranges[0].first_hz, r->ranges[0].last_hz};
    };
    REQUIRE(sim.host->descriptor().stream->plan_ranges(sim.device, cb, &span) == YR_OK);
    return span;
}

// Streams `sim` until every carrier of `want` has `per_cell` synchronised
// records with a ground truth; the synchronised tetra_cell records of the
// scenario's carriers (others: the search's NO_SYNC candidates).
std::vector<CellRecord> stream_cells(Sim& sim, const std::set<uint64_t>& want, int per_cell,
                                     uint64_t serving_hz = 0)
{
    std::vector<CellRecord> out;
    std::map<uint64_t, int> synced;
    sim.stream(
        [&](const Record& r) {
            if (r.schema_index != 0 || (r.flags & YR_RECORD_NO_SYNC) != 0)
                return false;
            const uint64_t hz = r.values[YR_CORE_FREQUENCY_HZ].u64;
            CellRecord c;
            c.t_ns = r.time.monotonic_ns;
            c.hz = hz;
            c.role = static_cast<uint64_t>(r.values[YR_CORE_ROLE].i64);
            c.level_dbm = sim.v(r, YR_FIELD_LEVEL).f64; // dBFS until the stream ends
            if (const radios::Value& v = sim.v(r, "level_inst_dbm"); v.type == YR_TYPE_DOUBLE)
                c.truth_dbm = v.f64;
            out.push_back(c);
            if (want.count(hz) != 0 && c.truth_dbm)
                ++synced[hz];
            for (uint64_t f : want)
                if (synced[f] < per_cell)
                    return false;
            return true;
        },
        60.0, {}, {}, serving_hz);
    const double offset = sim.offset(); // known once the stream ran
    for (CellRecord& c : out)
        c.level_dbm += offset;
    for (uint64_t f : want) {
        CAPTURE(f);
        CHECK(synced[f] >= per_cell);
    }
    return out;
}

// The serving record of each of the last `n` intervals in which every
// carrier of `want` was measured: {interval time, its records}.
std::vector<std::vector<CellRecord>> last_full_intervals(const std::vector<CellRecord>& recs,
                                                         const std::set<uint64_t>& want, std::size_t n)
{
    std::map<int64_t, std::vector<CellRecord>> by_t;
    for (const CellRecord& c : recs)
        if (want.count(c.hz) != 0 && c.truth_dbm)
            by_t[c.t_ns].push_back(c);
    std::vector<std::vector<CellRecord>> out;
    for (auto it = by_t.rbegin(); it != by_t.rend() && out.size() < n; ++it)
        if (it->second.size() == want.size())
            out.push_back(it->second);
    return out;
}

// Every cell of a bundled scenario the capture holds is measured, with
// its level within 0.5 dB of the ground truth (as decode-iq, RAD-63), and
// the serving cell is the strongest (RAD-61, RAD-102, RAD-112).
void check_every_cell(const std::string& id)
{
    CAPTURE(id);
    const gen::Scenario s = scenario_file(id);
    Sim sim;
    sim.set_enum("scenario", id);
    const auto [first, last] = offered_span(sim);
    const double centre = gen::centre_frequency_hz(s);
    std::set<uint64_t> want;
    std::map<uint64_t, std::string> cell_of;
    for (const gen::ScenarioCell& c : s.cells) {
        const double hz = gen::cell_frequency_hz(c);
        const auto f = static_cast<uint64_t>(std::llround(hz));
        CAPTURE(c.id);
        // Every cell the capture holds is in the span plans may use.
        if (std::abs(hz - centre) + 15e3 <= s.sample_rate_hz / 2.0)
            CHECK((f >= first && f <= last));
        bool on = c.on_air;
        for (const gen::ScenarioEvent& e : s.events)
            if (e.cell == c.id && e.type == gen::EventType::cell_on)
                on = true;
        if (on && f >= first && f <= last)
            want.insert(f);
        cell_of[f] = c.id;
    }
    REQUIRE(want.size() >= 2);
    const std::vector<CellRecord> recs = stream_cells(sim, want, 12);

    // Levels: each record against its interval's truth, the truth at the
    // start of this and the next interval of the carrier (a moving
    // receiver changes the level within an interval).
    for (uint64_t f : want) {
        CAPTURE(cell_of[f]);
        std::vector<CellRecord> mine;
        for (const CellRecord& c : recs)
            if (c.hz == f && c.truth_dbm)
                mine.push_back(c);
        int checked = 0, inside = 0;
        for (std::size_t i = 0; i + 1 < mine.size(); ++i) {
            const double lo = std::min(*mine[i].truth_dbm, *mine[i + 1].truth_dbm);
            const double hi = std::max(*mine[i].truth_dbm, *mine[i + 1].truth_dbm);
            const double err = std::max({lo - mine[i].level_dbm, mine[i].level_dbm - hi, 0.0});
            ++checked;
            inside += err <= 0.5 ? 1 : 0;
            // A receiver walking through a shadowing field: the level in
            // the interval need not lie between the two samples of the truth.
            CHECK_MESSAGE(err <= 2.0, mine[i].level_dbm << " dBm, truth " << lo << " to " << hi);
        }
        CHECK(checked >= 10);
        CHECK(inside * 10 >= checked * 9);
    }
    // Only the scenario's carriers synchronise, and a cell off the air
    // throughout (failed_site's N3) never.
    for (const CellRecord& c : recs) {
        CAPTURE(c.hz);
        CHECK(cell_of.count(c.hz) == 1);
        CHECK((want.count(c.hz) == 1 || !c.truth_dbm));
    }

    // Serving: one per interval; the strongest decoded level within the
    // engine's 3 dB hysteresis, and the strongest cell of the truth where
    // the levels do not move (no path, no fading).
    const bool fixed = s.path.points.empty() && s.channel.fading == gen::FadingModel::none;
    const auto intervals = last_full_intervals(recs, want, 5);
    CHECK(intervals.size() == 5);
    for (const auto& iv : intervals) {
        const CellRecord* serving = nullptr;
        const CellRecord* strongest = nullptr;
        double best = -1e9;
        for (const CellRecord& c : iv) {
            if (c.role == YR_ROLE_SERVING) {
                CHECK(serving == nullptr);
                serving = &c;
            }
            best = std::max(best, c.level_dbm);
            if (strongest == nullptr || *c.truth_dbm > *strongest->truth_dbm)
                strongest = &c;
        }
        REQUIRE(serving != nullptr);
        CHECK(serving->level_dbm >= best - 3.0 - 1e-6);
        if (fixed)
            CHECK(cell_of[serving->hz] == cell_of[strongest->hz]);
    }
}

} // namespace

// One case per bundled multi-cell scenario (RAD-61, RAD-74). weak_signal
// is left out: its second cell is 0.6 dB under the noise in 18 kHz by
// design (below the search's margin and the decoder's sensitivity).
TEST_CASE("simulator: every cell of failed_site is measured (RAD-61, RAD-102, RAD-112)")
{
    check_every_cell("failed_site");
}
TEST_CASE("simulator: every cell of floor_change is measured (RAD-61, RAD-102, RAD-112)")
{
    check_every_cell("floor_change");
}
TEST_CASE("simulator: every cell of fraunces is measured (RAD-61, RAD-102, RAD-112)")
{
    check_every_cell("fraunces");
}
TEST_CASE("simulator: every cell of indoor_good_das is measured (RAD-61, RAD-102, RAD-112)")
{
    check_every_cell("indoor_good_das");
}
TEST_CASE("simulator: every cell of indoor_weak_das is measured (RAD-61, RAD-102, RAD-112)")
{
    check_every_cell("indoor_weak_das");
}
TEST_CASE("simulator: every cell of mak_beer_house is measured (RAD-61, RAD-102, RAD-112)")
{
    check_every_cell("mak_beer_house");
}
TEST_CASE("simulator: every cell of mak_haus_claeson is measured (RAD-61, RAD-102, RAD-112)")
{
    check_every_cell("mak_haus_claeson");
}
TEST_CASE("simulator: every cell of mak_villa_bunzl is measured (RAD-61, RAD-102, RAD-112)")
{
    check_every_cell("mak_villa_bunzl");
}

TEST_CASE("simulator: a given serving carrier is followed, the other measured (RAD-32, RAD-61)")
{
    // failed_site: N1 (390.0 MHz, -72 dBm) at the span's edge, N2 (390.2
    // MHz, -85 dBm) at its centre. Each one asked for is the serving
    // cell, even the weaker; the other is its announced neighbour; one
    // decoder per carrier (a given serving carrier is pinned once).
    constexpr uint64_t kN1 = 390'000'000, kN2 = 390'200'000;
    for (uint64_t serving : {kN1, kN2}) {
        CAPTURE(serving);
        Sim sim;
        sim.set_enum("scenario", "failed_site");
        const std::set<uint64_t> want{kN1, kN2};
        const std::vector<CellRecord> recs = stream_cells(sim, want, 12, serving);
        std::map<int64_t, std::map<uint64_t, int>> per_interval;
        for (const CellRecord& c : recs)
            ++per_interval[c.t_ns][c.hz];
        for (const auto& [t, counts] : per_interval)
            for (const auto& [hz, n] : counts) {
                CAPTURE(hz);
                CHECK(n == 1);
            }
        const auto intervals = last_full_intervals(recs, want, 5);
        CHECK(intervals.size() == 5);
        for (const auto& iv : intervals)
            for (const CellRecord& c : iv) {
                CAPTURE(c.hz);
                CHECK(c.role == (c.hz == serving ? YR_ROLE_SERVING : YR_ROLE_NEIGHBOUR));
                CHECK(std::abs(c.level_dbm - *c.truth_dbm) <= 0.5);
            }
    }
}

TEST_CASE("simulator: the same seed gives the same output (RAD-94)")
{
    auto levels = [](uint64_t seed) {
        Sim sim;
        sim.set_enum("scenario", "fraunces");
        sim.set_uint("seed", seed);
        std::vector<double> out;
        sim.stream(
            [&](const Record& r) {
                if (sim.is_cell(r, kT1Hz) || sim.is_cell(r, kM1Hz))
                    out.push_back(sim.v(r, YR_FIELD_LEVEL).f64);
                return out.size() >= 16;
            },
            30.0);
        return out;
    };
    const std::vector<double> a = levels(7), b = levels(7), c = levels(8);
    REQUIRE(a.size() == 16);
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("simulator: the practice calibration recovers the true offset (RAD-61, RAD-94)")
{
    for (double gain : {0.0, 17.0}) {
        CAPTURE(gain);
        Sim sim;
        sim.set_bool("calibration_generator", true);
        sim.set_double("calibration_level_dbm", -60.0);
        sim.set_double("gain_db", gain);
        std::vector<double> levels;
        sim.stream(
            [&](const Record& r) {
                if (r.schema_index == 0 && (r.flags & YR_RECORD_NO_SYNC) == 0)
                    levels.push_back(sim.v(r, YR_FIELD_LEVEL).f64);
                return levels.size() >= 10;
            },
            30.0);
        REQUIRE(levels.size() >= 10);
        double mean = 0.0;
        for (std::size_t i = 2; i < levels.size(); ++i)
            mean += levels[i] / static_cast<double>(levels.size() - 2);
        const double found = -60.0 - mean; // what Yantra's calibration works out (RF-02)
        CHECK(sim.offset() == doctest::Approx(-20.0 - gain));
        CHECK(std::abs(found - sim.offset()) <= 0.3);
    }
}

TEST_CASE("simulator: a slow sink gives OVERFLOW and DROPPED and the stream goes on (RAD-24, RAD-25)")
{
    Sim sim;
    sim.set_enum("scenario", "fraunces");
    sim.set_double("device_buffer_s", 0.3);
    SinkOptions slow;
    slow.push_hold = std::chrono::milliseconds(150);
    uint64_t overflow = 0, dropped = 0, after = 0;
    const StreamResult r = sim.stream(
        [&](const Record&) {
            if (overflow > 0)
                ++after;
            return after >= 5;
        },
        30.0,
        [&](const radios::Event& e) {
            if (e.event == YR_EVENT_OVERFLOW)
                ++overflow;
            if (e.event == YR_EVENT_DROPPED) {
                CHECK(e.value > 0);
                ++dropped;
            }
            CHECK(e.event != YR_EVENT_ERROR);
        },
        slow);
    CHECK(overflow >= 1);
    CHECK(dropped == overflow);
    CHECK(after >= 5);
    REQUIRE(r.stats_ok);
    CHECK(r.stats.overflows >= 1);
    CHECK(r.stats.samples_dropped > 0);
}

TEST_CASE("simulator: pause and resume, live settings and the simulated fault (RAD-34, RAD-61)")
{
    Sim sim;
    sim.set_enum("scenario", "fraunces");
    auto mean_level_snr = [&](int n) {
        std::vector<std::pair<double, double>> v;
        const StreamResult r = sim.stream(
            [&](const Record& rec) {
                if (sim.is_cell(rec, kT1Hz) && (rec.flags & YR_RECORD_NO_SYNC) == 0 &&
                    (rec.present & YR_GROUP_QUALITY) != 0)
                    v.emplace_back(sim.v(rec, YR_FIELD_LEVEL).f64, sim.v(rec, YR_FIELD_SNR_DB).f64);
                return static_cast<int>(v.size()) >= n;
            },
            30.0);
        CHECK(r.stats.records_delivered == r.records + r.records_beyond_limit + r.dropped);
        double l = 0.0, s = 0.0;
        for (std::size_t i = v.size() / 2; i < v.size(); ++i) {
            l += v[i].first;
            s += v[i].second;
        }
        const double k = static_cast<double>(v.size() - v.size() / 2);
        return std::pair<double, double>{l / k, s / k};
    };
    const auto first = mean_level_snr(6);
    const auto again = mean_level_snr(6); // stopped and started again
    CHECK(again.first == doctest::Approx(first.first).epsilon(0.5 / std::abs(first.first)));
    sim.set_double("signal_offset_db", -10.0);
    const auto lower = mean_level_snr(6);
    CHECK(lower.first - first.first == doctest::Approx(-10.0).epsilon(0.05));
    // The fault: 20 dB more receiver noise. 30 dB below the scenario T1
    // has about 29 dB SNR (the SNR estimate saturates near 35 dB).
    sim.set_double("signal_offset_db", -30.0);
    const auto weak = mean_level_snr(6);
    std::string result;
    REQUIRE(run_action(*sim.host, sim.device, "inject_fault", result) == YR_OK);
    const auto noisy = mean_level_snr(6);
    MESSAGE("SNR " << weak.second << " dB, with the fault " << noisy.second << " dB");
    CHECK(noisy.second < weak.second - 10.0);
    REQUIRE(run_action(*sim.host, sim.device, "clear_fault", result) == YR_OK);
}

TEST_SUITE("timing")
{
    TEST_CASE("simulator: the level follows a cursor move within 1 s (RAD-94)")
    {
        const PlanPoint a{4, 5}, b{15, 5};
        Sim sim(fraunces_plan({a}));
        const double want_b = formula_t1(fraunces_scene(), b.x_m, b.y_m);
        REQUIRE(std::abs(want_b - formula_t1(fraunces_scene(), a.x_m, a.y_m)) > 10.0);
        int at_a = 0;
        int64_t moved_ns = 0;
        int64_t followed_ns = -1;
        const yr_host_api& api = sim.host->host_api();
        sim.stream(
            [&](const Record& r) {
                if (!sim.is_cell(r, kT1Hz) || !sim.has_position(r))
                    return false;
                if (moved_ns == 0) {
                    if (++at_a == 3) {
                        moved_ns = api.monotonic_ns(api.host);
                        sim.host->notify(sim.host->context().set_cursor(b));
                    }
                    return false;
                }
                const double got = sim.v(r, YR_FIELD_LEVEL).f64 + sim.offset();
                if (sim.x(r) == b.x_m && std::abs(got - want_b) <= 0.5) {
                    followed_ns = r.time.monotonic_ns;
                    return true;
                }
                return false;
            },
            30.0);
        REQUIRE(moved_ns != 0);
        REQUIRE(followed_ns > 0);
        MESSAGE("followed after " << (followed_ns - moved_ns) / 1e6 << " ms");
        CHECK(followed_ns - moved_ns <= 1'000'000'000);
    }

    TEST_CASE("simulator: real-time pacing within 1 % (RAD-94)")
    {
        for (double speed : {1.0, 2.0}) {
            CAPTURE(speed);
            Sim sim;
            sim.set_enum("scenario", "fraunces");
            sim.set_double("playback_speed", speed);
            const yr_host_api& api = sim.host->host_api();
            int64_t first_wall = 0, last_wall = 0, first_rec = 0, last_rec = 0;
            int intervals = 0;
            sim.stream(
                [&](const Record& r) {
                    if (!sim.is_cell(r, kT1Hz))
                        return false;
                    const int64_t now = api.monotonic_ns(api.host);
                    if (first_wall == 0) {
                        first_wall = now;
                        first_rec = r.time.monotonic_ns;
                    } else {
                        ++intervals;
                    }
                    last_wall = now;
                    last_rec = r.time.monotonic_ns;
                    return last_wall - first_wall >= 8'000'000'000;
                },
                30.0);
            const double wall = static_cast<double>(last_wall - first_wall) / 1e9;
            const double stamped = static_cast<double>(last_rec - first_rec) / 1e9;
            const double signal = intervals * 0.2; // 5 records per signal second
            MESSAGE("speed " << speed << ": wall " << wall << " s, record times " << stamped
                             << " s, signal " << signal << " s");
            CHECK(std::abs(stamped / wall - 1.0) <= 0.01);
            CHECK(std::abs(signal / (wall * speed) - 1.0) <= 0.01);
        }
    }
}
