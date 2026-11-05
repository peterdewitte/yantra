// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-hackrf (RAD-66): every command against the fake HackRF (RAD-92),
// with --json, the exit codes and the --bias-tee confirmation.
#include <doctest/doctest.h>

#include "hackrf_app.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

using namespace yantra::hackrf_tool;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const char* const kSerial = "0000000000000000a1b2c3d4e5f60001";

std::string fixture() { return std::string(YANTRA_TEST_DATA_DIR) + "/soapy/fake_two_cells.json"; }

// A limit that only guards against a hang (slower in sanitizer trees).
std::string guard(double seconds) { return std::to_string(seconds * YANTRA_TEST_TIME_SCALE); }

struct Result {
    int code = -1;
    std::string out, err;
    json j;
};

struct Fake {
    fs::path control;
    explicit Fake(const std::string& extra = R"(,"paced":false)", bool device = true)
    {
        static int n = 0;
        control = fs::path(YANTRA_BINARY_DIR) / "tests" / "yantra_hackrf_control" /
                  (std::to_string(getpid()) + "_" + std::to_string(++n));
        fs::remove_all(control);
        fs::create_directories(control);
        const std::string devices =
            device ? "{\"serial\":\"" + std::string(kSerial) + "\",\"scenario\":\"" + fixture() + "\"" + extra + "}" : "";
        const std::string cfg = "{\"control\":\"" + control.string() + "\",\"devices\":[" + devices + "]}";
        REQUIRE(setenv("YANTRA_FAKE_SOAPY", cfg.c_str(), 1) == 0);
    }
    ~Fake()
    {
        unsetenv("YANTRA_FAKE_SOAPY");
        std::error_code ec;
        fs::remove_all(control, ec);
    }
    std::string file(const char* suffix) const { return (control / (std::string(kSerial) + suffix)).string(); }

    Result cli(std::vector<std::string> args, bool plugin = true, const std::string& input = "") const
    {
        if (plugin) {
            args.push_back("--plugin");
            args.push_back(HACKRF_FAKE_PLUGIN);
        }
        std::ostringstream out, err;
        std::istringstream in(input);
        Environment env;
        env.out = &out;
        env.err = &err;
        env.in = &in;
        Result r;
        r.code = run(args, env);
        r.out = out.str();
        r.err = err.str();
        if (std::find(args.begin(), args.end(), "--json") != args.end())
            r.j = json::parse(r.out);
        return r;
    }
};

} // namespace

TEST_CASE("yantra-hackrf: usage errors exit 3, --help 0 (RAD-66)")
{
    Fake f;
    CHECK(f.cli({}, false).code == kUsage);
    CHECK(f.cli({"frobnicate"}).code == kUsage);
    CHECK(f.cli({"measure", "--duration"}).code == kUsage);
    CHECK(f.cli({"measure", "--plan", "carriers"}).code == kUsage);
    CHECK(f.cli({"level", "--frequency", "392.5M"}).code == kUsage);
    const Result h = f.cli({"--help"}, false);
    CHECK(h.code == kPass);
    CHECK(h.out.find("exit codes: 0 pass, 1 fail, 2 no device, 3 usage error") != std::string::npos);
    const Result bad = f.cli({"selftest", "--plugin", "/nonexistent/hackrf.so"}, false);
    CHECK(bad.code == kFail);
    CHECK(bad.err.find("cannot load the HackRF plugin") != std::string::npos);
}

TEST_CASE("yantra-hackrf: no HackRF exits 2 with the udev hint (RAD-66)")
{
    Fake f("", false);
    for (const char* c : {"list", "info", "selftest", "measure", "replug"}) {
        const Result r = f.cli({c, "--json"});
        CHECK_MESSAGE(r.code == kNoDevice, c);
        CHECK(r.j["result"] == "no_device");
        CHECK(r.j["error"].get<std::string>().find("udev") != std::string::npos);
    }
}

TEST_CASE("yantra-hackrf: list and info (RAD-66, RAD-136)")
{
    Fake f;
    const Result l = f.cli({"list", "--json"});
    REQUIRE(l.code == kPass);
    REQUIRE(l.j["devices"].size() == 1);
    CHECK(l.j["devices"][0]["key"] == std::string("hackrf:") + kSerial);
    CHECK(l.j["devices"][0]["board"] == "HackRF One");
    CHECK(l.j["devices"][0]["firmware"] == "2024.02.1");
    CHECK(l.j["devices"][0].contains("usb_speed"));
    const Result lt = f.cli({"list"});
    CHECK(lt.out.find("board HackRF One, firmware 2024.02.1") != std::string::npos);

    const Result i = f.cli({"info", "--json"});
    REQUIRE(i.code == kPass);
    CHECK(i.j["plugin"]["id"] == "hackrf_fake");
    CHECK(i.j["sensors"]["serial_number"] == kSerial);
    CHECK(i.j["sensors"]["build_info"].get<std::string>().find("SoapySDR 0.8.1") != std::string::npos);
    CHECK_FALSE(i.j["sensors"].contains("antenna_power")); // a setting, not a sensor
    bool soapy = false;
    for (const json& b : i.j["build"])
        soapy = soapy || (b["name"] == "SoapySDR" && b["licence"] == "BSL-1.0");
    CHECK(soapy);
    CHECK(f.cli({"info"}).out.find("Build info (build_info)") != std::string::npos);
}

TEST_CASE("yantra-hackrf: selftest and the --bias-tee confirmation (RAD-66, RAD-46, RAD-40)")
{
    Fake f;
    const Result s = f.cli({"selftest", "--json"});
    CHECK(s.code == kPass);
    REQUIRE(!s.j["items"].empty());
    for (const json& it : s.j["items"])
        CHECK_MESSAGE(it["result"] == "pass", it.dump());
    // Antenna port power needs the confirmation.
    const Result refused = f.cli({"selftest", "--set", "antenna_power=true"});
    CHECK(refused.code == kUsage);
    CHECK(refused.err.find("--bias-tee") != std::string::npos);
    CHECK(f.cli({"settings", "set", "antenna_power=on"}).code == kUsage);
    const Result on = f.cli({"selftest", "--json", "--bias-tee", "--set", "antenna_power=true"});
    CHECK(on.code == kPass); // a warning, not a failure
    CHECK(on.j["set"]["antenna_power"] == true);
    bool warned = false;
    for (const json& it : on.j["items"])
        if (it["name"] == "antenna_power")
            warned = it["result"] == "warn" && !it["remedy"].get<std::string>().empty();
    CHECK(warned);
    CHECK(f.cli({"selftest"}).out.find("remedy:") == std::string::npos);
}

TEST_CASE("yantra-hackrf: settings list, get, set with read-back, presets (RAD-66, RAD-41)")
{
    Fake f;
    const Result l = f.cli({"settings", "--json"});
    REQUIRE(l.code == kPass);
    bool power = false;
    for (const json& s : l.j["settings"])
        if (s["key"] == "antenna_power")
            power = s["value"] == false && s.contains("warning");
    CHECK(power);
    const Result g = f.cli({"settings", "get", "gain_lna", "rf_amplifier", "--json"});
    REQUIRE(g.code == kPass);
    CHECK(g.j["settings"].size() == 2);
    CHECK(f.cli({"settings", "get", "no_such_key"}).code == kUsage);
    const Result s = f.cli({"settings", "set", "gain_lna=24", "rf_amplifier=true", "--json"});
    REQUIRE(s.code == kPass);
    CHECK(s.j["read_back"]["gain_lna"] == 24.0);
    CHECK(s.j["read_back"]["rf_amplifier"] == true);
    CHECK(f.cli({"settings", "set", "gain_lna=24"}).out.find("gain_lna = 24 (read back)") != std::string::npos);
    CHECK(f.cli({"settings", "set", "gain_lna=abc"}).code == kUsage);
    const Result bias = f.cli({"settings", "set", "antenna_power=true", "--bias-tee", "--json"});
    CHECK(bias.code == kPass);
    CHECK(bias.j["read_back"]["antenna_power"] == true);

    const Result pl = f.cli({"settings", "preset", "--json"});
    REQUIRE(pl.code == kPass);
    REQUIRE(pl.j["presets"].size() == 2);
    CHECK(pl.j["presets"][0]["id"] == "indoor_sensitive");
    const Result p = f.cli({"settings", "preset", "indoor_sensitive", "--json"});
    REQUIRE(p.code == kPass);
    std::map<std::string, json> back;
    for (const json& e : p.j["settings"])
        back[e["key"]] = e["value"];
    CHECK(back["rf_amplifier"] == true);
    CHECK(back["gain_lna"] == 32.0);
    CHECK(back["gain_vga"] == 30.0);
    CHECK(f.cli({"settings", "preset", "bogus"}).code == kUsage);
    CHECK(f.cli({"settings", "frob"}).code == kUsage);
}

TEST_CASE("yantra-hackrf: measure serving, carriers and scan, to the terminal and a record file (RAD-66, RAD-32, RAD-65)")
{
    Fake f;
    const Result s = f.cli({"measure", "--records", "4", "--duration", guard(60), "--json"});
    REQUIRE(s.code == kPass);
    CHECK(s.j["records"].size() == 4);
    CHECK(s.j["stream"]["invalid"] == 0);
    CHECK(s.j["records"][0]["schema"] == "tetra_cell");

    const Result c = f.cli({"measure", "--plan", "carriers", "--carrier", "392.8M", "--records", "3",
                            "--duration", guard(60)});
    CHECK(c.code == kPass);
    CHECK(c.out.find("392.8000 MHz") != std::string::npos);

    const Result sc = f.cli({"measure", "--plan", "scan", "--first", "392.45M", "--last", "392.55M",
                             "--records", "5", "--duration", guard(60), "--json", "--set", "scan_dwell_s=0.5"});
    REQUIRE(sc.code == kPass);
    for (const json& r : sc.j["records"])
        CHECK(r["schema"] == "tetra_scan");

    const std::string out = (f.control / "m.yrec").string();
    const Result w = f.cli({"measure", "--records", "3", "--duration", guard(60), "--out", out});
    CHECK(w.code == kPass);
    std::ifstream in(out);
    std::string first;
    std::getline(in, first);
    CHECK(first.find("yantra-hackrf") != std::string::npos);
    CHECK(f.cli({"measure", "--plan", "scan", "--first", "394M", "--last", "391M"}).code == kUsage);
}

TEST_CASE("yantra-hackrf: capture IQ to SigMF (RAD-66, RAD-63)")
{
    Fake f;
    const std::string out = (f.control / "c.sigmf-data").string();
    const Result r = f.cli({"capture", "--out", out, "--duration", "0.5", "--rate", "2M", "--json"});
    REQUIRE_MESSAGE(r.code == kPass, std::string(r.out + r.err));
    CHECK(r.j["capture"]["samples"].get<uint64_t>() >= 900'000);
    CHECK(fs::exists(f.control / "c.sigmf-meta"));
    CHECK(fs::file_size(out) == 4 * r.j["capture"]["samples"].get<uint64_t>());
    CHECK(f.cli({"capture"}).code == kUsage);
}

TEST_CASE("yantra-hackrf: noise floor per gain (RAD-66)")
{
    Fake f;
    const Result r = f.cli({"noise", "--gains", "16/24,32/24", "--duration", guard(60), "--json", "--no-prompt"});
    REQUIRE_MESSAGE(r.code == kPass, std::string(r.out + r.err));
    REQUIRE(r.j["points"].size() == 2);
    const double a = r.j["points"][0]["noise_floor_dbfs"].get<double>();
    const double b = r.j["points"][1]["noise_floor_dbfs"].get<double>();
    CHECK(a > -112.0);
    CHECK(a < -95.0);
    CHECK(std::fabs(b - a - 16.0) < 3.0); // the fake's noise follows the gain
    CHECK(f.cli({"noise", "--gains", "16"}).code == kUsage);
}

TEST_CASE("yantra-hackrf: level linearity with the level steps (RAD-66, RAD-125)")
{
    Fake f;
    // The fake's level control file plays the signal generator.
    const std::string step = "printf %s {dbm} > " + f.file(".level_offset");
    const Result r = f.cli({"level", "--frequency", "392.5M", "--levels", "-20,-10,0", "--step-cmd", step,
                            "--duration", guard(60), "--json"});
    REQUIRE_MESSAGE(r.code == kPass, std::string(r.out + r.err));
    REQUIRE(r.j["steps"].size() == 3);
    CHECK(r.j["worst_error_db"].get<double>() < 1.0);
    // Without the steps the level does not follow: a linearity failure.
    fs::remove(f.file(".level_offset"));
    const Result flat = f.cli({"level", "--frequency", "392.5M", "--levels", "-20,0", "--duration", guard(60),
                               "--no-prompt", "--json"});
    CHECK(flat.code == kFail);
    CHECK(flat.j["worst_error_db"].get<double>() > 5.0);
    // Guided: one Enter per level; no answer is a failure.
    const Result guided = f.cli({"level", "--frequency", "392.5M", "--levels", "-10", "--duration", guard(60)},
                                true, "\n");
    CHECK(guided.code == kPass);
    CHECK(guided.err.find("Set the signal generator to -10.0 dBm at 392.5000 MHz.") != std::string::npos);
    CHECK(f.cli({"level", "--frequency", "392.5M", "--levels", "-10,-5", "--duration", guard(60)}, true, "\n").code ==
          kFail);
}

TEST_CASE("yantra-hackrf: frequency error of a known carrier (RAD-66)")
{
    Fake f;
    const Result r = f.cli({"ppm", "--frequency", "392.5M", "--duration", guard(60), "--json"});
    REQUIRE_MESSAGE(r.code == kPass, std::string(r.out + r.err));
    CHECK(std::fabs(r.j["error_ppm"].get<double>()) < 1.0);
    CHECK(f.cli({"ppm", "--frequency", "392.5M", "--max-ppm", "-1", "--duration", guard(60)}).code == kFail);
    CHECK(f.cli({"ppm"}).code == kUsage);
}

TEST_CASE("yantra-hackrf: throughput per sample rate (RAD-66, RAD-24)")
{
    SUBCASE("no losses")
    {
        Fake f;
        const Result r = f.cli({"throughput", "--rates", "2,4", "--duration", "2", "--json"});
        REQUIRE_MESSAGE(r.code == kPass, std::string(r.out + r.err));
        REQUIRE(r.j["rates"].size() == 2);
        CHECK(r.j["rates"][1]["rate_hz"] == 4e6);
        CHECK(r.j["rates"][1]["overflows"] == 0);
        CHECK(f.cli({"throughput", "--rates", "2.2"}).code == kUsage); // not offered
    }
    SUBCASE("an overflow fails")
    {
        // The overflow comes at the second read: the unpaced fake is
        // CPU-bound and 200000 samples (0.1 s of signal) took 4-6 s of the
        // 10 s run alone under ASan, more than the run under ctest load.
        Fake f(R"(,"paced":false,"overflow_at":1)");
        const Result r = f.cli({"throughput", "--rates", "2", "--duration", guard(2), "--json"});
        CHECK(r.code == kFail);
        CHECK(r.j["rates"][0]["overflow_events"] == 1);
        CHECK(r.j["rates"][0]["samples_dropped"] == 20000);
    }
}

TEST_CASE("yantra-hackrf: replug and report (RAD-66, RAD-35, RAD-95)")
{
    Fake f;
    const std::string unplug = "touch " + f.file(".unplugged");
    const std::string replug = "rm " + f.file(".unplugged");
    const Result r = f.cli({"replug", "--unplug-cmd", unplug, "--replug-cmd", replug, "--timeout", "20",
                            "--bias-tee", "--set", "antenna_power=true", "--json"});
    REQUIRE_MESSAGE(r.code == kPass, std::string(r.out + r.err));
    CHECK(r.j["lost_seen"] == true);
    CHECK(r.j["antenna_power_off"] == true);
    CHECK(r.j["records_after"].get<int>() >= 2);
    // Nobody unplugs: a failure after the timeout.
    CHECK(f.cli({"replug", "--timeout", "1", "--no-prompt"}).code == kFail);

    const std::string md = (f.control / "DEVICES.md").string();
    const Result rep = f.cli({"report", "--unplug-cmd", unplug, "--replug-cmd", replug, "--timeout", "20",
                              "--rates", "2", "--gains", "16/24", "--duration", guard(4), "--no-prompt", "--out", md,
                              "--json"});
    REQUIRE_MESSAGE(rep.code == kPass, std::string(rep.out + rep.err));
    const std::string row = rep.j["devices_md_row"];
    CHECK(row.rfind("| HackRF One (firmware 2024.02.1) | SoapyHackRF (yantra fake module for tests) 0.3.6-fake", 0) == 0);
    CHECK(row.find("selftest pass") != std::string::npos);
    CHECK(row.find("throughput up to 2 Msps pass") != std::string::npos);
    CHECK(row.find("replug pass") != std::string::npos);
    CHECK(row.find(kSerial) == std::string::npos); // no serial numbers in DEVICES.md
    std::ifstream in(md);
    std::string line;
    std::getline(in, line);
    CHECK(line == row);
}
