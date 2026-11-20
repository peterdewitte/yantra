// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's control service for the probe plugin (API-01/02, PLG-24):
// status from the UI thread's copy, other commands queued and run by
// tick(), the runner-only commands refused; the probe plugin loaded in the
// app gets its own data folder (plugin API 1.1) and opens no socket until
// api.listen is set.
#include <doctest/doctest.h>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  include <vector>
#endif

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "yantra/app/app_control.h"
#include "yantra/app/app_state.h"
#include "yantra/app/plugin_state_file.h"
#include "yantra/core/plugin/loader.h"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace yantra::app;

namespace {

json ask(AppControl& c, const json& request) { return json::parse(c.control("yantra.probe", request.dump())); }

/// Network listeners of this process: its TCP sockets in state LISTEN and
/// its bound UDP sockets, IPv4 and IPv6 (Linux: /proc; Windows: the IP
/// helper's tables with their owning process).
#ifdef _WIN32
[[maybe_unused]] std::size_t listeners() {
    const DWORD self = GetCurrentProcessId();
    std::size_t n = 0;
    const auto count = [&](auto get, auto table_class, ULONG family, auto per_row) {
        ULONG size = 0;
        std::vector<unsigned char> buffer;
        DWORD rc = ERROR_INSUFFICIENT_BUFFER;
        while (rc == ERROR_INSUFFICIENT_BUFFER) {
            buffer.resize(size + 1);
            rc = get(buffer.data(), &size, FALSE, family, table_class, 0);
        }
        if (rc == NO_ERROR) n += per_row(buffer.data(), self);
    };
    const auto tcp4 = [](const unsigned char* b, DWORD pid) {
        const auto* t = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(b);
        std::size_t k = 0;
        for (DWORD i = 0; i < t->dwNumEntries; ++i) k += t->table[i].dwOwningPid == pid ? 1 : 0;
        return k;
    };
    const auto tcp6 = [](const unsigned char* b, DWORD pid) {
        const auto* t = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(b);
        std::size_t k = 0;
        for (DWORD i = 0; i < t->dwNumEntries; ++i) k += t->table[i].dwOwningPid == pid ? 1 : 0;
        return k;
    };
    const auto udp4 = [](const unsigned char* b, DWORD pid) {
        const auto* t = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(b);
        std::size_t k = 0;
        for (DWORD i = 0; i < t->dwNumEntries; ++i) k += t->table[i].dwOwningPid == pid ? 1 : 0;
        return k;
    };
    const auto udp6 = [](const unsigned char* b, DWORD pid) {
        const auto* t = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(b);
        std::size_t k = 0;
        for (DWORD i = 0; i < t->dwNumEntries; ++i) k += t->table[i].dwOwningPid == pid ? 1 : 0;
        return k;
    };
    count(GetExtendedTcpTable, TCP_TABLE_OWNER_PID_LISTENER, AF_INET, tcp4);
    count(GetExtendedTcpTable, TCP_TABLE_OWNER_PID_LISTENER, AF_INET6, tcp6);
    count(GetExtendedUdpTable, UDP_TABLE_OWNER_PID, AF_INET, udp4);
    count(GetExtendedUdpTable, UDP_TABLE_OWNER_PID, AF_INET6, udp6);
    return n;
}
#else
[[maybe_unused]] std::size_t listeners() {
    std::set<std::string> mine;  // socket inodes of our file descriptors
    std::error_code ec;
    for (fs::directory_iterator it("/proc/self/fd", ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code lec;
        const std::string target = fs::read_symlink(it->path(), lec).string();
        if (!lec && target.rfind("socket:[", 0) == 0) mine.insert(target.substr(8, target.size() - 9));
    }
    std::size_t n = 0;
    for (const char* table : {"/proc/self/net/tcp", "/proc/self/net/tcp6", "/proc/self/net/udp", "/proc/self/net/udp6"}) {
        const bool tcp = std::string(table).find("tcp") != std::string::npos;
        std::ifstream in(table);
        std::string line;
        std::getline(in, line);  // header
        while (std::getline(in, line)) {
            std::istringstream f(line);
            std::string sl, local, remote, state, queues, timer, retr, uid, timeout, inode;
            f >> sl >> local >> remote >> state >> queues >> timer >> retr >> uid >> timeout >> inode;
            if (mine.count(inode) != 0 && (!tcp || state == "0A")) ++n;
        }
    }
    return n;
}
#endif

/// The build's technology and feature plugins in a folder of the test's
/// own; without the probe plugin unless `with_probe`.
[[maybe_unused]] fs::path plugin_copy(const fs::path& dir, bool with_probe) {
    const fs::path folder = dir / "plugins";
    fs::create_directories(folder);
    for (const auto& e : fs::directory_iterator(YANTRA_TEST_TECH_PLUGIN_DIR))
        if (e.is_regular_file() && e.path().extension() == YANTRA_PLUGIN_SUFFIX && (with_probe || e.path().filename() != "probe" YANTRA_PLUGIN_SUFFIX))
            fs::copy_file(e.path(), folder / e.path().filename());
    return folder;
}

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

#ifdef YANTRA_TEST_HAVE_PROBE
TEST_CASE("PLG-24 the probe plugin in the app: data folder, sound commands run on tick, status with the source link") {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / "app_control";
    fs::remove_all(dir);
    const fs::path plugins = dir / "plugins";
    fs::create_directories(plugins);
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "probe" YANTRA_PLUGIN_SUFFIX, plugins / "probe" YANTRA_PLUGIN_SUFFIX);
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
#endif

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
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "tetra" YANTRA_PLUGIN_SUFFIX, plugins / "tetra" YANTRA_PLUGIN_SUFFIX);
    fs::copy_file(YANTRA_TEST_FEATURE_PLUGIN, plugins / "ctest" YANTRA_PLUGIN_SUFFIX);
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

#ifdef YANTRA_TEST_DEMO_PROJECT
TEST_CASE("PLG-01, PLG-24 without the probe plugin the app opens no network listener") {
    const std::size_t before = listeners();
    const auto run = [&](const std::string& name, bool with_probe, const std::string& listen) {
        const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / ("no_listener_" + name);
        fs::remove_all(dir);
        AppPaths p;
        p.settings_file = dir / "config" / "app.json";
        p.data_folders = {fs::path(YANTRA_TEST_DEMO_PROJECT).parent_path().parent_path().parent_path()};
        p.user_data = dir / "data";
        p.radios.plugin_folders = {plugin_copy(dir, with_probe)};
        p.radios.plugin_state = dir / "config" / "plugin_state.json";
        if (!listen.empty()) PluginStateFile(p.radios.plugin_state).set_setting("yantra.probe", "api.listen", listen);
        AppState s(AppSettings{}, p);
        CHECK((s.plugins().info("yantra.probe") != nullptr) == with_probe);
        REQUIRE(s.plugins().info("yantra.tetra") != nullptr);
        // The indoor work: the demo project and its replayed walk.
        const Result opened = s.trigger(Action::OpenDemo);
        REQUIRE_MESSAGE(opened.ok, opened.message);
        const auto recordings = s.replay()->recordings();
        REQUIRE_FALSE(recordings.empty());
        const Result r = s.open_replay(recordings.front());
        REQUIRE_MESSAGE(r.ok, r.message);
        REQUIRE(s.replay()->play(0.0).ok);
        REQUIRE(s.replay()->wait([](auto st, auto) { return st != yantra::recording::ReplayPlayer::State::Playing; },
                                 120000 * YANTRA_TEST_TIME_SCALE));
        s.tick();
        const std::size_t open = listeners() - before;
        REQUIRE(s.close_replay().ok);
        return open;
    };
    CHECK(run("plain", false, "") == 0);
#ifdef YANTRA_TEST_HAVE_PROBE
    // With the probe plugin: still none until api.listen is given; then
    // its server is seen (the check sees listeners at all).
    CHECK(run("probe_off", true, "") == 0);
    CHECK(run("probe_on", true, "127.0.0.1:0") >= 1);
#endif
    CHECK(listeners() == before);
}
#endif
