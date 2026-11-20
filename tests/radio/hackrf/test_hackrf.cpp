// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The HackRF plugin (RAD-60) against the fake module registered as the
// driver "hackrf" (RAD-92): settings by the names SoapyHackRF documents
// (RAD-51), the antenna port power safety rule, presets, sensors, self
// test, records that match the generator's truth with offset tuning,
// C1 from a calibration offset (RAD-22), band scans (RAD-23), overflow
// events (RAD-24, RAD-25) and hot-plug (RAD-35).
#include <doctest/doctest.h>

#include "test_platform.hpp"

#include <yantra/gen/scenario.hpp>
#include <yantra/gen/scenario_engine.hpp>
#include <yantra/radios/tetra_schemas.hpp>

#include "host.hpp"
#include "stream_guard.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

using namespace yantra;
using namespace yantra::radio_host;
using radios::Record;
namespace fs = std::filesystem;

namespace {

const char* const kSerial = "0000000000000000a1b2c3d4e5f60001";
const std::string kKey = std::string("hackrf:") + kSerial;

std::string fixture() { return std::string(YANTRA_TEST_DATA_DIR) + "/soapy/fake_two_cells.json"; }

std::string control_dir()
{
    static int n = 0;
    const fs::path p = fs::path(YANTRA_BINARY_DIR) / "tests" / "hackrf_control" /
                       (std::to_string(getpid()) + "_" + std::to_string(++n));
    fs::remove_all(p);
    fs::create_directories(p);
    return p.generic_string(); // goes into JSON text: forward slashes
}

bool wait_for(const std::function<bool()>& ok, double seconds)
{
    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::duration<double>(seconds * YANTRA_TEST_TIME_SCALE);
    while (!ok()) {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

struct Fake {
    std::string control;
    std::unique_ptr<Host> host;
    yr_device* device = nullptr;

    explicit Fake(const std::string& extra = "") : control(control_dir())
    {
        const std::string json = "{\"control\":\"" + control + "\",\"devices\":[{\"serial\":\"" + kSerial +
                                 "\",\"scenario\":\"" + fixture() + "\"" + extra + "}]}";
        REQUIRE(yantra::test::set_env("YANTRA_FAKE_SOAPY", json.c_str()) == 0);
        std::string error;
        host = Host::load(HACKRF_FAKE_PLUGIN, HostConfig{}, error);
        REQUIRE_MESSAGE(host != nullptr, error);
        REQUIRE(host->descriptor_problems().empty());
    }
    ~Fake()
    {
        if (device != nullptr)
            host->descriptor().stream->close(device);
        host.reset();
        yantra::test::unset_env("YANTRA_FAKE_SOAPY");
        std::error_code ec;
        fs::remove_all(control, ec);
    }
    void open()
    {
        REQUIRE_MESSAGE(host->descriptor().stream->open(host->plugin(), kKey.c_str(), &device) == YR_OK,
                        host->last_error(nullptr));
    }
    void close()
    {
        host->descriptor().stream->close(device);
        device = nullptr;
    }
    std::map<std::string, SettingDef> settings(bool sensors = false)
    {
        std::vector<SettingDef> defs;
        REQUIRE(list_settings(*host, device, sensors, defs) == YR_OK);
        std::map<std::string, SettingDef> out;
        for (const SettingDef& d : defs)
            out[d.key] = d;
        return out;
    }
    void set_double(const std::string& key, double x)
    {
        Value v;
        v.type = YR_TYPE_DOUBLE;
        v.f64 = x;
        REQUIRE_MESSAGE(write_value(*host, device, key, v) == YR_OK, host->last_error(device));
    }
    void set_bool(const std::string& key, bool on)
    {
        Value v;
        v.type = YR_TYPE_BOOL;
        v.i64 = on ? 1 : 0;
        REQUIRE_MESSAGE(write_value(*host, device, key, v) == YR_OK, host->last_error(device));
    }
    Value get(const std::string& key, bool sensor = false)
    {
        Value v;
        REQUIRE_MESSAGE(read_value(*host, device, key, sensor, v) == YR_OK, host->last_error(device));
        return v;
    }
    /// Total gain 40 dB: the fake's reference gain, so dBFS = dBm + 20.
    void reference_gain()
    {
        set_double("gain_lna", 16.0);
        set_double("gain_vga", 24.0);
        set_bool("rf_amplifier", false);
    }
    std::string unplugged() const { return control + "/" + kSerial + ".unplugged"; }
    uint32_t state() { return host->descriptor().stream->state(device); }

    StreamResult stream(const yr_plan& plan, const std::function<bool(const Record&)>& on_record,
                        double signal_s, const std::function<bool(const radios::Event&)>& on_event = {})
    {
        // At most signal_s of signal: the unpaced fake is CPU-bound, a wall-clock
        // limit failed on a loaded machine (tests/stream_guard.hpp).
        std::atomic<bool> stop{false};
        tests::StreamGuard guard(signal_s, stop);
        StreamLimits limits;
        limits.interrupt = &stop;
        StreamResult r = run_stream(
            *host, device, plan, limits,
            [&](const Record& rec) {
                guard.record(rec);
                if (on_record(rec))
                    stop = true;
            },
            [&](const radios::Event& e) {
                guard.event();
                if (on_event && on_event(e))
                    stop = true;
            });
        if (r.stop_reason == "interrupted" && !guard.reason().empty())
            r.stop_reason = guard.reason() + " guard after " + std::to_string(guard.signal_s()) + " s of signal";
        return r;
    }
    int field(uint32_t schema, const char* name) const
    {
        return host->header().schemas.at(schema).field_index(name);
    }
};

yr_plan serving_plan()
{
    yr_plan plan{};
    plan.struct_size = sizeof plan;
    plan.type = YR_PLAN_SERVING;
    return plan;
}

yr_plan scan_plan(uint64_t first, uint64_t last)
{
    yr_plan plan = serving_plan();
    plan.type = YR_PLAN_SCAN;
    plan.scan_first_hz = first;
    plan.scan_last_hz = last;
    plan.scan_step_hz = 25'000;
    return plan;
}

// What the generator says the antenna sees, dBm, per cell id.
std::map<std::string, double> truth_levels()
{
    gen::Scenario s = gen::load_scenario_file(fixture());
    s.duration_s = 0.0;
    s.centre_frequency_hz = 392.5e6;
    gen::ScenarioEngine g(s);
    std::vector<std::complex<float>> x(200000);
    g.read(x.data(), x.size());
    std::map<std::string, double> out;
    for (const gen::TruthRow& row : g.take_truth())
        for (const gen::CellTruth& c : row.cells)
            out[c.cell] = c.level_dbm;
    return out;
}

bool identified(const Record& r)
{
    return r.schema_index == 0 && r.values[YR_CORE_LA].type == YR_TYPE_UINT &&
           r.values[YR_CORE_LEVEL].type == YR_TYPE_DOUBLE;
}

} // namespace

TEST_CASE("hackrf: the device enumerates under the hackrf driver only (RAD-30, RAD-52, RAD-62)")
{
    Fake f;
    std::vector<radios::DeviceInfo> devices;
    std::string diagnostic;
    REQUIRE(enumerate_devices(*f.host, devices, diagnostic) == YR_OK);
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].key == kKey);
    CHECK(devices[0].driver == "hackrf");
    CHECK(devices[0].serial == kSerial);
    CHECK(devices[0].module_version.find("0.3.6-fake") != std::string::npos);
    f.open();
    std::string hw;
    f.host->descriptor().stream->device_info(
        f.device, [](void* u, const yr_device_info* i) { *static_cast<std::string*>(u) = i->hardware_info; }, &hw);
    CHECK(hw.find("version=2024.02.1") != std::string::npos);
    CHECK(hw.find("hardware=HackRF One") != std::string::npos);
    CHECK((f.host->descriptor().capabilities & YR_CAP_SCAN) != 0);
    CHECK((f.host->descriptor().capabilities & YR_CAP_HOTPLUG) != 0);
    CHECK((f.host->descriptor().capabilities & YR_CAP_SELF_CALIBRATED) == 0);
}

TEST_CASE("hackrf: settings by the names SoapyHackRF documents (RAD-60, RAD-51)")
{
    SUBCASE("a HackRF One")
    {
        Fake f;
        f.open();
        auto s = f.settings();
        REQUIRE(s.count("receiver_rate") == 1);
        const auto& rates = s["receiver_rate"].items;
        REQUIRE(rates.size() >= 2);
        CHECK(rates.front().label == "2 Msps");
        CHECK(rates.back().label == "20 Msps");
        CHECK(f.get("receiver_rate").i64 == 0); // 2 Msps
        CHECK(s["bandwidth"].items.size() == 17);
        CHECK(s["gain_lna"].label == "LNA (IF) gain");
        CHECK(s["gain_lna"].step == 8.0);
        CHECK(s["gain_vga"].label == "VGA (baseband) gain");
        CHECK(s["gain_vga"].maximum == 62.0);
        REQUIRE(s.count("rf_amplifier") == 1);
        CHECK(s.count("gain_amp") == 0);
        CHECK(s["rf_amplifier"].type == YR_TYPE_BOOL);
        CHECK(s["rf_amplifier"].default_value.i64 == 0);
        for (const char* k : {"gain_lna", "gain_vga", "rf_amplifier", "antenna"})
            CHECK_MESSAGE((s[k].flags & YR_SETTING_CALIBRATION) != 0, k);
        REQUIRE(s.count("antenna_power") == 1);
        CHECK(s.count("arg_bias_tx") == 0);
        CHECK(s["antenna_power"].type == YR_TYPE_BOOL);
        CHECK((s["antenna_power"].flags & YR_SETTING_WARNING) != 0);
        CHECK(!s["antenna_power"].warning.empty());
        CHECK(s["antenna_power"].default_value.i64 == 0);
        CHECK((s["clock_source"].flags & YR_SETTING_UNAVAILABLE) != 0);
        CHECK(!s["clock_source"].unavailable_reason.empty());
        CHECK(s["offset_khz"].default_value.f64 == 250.0);
        CHECK(s.count("c1_enabled") == 1);
        CHECK(s.count("c1_level_offset_db") == 1);
        CHECK(s.count("scan_dwell_s") == 1);
        CHECK(s.count("ms_max_power_dbm") == 1);
        // The amplifier switches the AMP element between 0 and 14 dB.
        f.set_bool("rf_amplifier", true);
        CHECK(f.get("rf_amplifier").i64 == 1);
        CHECK(f.get("gain_state", true).str.find("AMP on") != std::string::npos);
    }
    SUBCASE("a module without the AMP element and with an external reference")
    {
        Fake f(R"(,"gains":[{"name":"LNA","min":0,"max":40,"step":8},{"name":"VGA","min":0,"max":62,"step":2}],"clock_sources":["internal","external"])");
        f.open();
        auto s = f.settings();
        REQUIRE(s.count("rf_amplifier") == 1);
        CHECK((s["rf_amplifier"].flags & YR_SETTING_UNAVAILABLE) != 0);
        CHECK(s["rf_amplifier"].unavailable_reason.find("AMP") != std::string::npos);
        CHECK(f.get("rf_amplifier").i64 == 0);
        CHECK((s["clock_source"].flags & YR_SETTING_UNAVAILABLE) == 0);
        Value v;
        v.type = YR_TYPE_BOOL;
        v.i64 = 1;
        CHECK(write_value(*f.host, f.device, "rf_amplifier", v) == YR_ERR_UNSUPPORTED);
        std::vector<SelfTestItem> items;
        REQUIRE(run_self_test(*f.host, f.device, items) == YR_OK);
        bool warned = false;
        for (const SelfTestItem& it : items)
            if (it.name == "gains") {
                warned = it.result == YR_TEST_WARN && it.detail.find("AMP") != std::string::npos;
                CHECK(!it.remedy.empty());
            }
        CHECK(warned);
    }
}

TEST_CASE("hackrf: antenna port power is off at every open and replug, never in a preset (RAD-60)")
{
    Fake f(R"(,"paced":false)");
    f.open();
    CHECK(f.get("antenna_power").i64 == 0);
    f.set_bool("antenna_power", true);
    CHECK(f.get("antenna_power").i64 == 1);
    // The self-test warns while it is on.
    std::vector<SelfTestItem> items;
    REQUIRE(run_self_test(*f.host, f.device, items) == YR_OK);
    bool warned = false;
    for (const SelfTestItem& it : items)
        if (it.name == "antenna_power")
            warned = it.result == YR_TEST_WARN && !it.remedy.empty();
    CHECK(warned);
    // Presets leave it alone.
    REQUIRE(f.host->descriptor().device->apply_preset(f.device, "near_site_strong") == YR_OK);
    CHECK(f.get("antenna_power").i64 == 1);
    // Reopened: off.
    f.close();
    f.open();
    CHECK(f.get("antenna_power").i64 == 0);
    // Replugged: off, and the host is told the settings changed.
    f.set_bool("antenna_power", true);
    const uint64_t changed = f.host->settings_changed_count();
    std::ofstream(f.unplugged()) << "";
    REQUIRE(wait_for([&] { return f.state() == YR_DEVICE_LOST; }, 10.0));
    fs::remove(f.unplugged());
    REQUIRE(wait_for([&] { return f.state() == YR_DEVICE_OPEN; }, 10.0));
    CHECK(wait_for([&] { return f.host->settings_changed_count() > changed; }, 10.0));
    CHECK(f.get("antenna_power").i64 == 0);
}

TEST_CASE("hackrf: presets set the gains (RAD-60, RAD-43, RAD-44)")
{
    Fake f;
    f.open();
    std::vector<PresetDef> presets;
    REQUIRE(list_presets(*f.host, f.device, presets) == YR_OK);
    REQUIRE(presets.size() == 2);
    CHECK(presets[0].id == "indoor_sensitive");
    CHECK(presets[0].label == "Indoor, sensitive");
    CHECK(presets[1].id == "near_site_strong");
    CHECK(presets[1].label == "Near site, strong signal");
    const uint64_t changed = f.host->settings_changed_count();
    REQUIRE(f.host->descriptor().device->apply_preset(f.device, "indoor_sensitive") == YR_OK);
    CHECK(f.get("rf_amplifier").i64 == 1);
    CHECK(f.get("gain_lna").f64 == 32.0);
    CHECK(f.get("gain_vga").f64 == 30.0);
    REQUIRE(f.host->descriptor().device->apply_preset(f.device, "near_site_strong") == YR_OK);
    CHECK(f.get("rf_amplifier").i64 == 0);
    CHECK(f.get("gain_lna").f64 == 8.0);
    CHECK(f.get("gain_vga").f64 == 16.0);
    CHECK(wait_for([&] { return f.host->settings_changed_count() >= changed + 2; }, 5.0));
    CHECK(f.host->descriptor().device->apply_preset(f.device, "no_such_preset") == YR_ERR_NOT_FOUND);
}

TEST_CASE("hackrf: sensors and self-test (RAD-60, RAD-45, RAD-46)")
{
    Fake f(R"(,"paced":false)");
    f.open();
    CHECK(f.get("board_id", true).str == "HackRF One");
    CHECK(f.get("firmware_version", true).str == "2024.02.1");
    CHECK(f.get("serial_number", true).str == kSerial);
    CHECK(f.get("part_id", true).str == "a000cb3c00f1f1f1");
    CHECK(f.get("noise_floor_dbfs", true).type == YR_TYPE_NONE); // not streamed yet
    CHECK(f.get("overload", true).str == "none");
    f.reference_gain();
    CHECK(f.get("gain_state", true).str == "LNA 16 dB, VGA 24 dB, AMP off");
    int n = 0;
    f.stream(serving_plan(), [&](const Record& r) { return identified(r) && ++n >= 6; }, 30.0);
    REQUIRE(n >= 6);
    // Thermal noise + 7 dB noise figure in 25 kHz is -123 dBm: -103 dBFS here.
    const Value floor = f.get("noise_floor_dbfs", true);
    REQUIRE(floor.type == YR_TYPE_DOUBLE);
    CHECK(floor.f64 > -112.0);
    CHECK(floor.f64 < -95.0);
    CHECK(f.get("receiver_load", true).f64 > 0.0);
    CHECK(f.get("clipped_percent", true).f64 == 0.0);
    CHECK(std::fabs(f.get("tuned_mhz", true).f64 - 392.25) < 1e-6); // offset tuning

    std::vector<SelfTestItem> items;
    REQUIRE(run_self_test(*f.host, f.device, items) == YR_OK);
    std::map<std::string, uint32_t> result;
    for (const SelfTestItem& it : items)
        result[it.name] = it.result;
    for (const char* k : {"module", "rates", "gains", "info_firmware_version", "info_serial_number",
                          "antenna_power", "stream"})
        CHECK_MESSAGE(result.count(k) == 1, k);
    // The receive stream item reads 100000 values in at most 2 s: real
    // time, checked in the timing case below.
    for (const auto& [k, r] : result)
        if (k != "stream")
            CHECK_MESSAGE(r == YR_TEST_PASS, k);
}

TEST_SUITE("timing")
{
TEST_CASE("hackrf: every self-test item passes, the receive stream included (RAD-46)")
{
    Fake f(R"(,"paced":false)");
    f.open();
    std::vector<SelfTestItem> items;
    REQUIRE(run_self_test(*f.host, f.device, items) == YR_OK);
    REQUIRE(!items.empty());
    for (const SelfTestItem& it : items)
        CHECK_MESSAGE(it.result == YR_TEST_PASS, it.name << ": " << it.detail);
}
}

TEST_CASE("hackrf: clipping is flagged with the HackRF remedy (RAD-60, RAD-21)")
{
    Fake f(R"(,"paced":false)");
    f.open();
    // 76 dB above the reference gain: the -40 dBFS cell clips.
    f.set_double("gain_lna", 40.0);
    f.set_double("gain_vga", 62.0);
    f.set_bool("rf_amplifier", true);
    int overloaded = 0;
    f.stream(serving_plan(), [&](const Record& r) {
        if (r.schema_index == 0 && (r.flags & YR_RECORD_OVERLOAD) != 0)
            ++overloaded;
        return overloaded >= 3;
    }, 30.0);
    CHECK(overloaded >= 3);
    CHECK(f.get("clipped_percent", true).f64 > 1.0);
    CHECK(f.get("overload", true).str.find("RF amplifier off") != std::string::npos);
}

TEST_CASE("hackrf: records of the fake HackRF match the generator's truth with offset tuning (RAD-60, RAD-92, RAD-22)")
{
    Fake f(R"(,"paced":false)");
    f.open();
    f.reference_gain();
    const std::map<std::string, double> truth = truth_levels();
    REQUIRE(truth.size() == 2);
    std::map<uint64_t, double> level;
    std::map<uint64_t, uint64_t> la, cc;
    uint32_t groups = 0;
    // The serving cell's neighbour list: per broadcasting carrier, the
    // (cell number, main carrier) entries. Each cell of the fixture lists
    // the other one (A: cell 1, carrier 3700; B: cell 2, carrier 3712).
    std::map<uint64_t, std::set<std::pair<uint64_t, uint64_t>>> lists;
    const int nb_from = f.field(1, YR_FIELD_FREQUENCY_HZ);
    const int nb_cell = f.field(1, "cell_number");
    const int nb_carrier = f.field(1, "main_carrier");
    REQUIRE((nb_from >= 0 && nb_cell >= 0 && nb_carrier >= 0));
    const int c1 = f.field(0, YR_FIELD_C1_DB);
    REQUIRE(c1 >= 0);
    bool any_c1 = false;
    const StreamResult r = f.stream(
        serving_plan(),
        [&](const Record& rec) {
            if (rec.schema_index == 1) {
                const auto at = [&](int i) { return rec.values[static_cast<std::size_t>(i)].u64; };
                lists[at(nb_from)].insert({at(nb_cell), at(nb_carrier)});
            }
            if (!identified(rec))
                return false;
            const uint64_t hz = rec.values[YR_CORE_FREQUENCY_HZ].u64;
            la[hz] = rec.values[YR_CORE_LA].u64;
            cc[hz] = rec.values[YR_CORE_COLOUR_CODE].u64;
            CHECK(rec.values[YR_CORE_MCC].u64 == 901);
            CHECK(rec.values[YR_CORE_MNC].u64 == 1);
            CHECK((rec.flags & YR_RECORD_SIMULATED) == 0);
            level[hz] = rec.values[YR_CORE_LEVEL].f64;
            groups |= rec.present;
            any_c1 = any_c1 || rec.values[static_cast<std::size_t>(c1)].type != YR_TYPE_NONE;
            return la.size() >= 2 && !lists.empty() && (groups & YR_GROUP_QUALITY) != 0 &&
                   (groups & YR_GROUP_BROADCAST) != 0 && (groups & YR_GROUP_FRAME) != 0;
        },
        30.0); // seconds of signal; it needs about 3
    INFO(r.stop_reason);
    CHECK(la[392500000] == 120);
    CHECK(la[392800000] == 120);
    CHECK(cc[392500000] == 1);
    CHECK(cc[392800000] == 2);
    CHECK(std::fabs(level[392500000] - (truth.at("A") + 20.0)) < 1.5);
    CHECK(std::fabs(level[392800000] - (truth.at("B") + 20.0)) < 1.5);
    CHECK((groups & YR_GROUP_QUALITY) != 0);
    CHECK((groups & YR_GROUP_BROADCAST) != 0);
    CHECK((groups & YR_GROUP_FRAME) != 0);
    // The list of the serving cell (A, the stronger) and only the other cell in it.
    REQUIRE(lists.size() == 1);
    CHECK(lists.begin()->first == 392500000);
    CHECK(lists.begin()->second == std::set<std::pair<uint64_t, uint64_t>>{{2, 3712}});
    CHECK_FALSE(any_c1); // C1 only with a calibration offset
    CHECK(r.stats.overflows == 0);
}

TEST_CASE("hackrf: C1 from the dBFS level and a calibration offset (RAD-60, RAD-22)")
{
    Fake f(R"(,"paced":false)");
    f.open();
    f.reference_gain();
    f.set_bool("c1_enabled", true);
    f.set_double("c1_level_offset_db", -20.0); // dBm = dBFS - 20 at the reference gain
    const std::map<std::string, double> truth = truth_levels();
    const int c1 = f.field(0, YR_FIELD_C1_DB);
    const int rxlev = f.field(0, "rxlev_access_min_dbm");
    const int txpwr = f.field(0, "ms_txpwr_max_cell_dbm");
    REQUIRE(c1 >= 0);
    std::map<uint64_t, Record> last;
    f.stream(serving_plan(), [&](const Record& rec) {
        if (identified(rec) && rec.values[static_cast<std::size_t>(c1)].type == YR_TYPE_DOUBLE)
            last[rec.values[YR_CORE_FREQUENCY_HZ].u64] = rec;
        return last.size() >= 2;
    }, 30.0);
    REQUIRE(last.count(392500000) == 1);
    const Record& a = last[392500000];
    CHECK(a.values[YR_CORE_LEVEL].type == YR_TYPE_DOUBLE); // still dBFS
    const double want = radios::tetra_c1_db(truth.at("A"), static_cast<double>(a.values[static_cast<std::size_t>(rxlev)].i64),
                                            static_cast<double>(a.values[static_cast<std::size_t>(txpwr)].i64), 30.0);
    CHECK(std::fabs(a.values[static_cast<std::size_t>(c1)].f64 - want) < 1.5);
    CHECK((a.present & YR_GROUP_C1) != 0);
}

TEST_CASE("hackrf: band scan wider than the span retunes and gives every channel once per pass (RAD-60, RAD-23)")
{
    Fake f(R"(,"paced":false)");
    f.open();
    f.reference_gain();
    f.set_double("scan_dwell_s", 3.5); // long enough for the carriers to lock
    const std::map<std::string, double> truth = truth_levels();
    std::map<uint64_t, Record> pass;
    int64_t done_value = -1;
    int serving = 0, cell_records = 0;
    bool done = false;
    const yr_plan plan = scan_plan(391'000'000, 394'000'000);
    const StreamResult r = f.stream(
        plan,
        [&](const Record& rec) {
            if (rec.schema_index == 2 && !done)
                pass[rec.values[YR_CORE_FREQUENCY_HZ].u64] = rec;
            if (rec.schema_index != 2)
                ++cell_records;
            if (rec.schema_index != 2 && rec.values[YR_CORE_ROLE].i64 == YR_ROLE_SERVING)
                ++serving;
            return done;
        },
        60.0, // seconds of signal; dwells of 3.5 s
        [&](const radios::Event& e) {
            if (e.event == YR_EVENT_SCAN_DONE && !done) {
                done = true;
                done_value = e.value;
            }
            return done;
        });
    INFO(r.stop_reason);
    CHECK(r.invalid == 0);
    REQUIRE(done);
    CHECK(done_value == 121); // 391.000 to 394.000 MHz in 25 kHz
    CHECK(pass.size() == 121);
    CHECK(serving == 0);
    CHECK(cell_records == 0);
    REQUIRE(pass.count(392500000) == 1);
    const Record& a = pass[392500000];
    CHECK(std::fabs(a.values[YR_CORE_LEVEL].f64 - (truth.at("A") + 20.0)) < 2.0);
    CHECK(a.values[YR_CORE_LA].type == YR_TYPE_UINT);
    if (a.values[YR_CORE_LA].type == YR_TYPE_UINT) {
        CHECK(a.values[YR_CORE_LA].u64 == 120);
        CHECK((a.flags & YR_RECORD_NO_SYNC) == 0);
    }
    REQUIRE(pass.count(391500000) == 1);
    CHECK(pass[391500000].values[YR_CORE_LEVEL].f64 < -90.0); // an empty channel
    CHECK((pass[391500000].flags & YR_RECORD_NO_SYNC) != 0);
}

TEST_CASE("hackrf: an averaged FFT of its own samples once a second (SPECTRUM, RAD-140, RAD-60)")
{
    // Paced: real time, as a spectrum is due once a second of host time.
    // The sanitizer trees decode slower than real time: a paced fake
    // overflows there all the time and the decoder never synchronises, so
    // they read the fake unpaced (the spectra still come once a second of
    // host time, the records as fast as the samples are decoded).
    Fake f(YANTRA_TEST_TIME_SCALE == 1 ? "" : R"(,"paced":false)");
    CHECK((f.host->descriptor().capabilities & YR_CAP_SPECTRUM) != 0);
    f.open();
    f.reference_gain();
    yr_plan plan = serving_plan();
    plan.flags = YR_PLAN_SPECTRUM; // default band 380-400 MHz
    std::map<uint64_t, double> level;
    std::vector<Spectrum> spectra;
    std::atomic<bool> stop{false};
    StreamLimits limits;
    limits.max_seconds = 20.0 * YANTRA_TEST_TIME_SCALE; // a hang guard; it needs about 3 s
    limits.interrupt = &stop;
    const StreamResult r = run_stream(
        *f.host, f.device, plan, limits,
        [&](const Record& rec) {
            if (identified(rec))
                level[rec.values[YR_CORE_FREQUENCY_HZ].u64] = rec.values[YR_CORE_LEVEL].f64;
        },
        {}, SinkOptions{},
        [&](const Spectrum& s) {
            spectra.push_back(s);
            if (spectra.size() >= 3 && level.size() >= 2)
                stop = true;
        });
    CHECK(r.invalid == 0);
    for (const std::string& p : r.problems)
        FAIL_CHECK(p);
    REQUIRE(spectra.size() >= 3);
    REQUIRE(level.count(392500000) == 1);
    REQUIRE(level.count(392800000) == 1);
    const double tuned = f.get("tuned_mhz", true).f64 * 1e6;
    for (std::size_t i = 0; i < spectra.size(); ++i) {
        const Spectrum& s = spectra[i];
        CAPTURE(i);
        CHECK(s.flags == 0); // dBFS, a real device, no clipping (RAD-21)
        // 2 Msps: 2048-point FFT, 977 Hz bins; the band within the 1.6 MHz received.
        CHECK(s.bin_width_hz == doctest::Approx(2e6 / 2048.0));
        CHECK(s.start_hz >= tuned - 0.8e6 - s.bin_width_hz);
        CHECK(s.stop_hz <= tuned + 0.8e6 + s.bin_width_hz);
        CHECK(s.power_db.size() <= 2048);
        CHECK(s.power_db.size() >= 1600);
        if (i > 0) {
            const double gap = static_cast<double>(s.monotonic_ns - spectra[i - 1].monotonic_ns) / 1e9;
            CHECK(gap == doctest::Approx(1.0).epsilon(0.25));
        }
        // Both carriers: their bins add up to their records' levels (dBFS).
        for (uint64_t hz : {392500000ull, 392800000ull}) {
            double mw = 0.0;
            for (std::size_t k = 0; k < s.power_db.size(); ++k) {
                const double centre = s.start_hz + (static_cast<double>(k) + 0.5) * s.bin_width_hz;
                if (std::fabs(centre - static_cast<double>(hz)) <= 12.5e3)
                    mw += std::pow(10.0, s.power_db[k] / 10.0);
            }
            CHECK(10.0 * std::log10(mw) == doctest::Approx(level.at(hz)).epsilon(0.05));
        }
    }
}

TEST_CASE("hackrf: plans the device cannot measure are refused with the reason (RAD-32, RAD-23)")
{
    Fake f;
    f.open();
    StreamSink sink(*f.host);
    yr_plan bad = scan_plan(394'000'000, 391'000'000);
    CHECK(f.host->descriptor().stream->start_stream(f.device, &bad, sink.handle()) == YR_ERR_INVALID_ARGUMENT);
    CHECK(f.host->last_error(f.device).find("scan band") != std::string::npos);
    bad = scan_plan(391'000'000, 394'000'000);
    bad.scan_step_hz = 12'500;
    CHECK(f.host->descriptor().stream->start_stream(f.device, &bad, sink.handle()) == YR_ERR_INVALID_ARGUMENT);
}

TEST_CASE("hackrf: overflows give OVERFLOW and DROPPED events (RAD-24, RAD-25)")
{
    Fake f(R"(,"paced":false,"overflow_at":3000000)");
    f.open();
    std::atomic<int> overflow{0}, dropped{0};
    std::atomic<int64_t> lost{0};
    int records = 0;
    const StreamResult r = f.stream(
        serving_plan(), [&](const Record&) { return dropped > 0 && ++records > 4; }, 30.0,
        [&](const radios::Event& e) {
            if (e.event == YR_EVENT_OVERFLOW)
                ++overflow;
            if (e.event == YR_EVENT_DROPPED) {
                ++dropped;
                lost += e.value;
            }
            return false;
        });
    CHECK(overflow == 1);
    CHECK(dropped == 1);
    CHECK(lost == 20000); // 10 ms at 2 Msps
    CHECK(r.stats.overflows == 1);
    CHECK(r.stats.samples_dropped == 20000);
}

TEST_CASE("hackrf: unplugged while streaming, then replugged (RAD-35, RAD-34)")
{
    Fake f(R"(,"paced":false)");
    f.open();
    std::atomic<bool> lost{false};
    bool written = false;
    int records = 0;
    const StreamResult r = f.stream(
        serving_plan(),
        [&](const Record&) {
            if (++records == 3 && !written) {
                std::ofstream(f.unplugged()) << "";
                written = true;
            }
            return false;
        },
        30.0, [&](const radios::Event& e) {
            if (e.event == YR_EVENT_DEVICE_LOST)
                lost = true;
            return false;
        });
    CHECK(lost);
    CHECK(r.stop_reason == "device_lost");
    CHECK(f.state() == YR_DEVICE_LOST);
    fs::remove(f.unplugged());
    REQUIRE(wait_for([&] { return f.state() == YR_DEVICE_OPEN; }, 10.0));
    int n = 0;
    f.stream(serving_plan(), [&](const Record&) { return ++n >= 2; }, 20.0);
    CHECK(n >= 2);
}
