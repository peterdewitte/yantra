// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SoapyRemote in the generic plugin (RAD-50 network radios, RAD-62,
// RAD-92): the installed soapysdr plugin lists the devices of the
// SoapySDRServers named in YANTRA_SOAPY_REMOTE and measures through them;
// the server here is SoapyRemote's own server side on the loopback,
// serving the fake module, so records cross the network and still match
// the generator's truth. Without the variable no network radio is listed
// and the diagnostic says how to name a server.
#include <doctest/doctest.h>

#include <yantra/gen/scenario.hpp>
#include <yantra/gen/scenario_engine.hpp>

#include "host.hpp"
#include "remote_server.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <unistd.h>

using namespace yantra;
using namespace yantra::radio_host;
using radios::Record;
namespace fs = std::filesystem;

namespace {

const char* const kSerial = "0000000000000000a1b2c3d4e5f60001";
const std::string kKey = std::string("remote:") + kSerial;

std::string fixture() { return std::string(YANTRA_TEST_DATA_DIR) + "/soapy/fake_two_cells.json"; }

struct Rig {
    std::string control;
    tests::RemoteServer server;
    std::unique_ptr<Host> host;
    yr_device* device = nullptr;

    explicit Rig(bool with_servers = true)
    {
        static int n = 0;
        const fs::path dir = fs::path(YANTRA_BINARY_DIR) / "tests" / "soapy_remote_control" /
                             (std::to_string(getpid()) + "_" + std::to_string(++n));
        fs::remove_all(dir);
        fs::create_directories(dir);
        control = dir.string();
        // The fake (in this process, behind the server); unpaced: values,
        // not timing, and SoapyRemote's flow control paces the network.
        const std::string fake = "{\"control\":\"" + control + "\",\"devices\":[{\"serial\":\"" + kSerial +
                                 "\",\"scenario\":\"" + fixture() + "\",\"paced\":false}]}";
        REQUIRE(setenv("YANTRA_FAKE_SOAPY", fake.c_str(), 1) == 0);
        if (with_servers)
            REQUIRE(setenv("YANTRA_SOAPY_REMOTE", server.url().c_str(), 1) == 0);
        else
            REQUIRE(unsetenv("YANTRA_SOAPY_REMOTE") == 0);
        std::string error;
        host = Host::load(SOAPYSDR_PLUGIN, HostConfig{}, error);
        REQUIRE_MESSAGE(host != nullptr, error);
        REQUIRE(host->descriptor_problems().empty());
    }
    ~Rig()
    {
        if (device != nullptr)
            host->descriptor().stream->close(device);
        host.reset();
        server.stop();
        unsetenv("YANTRA_SOAPY_REMOTE");
        unsetenv("YANTRA_FAKE_SOAPY");
        std::error_code ec;
        fs::remove_all(control, ec);
    }
    void set_double(const std::string& key, double x)
    {
        Value v;
        v.type = YR_TYPE_DOUBLE;
        v.f64 = x;
        REQUIRE_MESSAGE(write_value(*host, device, key, v) == YR_OK, host->last_error(device));
    }
};

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

TEST_CASE("remote: the devices of a SoapySDRServer are listed with the module version (RAD-50, RAD-52)")
{
    Rig rig;
    std::vector<radios::DeviceInfo> devices;
    std::string diagnostic;
    REQUIRE(enumerate_devices(*rig.host, devices, diagnostic) == YR_OK);
    std::vector<radios::DeviceInfo> remote;
    for (const auto& d : devices)
        if (d.driver == "remote")
            remote.push_back(d);
    REQUIRE(remote.size() == 1);
    CHECK(remote[0].key == kKey);
    CHECK(remote[0].serial == kSerial);
    CHECK(remote[0].module_version == "SoapyRemote 0.5.3 (network client)");

    REQUIRE(rig.host->descriptor().stream->open(rig.host->plugin(), kKey.c_str(), &rig.device) == YR_OK);
    std::string hw;
    rig.host->descriptor().stream->device_info(
        rig.device, [](void* u, const yr_device_info* i) { *static_cast<std::string*>(u) = i->hardware_info; },
        &hw);
    CHECK(hw.find("HackRF One") != std::string::npos); // forwarded by the server

    // The server gone: the device is no longer listed, nothing fails.
    rig.host->descriptor().stream->close(rig.device);
    rig.device = nullptr;
    rig.server.stop();
    devices.clear();
    enumerate_devices(*rig.host, devices, diagnostic);
    for (const auto& d : devices)
        CHECK(d.driver != "remote");
    CHECK(diagnostic.find("network radio servers: " + rig.server.url()) != std::string::npos);
}

TEST_CASE("remote: without YANTRA_SOAPY_REMOTE no network radio is listed (RAD-50, RAD-52)")
{
    Rig rig(false);
    std::vector<radios::DeviceInfo> devices;
    std::string diagnostic;
    enumerate_devices(*rig.host, devices, diagnostic);
    for (const auto& d : devices)
        CHECK(d.driver != "remote");
    CHECK(diagnostic.find("SoapyRemote 0.5.3") != std::string::npos);
    CHECK(diagnostic.find("set YANTRA_SOAPY_REMOTE") != std::string::npos);
}

TEST_CASE("remote: records over the network match the generator's truth (RAD-50, RAD-62, RAD-92)")
{
    Rig rig;
    REQUIRE(rig.host->descriptor().stream->open(rig.host->plugin(), kKey.c_str(), &rig.device) == YR_OK);
    // Total gain 40 dB, the fake's reference gain: dBFS = dBm + 20.
    rig.set_double("gain_lna", 16.0);
    rig.set_double("gain_vga", 24.0);
    rig.set_double("gain_amp", 0.0);
    const std::map<std::string, double> truth = truth_levels();
    REQUIRE(truth.size() == 2);
    std::map<uint64_t, std::vector<double>> levels;
    std::atomic<bool> stop{false};
    StreamLimits limits;
    limits.max_seconds = 60.0 * YANTRA_TEST_TIME_SCALE;
    limits.interrupt = &stop;
    yr_plan plan{};
    plan.struct_size = sizeof plan;
    plan.type = YR_PLAN_SERVING;
    const StreamResult r = run_stream(
        *rig.host, rig.device, plan, limits,
        [&](const Record& rec) {
            if (rec.schema_index != 0 || rec.values[YR_CORE_LEVEL].type != YR_TYPE_DOUBLE ||
                rec.values[YR_CORE_LA].type != YR_TYPE_UINT)
                return;
            CHECK(rec.values[YR_CORE_MCC].u64 == 901);
            CHECK(rec.values[YR_CORE_LA].u64 == 120);
            levels[rec.values[YR_CORE_FREQUENCY_HZ].u64].push_back(rec.values[YR_CORE_LEVEL].f64);
            if (levels[392500000].size() >= 5 && levels[392800000].size() >= 5)
                stop = true;
        },
        [](const radios::Event&) {});
    INFO(r.stop_reason);
    REQUIRE(levels[392500000].size() >= 5);
    REQUIRE(levels[392800000].size() >= 5);
    CHECK(std::fabs(levels[392500000].back() - (truth.at("A") + 20.0)) < 1.5);
    CHECK(std::fabs(levels[392800000].back() - (truth.at("B") + 20.0)) < 1.5);
}
