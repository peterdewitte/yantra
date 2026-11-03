// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator plugin (RAD-61, RAD-94) through the test host's emulated
// context callbacks (RAD-18, RAD-91): levels against the multi-wall model
// (RAD-73) at each record's sim_position, a cursor move followed within
// 1 s, scene reload, the scenario of each example id, the fallback without
// plan or position, ground truth (identity, neighbours, C1), seeds,
// pacing, the practice calibration, a slow sink (RAD-24, RAD-25) and
// pause/resume.
#include <doctest/doctest.h>

#include <yantra/gen/multiwall.hpp>
#include <yantra/gen/scenario.hpp>
#include <yantra/radios/tetra_schemas.hpp>

#include "host.hpp"

#include <atomic>
#include <cmath>
#include <map>
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

    // Streams with a serving plan until `done` says so (or `seconds` pass).
    StreamResult stream(const std::function<bool(const Record&)>& on_record, double seconds,
                        const std::function<void(const radios::Event&)>& on_event = {},
                        const SinkOptions& sink = {})
    {
        yr_plan plan{};
        plan.struct_size = sizeof plan;
        plan.type = YR_PLAN_SERVING;
        std::atomic<bool> stop{false};
        StreamLimits limits;
        limits.max_seconds = seconds;
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
