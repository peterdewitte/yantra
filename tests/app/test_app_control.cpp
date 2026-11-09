// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's control service for the probe plugin (API-01/02, PLG-24):
// status from the UI thread's copy, other commands queued and run by
// tick(), the runner-only commands refused; the probe plugin loaded in the
// app gets its own data folder (plugin API 1.1) and opens no socket until
// api.listen is set.
#include <doctest/doctest.h>

#include <filesystem>

#include <nlohmann/json.hpp>

#include "yantra/app/app_control.h"
#include "yantra/app/app_state.h"
#include "yantra/core/plugin/loader.h"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace yantra::app;

namespace {

json ask(AppControl& c, const json& request) { return json::parse(c.control("yantra.probe", request.dump())); }

}  // namespace

TEST_CASE("API-01 the app control: status from the copy, commands queued, runner-only commands refused") {
    AppControl c;
    c.set_status(json{{"state", "stopped"}});
    json r = ask(c, {{"command", "status"}});
    CHECK(r["ok"] == true);
    CHECK(r["result"]["state"] == "stopped");
    r = ask(c, {{"command", "record.start"}, {"args", {{"name", "a"}}}});
    CHECK(r["ok"] == true);
    CHECK(r["result"]["queued"] == true);
    for (const char* name : {"play", "rotate", "quit"}) {
        r = ask(c, {{"command", name}, {"args", {{"file", "x.yrec"}}}});
        CHECK_MESSAGE(r["ok"] == false, name);
    }
    CHECK(ask(c, {{"command", "gps.on"}})["ok"] == false);
    CHECK(json::parse(c.control("p", "not json"))["ok"] == false);
    auto q = c.take();
    REQUIRE(q.size() == 1);
    CHECK(q[0].id == yantra::control::CommandId::RecordStart);
    CHECK(q[0].arg("name") == "a");
    CHECK(c.take().empty());
    for (std::size_t k = 0; k < AppControl::kMaxQueued; ++k) CHECK(ask(c, {{"command", "sound.on"}})["ok"] == true);
    CHECK(ask(c, {{"command", "sound.on"}})["ok"] == false);
}

TEST_CASE("PLG-24 the probe plugin in the app: data folder, sound commands run on tick, status with the source link") {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / "app_control";
    fs::remove_all(dir);
    const fs::path plugins = dir / "plugins";
    fs::create_directories(plugins);
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "probe.so", plugins / "probe.so");
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.user_data = dir / "data";
    p.radios.plugin_folders = {plugins};
    p.radios.plugin_state = dir / "config" / "plugin_state.json";
    AppState s(AppSettings{}, p);
    AppControl& c = s.remote_control();
    REQUIRE(s.plugins().info("yantra.probe") != nullptr);
    bool enabled = false;
    for (const auto& e : s.plugins().list())
        if (e.id == "yantra.probe") enabled = e.status == yantra::plugin::PluginStatus::Enabled;
    REQUIRE(enabled);
    CHECK(fs::is_directory(dir / "data" / "plugin-data" / "yantra.probe"));
    CHECK_FALSE(s.settings().sound.enabled);
    CHECK(ask(c, {{"command", "sound.on"}})["result"]["queued"] == true);
    s.tick();
    CHECK(s.settings().sound.enabled);
    const json st = ask(c, {{"command", "status"}})["result"];
    CHECK(st["application"] == "yantra");
    CHECK(st["sound"] == true);
    CHECK(st["state"] == "stopped");
    CHECK(st["source_url"].get<std::string>().rfind("https://", 0) == 0);
    CHECK(ask(c, {{"command", "record.start"}})["result"]["queued"] == true);
    s.tick();  // no project: refused on the status line, nothing records
    CHECK(ask(c, {{"command", "status"}})["result"]["recording"]["recording"] == false);
}

#if defined(YANTRA_TEST_DEMO_PROJECT) && defined(YANTRA_TEST_FEATURE_PLUGIN)
namespace {

// The C test plugin's instance (tests/plugin_api/header_c.c): its feature
// counts the records on_record saw.
struct FeatureCounter {
    const yantra_host_api* host;
    int records_seen;
};

}  // namespace

TEST_CASE("PLG-02, API-02 the app's replay feeds the feature plugins, as the walk test does") {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / "app_replay_feed";
    fs::remove_all(dir);
    const fs::path plugins = dir / "plugins";
    fs::create_directories(plugins);
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "tetra.so", plugins / "tetra.so");
    fs::copy_file(YANTRA_TEST_FEATURE_PLUGIN, plugins / "ctest.so");
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.data_folders = {fs::path(YANTRA_TEST_DEMO_PROJECT).parent_path().parent_path().parent_path()};
    p.user_data = dir / "data";
    p.radios.plugin_folders = {plugins};
    p.radios.plugin_state = dir / "config" / "plugin_state.json";
    AppState s(AppSettings{}, p);
    const auto* counter = reinterpret_cast<const FeatureCounter*>(s.plugins().instance("org.example.ctest"));
    REQUIRE(counter != nullptr);
    CHECK(counter->records_seen == 0);
    const Result opened = s.trigger(Action::OpenDemo);
    REQUIRE_MESSAGE(opened.ok, opened.message);
    REQUIRE(s.replay() != nullptr);
    const auto recordings = s.replay()->recordings();
    REQUIRE_FALSE(recordings.empty());
    const Result r = s.open_replay(recordings.front());
    REQUIRE_MESSAGE(r.ok, r.message);
    REQUIRE(s.replay()->play(0.0).ok);
    REQUIRE(s.replay()->wait([](auto st, auto) { return st != yantra::recording::ReplayPlayer::State::Playing; },
                             120000 * YANTRA_TEST_TIME_SCALE));
    // Closing joins the player's thread: the count is read after its last record.
    REQUIRE(s.close_replay().ok);
    CHECK(counter->records_seen > 0);
}
#endif
