// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The headless runner (PLG-24, API-01, API-03, DEMO-10) with the real
// pipeline, recorder and replay, fed by the simulator radio plugin.
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>

#include <yantra/core/headless/runner.h>
#include <yantra/core/plugin/state_store.h>

namespace fs = std::filesystem;
using nlohmann::json;
using yantra::headless::Runner;
using yantra::headless::RunnerConfig;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path p = fs::path(HEADLESS_SCRATCH_DIR) / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

bool have_simulator() { return fs::exists(fs::path(RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX); }

RunnerConfig demo_config(const fs::path& dir) {
    RunnerConfig c;
    // Only the TETRA technology plugin: a folder of its own.
    const fs::path plugins = dir / "plugins";
    fs::create_directories(plugins);
    fs::copy_file(TETRA_PLUGIN_PATH, plugins / fs::path(TETRA_PLUGIN_PATH).filename());
    c.plugin_folders = {plugins};
    c.radio_folders = {RADIO_PLUGIN_DIR};
    c.data_root = dir / "plugin-data";
    c.recording_folder = dir / "recordings";
    c.demo = true;
    c.monitor = false;
    return c;
}

// Waits (bounded) for an observable condition.
bool wait_for(const std::function<bool()>& ok, double seconds = 60) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (ok()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return ok();
}

json run(Runner& r, const std::string& line) {
    const json reply = r.execute_line(line);
    INFO(line << " -> " << reply.dump());
    return reply;
}

}  // namespace

TEST_CASE("DEMO-10 the runner measures, records, rotates and replays on the simulator (API-01, API-03)") {
    if (!have_simulator()) {
        MESSAGE("no simulator radio plugin built: skipped");
        return;
    }
    const fs::path dir = scratch("demo");
    RunnerConfig cfg = demo_config(dir);
    Runner runner(cfg);
    std::string error;
    REQUIRE_MESSAGE(runner.start(error), error);
    json s = runner.status();
    CHECK(s["state"] == "stopped");
    CHECK(s["demo"] == true);
    CHECK(s["source_url"] == "https://github.com/peterdewitte/yantra");

    CHECK(run(runner, "record start")["ok"] == false);  // needs monitoring
    json r = run(runner, "monitor on");
    REQUIRE(r["ok"] == true);
    CHECK(r["result"]["simulated"] == true);
    REQUIRE(wait_for([&] { return runner.status()["synced_carriers"].get<int>() > 0; }));
    s = runner.status();
    CHECK(s["state"] == "monitoring");
    CHECK(s["radio"]["simulated"] == true);
    CHECK(s["pipeline"]["accepted"].get<std::uint64_t>() > 0);

    r = run(runner, "record start name=probe1");
    REQUIRE(r["ok"] == true);
    CHECK(r["result"]["recording"] == true);
    REQUIRE(wait_for([&] { return runner.status()["recording"]["durable_inputs"].get<std::uint64_t>() > 5; }));
    r = run(runner, "rotate");
    REQUIRE(r["ok"] == true);
    CHECK(r["result"]["files"].get<int>() == 2);
    REQUIRE(wait_for([&] { return runner.status()["recording"]["durable_inputs"].get<std::uint64_t>() > 10; }));
    r = run(runner, "record stop");
    REQUIRE(r["ok"] == true);
    REQUIRE(r["result"]["files"].size() == 2);
    const std::string first = r["result"]["files"][0];
    CHECK(fs::exists(cfg.recording_folder / first));
    CHECK(run(runner, "rotate")["ok"] == false);  // no recording runs

    CHECK(run(runner, "sound on")["result"]["sound"] == true);
    CHECK(runner.status()["sound"] == true);
    CHECK(run(runner, "sound off")["result"]["sound"] == false);

    // Replay the first file as fast as possible: monitoring stops.
    CHECK(run(runner, "play file=" + first + " speed=0.05")["ok"] == false);  // speed out of range
    r = run(runner, "play " + first + " 0");
    REQUIRE(r["ok"] == true);
    s = runner.status();
    CHECK(s["monitoring"] == false);
    CHECK(s["play"]["playing"] == true);
    REQUIRE(wait_for([&] { return runner.status()["play"]["state"] == "ended"; }));
    CHECK(run(runner, "play nothing_here.yrec")["ok"] == false);

    CHECK_FALSE(runner.quit_requested());
    CHECK(run(runner, "quit")["ok"] == true);
    CHECK(runner.quit_requested());
}

TEST_CASE("PLG-24 plugins reach the runner's commands through control (JSON)") {
    if (!have_simulator()) {
        MESSAGE("no simulator radio plugin built: skipped");
        return;
    }
    const fs::path dir = scratch("control");
    Runner runner(demo_config(dir));
    std::string error;
    REQUIRE_MESSAGE(runner.start(error), error);
    json r = json::parse(runner.control("x.test", R"({"command":"status","args":{}})"));
    CHECK(r["ok"] == true);
    CHECK(r["command"] == "status");
    CHECK(r["result"]["application"] == "yantra-probe");
    r = json::parse(runner.control("x.test", "not json"));
    CHECK(r["ok"] == false);
    r = json::parse(runner.control("x.test", R"({"command":"gps.on"})"));
    CHECK(r["ok"] == false);
    // From another thread while monitoring starts (one command at a time).
    std::thread t([&] { (void)runner.control("x.test", R"({"command":"monitor.on"})"); });
    (void)runner.execute_line("status");
    t.join();
    CHECK(runner.status()["monitoring"] == true);
}

TEST_CASE("DEMO-10 demo mode refuses a radio that is not simulated; start-up options apply") {
    const fs::path dir = scratch("none");
    RunnerConfig cfg = demo_config(dir);
    cfg.radio_folders = {dir / "no-radios"};
    cfg.monitor = true;
    {
        Runner runner(cfg);
        std::string error;
        CHECK_FALSE(runner.start(error));
        CHECK(error.find("simulated") != std::string::npos);
    }
    if (!have_simulator()) return;
    cfg = demo_config(scratch("startup"));
    cfg.monitor = true;
    cfg.record = true;
    cfg.record_name = "fixed";
    cfg.split_minutes = 1;
    cfg.keep_files = 3;
    cfg.sound = true;
    Runner runner(cfg);
    std::string error;
    REQUIRE_MESSAGE(runner.start(error), error);
    const json s = runner.status();
    CHECK(s["state"] == "recording");
    CHECK(s["recording"]["name"] == "fixed");
    CHECK(s["sound"] == true);
}
