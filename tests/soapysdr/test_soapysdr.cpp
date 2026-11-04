// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shared SoapySDR layer and the generic plugin (RAD-54, RAD-62)
// against the fake SoapySDR module (RAD-92): enumeration with stable
// keys and module versions (RAD-30, RAD-52), settings generated from the
// module (RAD-51, RAD-53), records that match the generator's truth,
// overflow and drop events (RAD-24, RAD-25), hot-plug (RAD-35).
#include <doctest/doctest.h>

#include <yantra/gen/scenario.hpp>
#include <yantra/gen/scenario_engine.hpp>

#include "host.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>

#include <unistd.h>
#include <vector>

using namespace yantra;
using namespace yantra::radio_host;
using radios::Record;
namespace fs = std::filesystem;

namespace {

const char* const kSerial = "0000000000000000a1b2c3d4e5f60001";
const std::string kKey = std::string("yantrafake:") + kSerial;

std::string fixture() { return std::string(YANTRA_TEST_DATA_DIR) + "/soapy/fake_two_cells.json"; }

// A control folder of its own per fake (the cases run in parallel).
std::string control_dir()
{
    static int n = 0;
    const fs::path p = fs::path(YANTRA_BINARY_DIR) / "tests" / "soapysdr_control" /
                       (std::to_string(getpid()) + "_" + std::to_string(++n));
    fs::remove_all(p);
    fs::create_directories(p);
    return p.string();
}

void configure_fake(const std::string& control, const std::string& extra = "")
{
    const std::string json = "{\"control\":\"" + control + "\",\"devices\":[{\"serial\":\"" + kSerial +
                             "\",\"scenario\":\"" + fixture() + "\"" + extra + "}]}";
    REQUIRE(setenv("YANTRA_FAKE_SOAPY", json.c_str(), 1) == 0);
}

// Writes a control file of the fake module atomically (write, then
// rename): the module polls the folder from its stream thread and must
// never see the file before its content (an empty .overflow file reads
// as 1 overflow instead of 2).
void touch(const std::string& path, const std::string& content = "")
{
    const std::string tmp = path + ".tmp";
    std::ofstream(tmp) << content;
    fs::rename(tmp, path);
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
        configure_fake(control, extra);
        std::string error;
        host = Host::load(SOAPYSDR_FAKE_PLUGIN, HostConfig{}, error);
        REQUIRE_MESSAGE(host != nullptr, error);
        REQUIRE(host->descriptor_problems().empty());
    }
    ~Fake()
    {
        if (device != nullptr)
            host->descriptor().stream->close(device);
        host.reset();
        unsetenv("YANTRA_FAKE_SOAPY");
        std::error_code ec;
        fs::remove_all(control, ec);
    }
    void open()
    {
        REQUIRE_MESSAGE(host->descriptor().stream->open(host->plugin(), kKey.c_str(), &device) == YR_OK,
                        host->last_error(nullptr));
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
    std::string unplugged() const { return control + "/" + kSerial + ".unplugged"; }
    uint32_t state() { return host->descriptor().stream->state(device); }

    StreamResult stream(const yr_plan& plan, const std::function<bool(const Record&)>& on_record,
                        double seconds, const std::function<void(const radios::Event&)>& on_event = {})
    {
        std::atomic<bool> stop{false};
        StreamLimits limits;
        limits.max_seconds = seconds * YANTRA_TEST_TIME_SCALE;
        limits.interrupt = &stop;
        return run_stream(
            *host, device, plan, limits,
            [&](const Record& r) {
                if (on_record(r))
                    stop = true;
            },
            [&](const radios::Event& e) {
                if (on_event)
                    on_event(e);
            });
    }
};

yr_plan serving_plan()
{
    yr_plan plan{};
    plan.struct_size = sizeof plan;
    plan.type = YR_PLAN_SERVING;
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

} // namespace

TEST_CASE("soapysdr: the fake device enumerates with a stable key and module version (RAD-30, RAD-52)")
{
    Fake f;
    std::vector<radios::DeviceInfo> devices;
    std::string diagnostic;
    REQUIRE(enumerate_devices(*f.host, devices, diagnostic) == YR_OK);
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].key == kKey);
    CHECK(devices[0].driver == "yantrafake");
    CHECK(devices[0].serial == kSerial);
    CHECK(devices[0].module_version == "yantra fake module (test) 0.3.6-fake (yantra-gen)");
    f.open();
    std::string hw;
    f.host->descriptor().stream->device_info(
        f.device, [](void* u, const yr_device_info* i) { *static_cast<std::string*>(u) = i->hardware_info; }, &hw);
    CHECK(hw.find("hardware=HackRF One") != std::string::npos);
    CHECK(hw.find("clock source=internal") != std::string::npos);

    // Unplugged: no device, and the diagnostic names the module.
    touch(f.unplugged());
    devices.clear();
    yr_device* other = nullptr;
    CHECK(f.host->descriptor().stream->open(f.host->plugin(), kKey.c_str(), &other) == YR_ERR_BUSY);
    f.host->descriptor().stream->close(f.device);
    f.device = nullptr;
    CHECK(enumerate_devices(*f.host, devices, diagnostic) == YR_ERR_NO_DEVICE);
    CHECK(diagnostic.find("yantra fake module (test) 0.3.6-fake") != std::string::npos);
    CHECK(diagnostic.find("udev") != std::string::npos);
}

TEST_CASE("soapysdr: settings come from the module (RAD-51, RAD-53)")
{
    SUBCASE("a HackRF-like device")
    {
        Fake f;
        f.open();
        auto s = f.settings();
        REQUIRE(s.count("gain_lna") == 1);
        CHECK(s["gain_lna"].minimum == 0.0);
        CHECK(s["gain_lna"].maximum == 40.0);
        CHECK(s["gain_lna"].step == 8.0);
        CHECK((s["gain_lna"].flags & YR_SETTING_CALIBRATION) != 0);
        CHECK(s["gain_vga"].maximum == 62.0);
        CHECK(s["gain_vga"].step == 2.0);
        CHECK(s["gain_amp"].maximum == 14.0);
        CHECK(s["gain_amp"].default_value.f64 == 0.0); // the amplifier starts off
        CHECK((s["agc"].flags & YR_SETTING_UNAVAILABLE) != 0);
        CHECK((s["antenna"].flags & YR_SETTING_UNAVAILABLE) != 0);
        CHECK((s["clock_source"].flags & YR_SETTING_UNAVAILABLE) != 0);
        CHECK(s["bandwidth"].items.size() == 17); // automatic + 16 filters
        CHECK(s["receiver_rate"].items.size() == 10); // 1 to 10 Msps of 1-20
        REQUIRE(s.count("arg_bias_tx") == 1);
        CHECK(s["arg_bias_tx"].type == YR_TYPE_BOOL);
        CHECK((s["arg_bias_tx"].flags & YR_SETTING_WARNING) != 0);
        Value v;
        v.type = YR_TYPE_BOOL;
        v.i64 = 1;
        CHECK(write_value(*f.host, f.device, "agc", v) != YR_OK); // unavailable
    }
    SUBCASE("an external reference, AGC-less sensors and a frequency correction")
    {
        Fake f(R"(,"clock_sources":["internal","external"],"sensors":{"board temp":"41.5"},"frequency_correction":true)");
        f.open();
        auto s = f.settings();
        REQUIRE((s["clock_source"].flags & YR_SETTING_UNAVAILABLE) == 0);
        CHECK(s["clock_source"].items.size() == 2);
        CHECK(s["clock_source"].items[1].name == "external");
        auto sensors = f.settings(true);
        REQUIRE(sensors.count("sensor_board_temp") == 1);
        Value v;
        REQUIRE(read_value(*f.host, f.device, "sensor_board_temp", true, v) == YR_OK);
        CHECK(v.str == "41.5");
        REQUIRE(read_value(*f.host, f.device, "module", true, v) == YR_OK);
        CHECK(v.str.find("0.3.6-fake") != std::string::npos);
    }
}

TEST_CASE("soapysdr: records of the fake device match the generator's truth (RAD-62, RAD-92)")
{
    // A device buffer the loaded test machine never overruns.
    Fake f(R"(,"paced":false)"); // values, not timing: no natural overflow
    f.open();
    // Total gain 40 dB: the fake's reference gain, so dBFS = dBm + 20.
    f.set_double("gain_lna", 16.0);
    f.set_double("gain_vga", 24.0);
    f.set_double("gain_amp", 0.0);
    const std::map<std::string, double> truth = truth_levels();
    REQUIRE(truth.size() == 2);
    std::map<uint64_t, std::vector<double>> levels;
    std::map<uint64_t, uint64_t> la;
    const StreamResult r = f.stream(
        serving_plan(),
        [&](const Record& rec) {
            if (rec.schema_index != 0 || rec.values[YR_CORE_LEVEL].type != YR_TYPE_DOUBLE)
                return false;
            const uint64_t hz = rec.values[YR_CORE_FREQUENCY_HZ].u64;
            if (rec.values[YR_CORE_LA].type == YR_TYPE_UINT) {
                la[hz] = rec.values[YR_CORE_LA].u64;
                CHECK(rec.values[YR_CORE_MCC].u64 == 901);
                CHECK(rec.values[YR_CORE_MNC].u64 == 1);
                levels[hz].push_back(rec.values[YR_CORE_LEVEL].f64);
            }
            return levels[392500000].size() >= 5 && levels[392800000].size() >= 5;
        },
        30.0);
    INFO(r.stop_reason);
    REQUIRE(levels[392500000].size() >= 5);
    REQUIRE(levels[392800000].size() >= 5);
    CHECK(la[392500000] == 120);
    CHECK(la[392800000] == 120);
    auto last = [](const std::vector<double>& v) { return v.back(); };
    CHECK(std::fabs(last(levels[392500000]) - (truth.at("A") + 20.0)) < 1.5);
    CHECK(std::fabs(last(levels[392800000]) - (truth.at("B") + 20.0)) < 1.5);
    CHECK(r.stats.overflows == 0);
}

TEST_CASE("soapysdr: gain changes move the dBFS levels (RAD-51)")
{
    Fake f(R"(,"paced":false)"); // no overflow on a loaded machine
    f.open();
    f.set_double("gain_lna", 16.0);
    f.set_double("gain_vga", 14.0);
    auto level_of_a = [&f]() {
        double level = 0.0;
        int n = 0;
        f.stream(
            serving_plan(),
            [&](const Record& rec) {
                if (rec.schema_index == 0 && rec.values[YR_CORE_FREQUENCY_HZ].u64 == 392500000 &&
                    rec.values[YR_CORE_LA].type == YR_TYPE_UINT &&
                    rec.values[YR_CORE_LEVEL].type == YR_TYPE_DOUBLE) {
                    level = rec.values[YR_CORE_LEVEL].f64;
                    ++n;
                }
                return n >= 4;
            },
            30.0);
        REQUIRE(n >= 4);
        return level;
    };
    const double low = level_of_a();
    f.set_double("gain_vga", 24.0);
    const double high = level_of_a();
    CHECK(std::fabs(high - low - 10.0) < 1.0);
}

TEST_CASE("soapysdr: overflows give OVERFLOW and DROPPED events (RAD-24, RAD-25)")
{
    Fake f(R"(,"paced":false)"); // only the injected overflows
    f.open();
    std::atomic<int> overflow{0}, dropped{0};
    std::atomic<int64_t> lost{0};
    bool written = false;
    int records = 0;
    const StreamResult r = f.stream(
        serving_plan(),
        [&](const Record&) {
            if (++records == 2 && !written) {
                touch(f.control + "/" + kSerial + ".overflow", "2");
                written = true;
            }
            return dropped > 0 && records > 4;
        },
        30.0,
        [&](const radios::Event& e) {
            if (e.event == YR_EVENT_OVERFLOW)
                ++overflow;
            if (e.event == YR_EVENT_DROPPED) {
                ++dropped;
                lost += e.value;
            }
        });
    CHECK(overflow == 2);
    CHECK(dropped >= 1);
    CHECK(lost >= 20000); // 2 x 10 ms at 2 Msps
    CHECK(r.stats.overflows == 2);
    CHECK(r.stats.samples_dropped == static_cast<uint64_t>(lost.load()));
}

TEST_CASE("soapysdr: hot-plug of an open device and a refused open (RAD-35, RAD-34)")
{
    SUBCASE("unplug and replug while open")
    {
        Fake f(R"(,"paced":false)"); // the records after the replug, not timing
        f.open();
        const uint64_t changes = f.host->devices_changed_count();
        touch(f.unplugged());
        CHECK(wait_for([&] { return f.state() == YR_DEVICE_LOST; }, 10.0));
        CHECK(wait_for([&] { return f.host->devices_changed_count() >= changes + 1; }, 10.0));
        const yr_plan plan = serving_plan();
        StreamSink sink(*f.host);
        CHECK(f.host->descriptor().stream->start_stream(f.device, &plan, sink.handle()) == YR_ERR_DEVICE_LOST);
        fs::remove(f.unplugged());
        CHECK(wait_for([&] { return f.state() == YR_DEVICE_OPEN; }, 10.0));
        CHECK(wait_for([&] { return f.host->devices_changed_count() >= changes + 2; }, 10.0));
        int n = 0;
        f.stream(plan, [&](const Record&) { return ++n >= 2; }, 20.0);
        CHECK(n >= 2);
    }
    SUBCASE("the module refuses to open")
    {
        Fake f(R"x(,"open_error":"USB busy (fake)")x");
        yr_device* d = nullptr;
        CHECK(f.host->descriptor().stream->open(f.host->plugin(), kKey.c_str(), &d) == YR_ERR_IO);
        CHECK(f.host->last_error(nullptr).find("USB busy (fake)") != std::string::npos);
    }
}
