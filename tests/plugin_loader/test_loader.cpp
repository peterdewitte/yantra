// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plugin folders and the plugin loader (PLG-09, PLG-03, PLG-10): the C
// test plugin (tests/plugin_api/header_c.c) loaded as a module; a wrong
// API version, a failing init, a file without entry point, a file that is
// no library and a duplicate id are refused or disabled with a message
// and the loader keeps working; enable/disable is kept in the state
// store; a failure reported from another thread disables the plugin.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <yantra/core/plugin/folders.h>
#include <yantra/core/plugin/loader.h>

namespace fs = std::filesystem;
namespace yp = yantra::plugin;

namespace {

struct Log {
    std::mutex m;
    std::vector<std::string> lines;
    void add(const std::string& id, const std::string& msg) {
        const std::lock_guard lock(m);
        lines.push_back(id + ": " + msg);
    }
    bool has(const std::string& needle) {
        const std::lock_guard lock(m);
        for (const auto& l : lines)
            if (l.find(needle) != std::string::npos) return true;
        return false;
    }
};

yp::PluginLoaderConfig config(Log& log, std::shared_ptr<yp::PluginStateStore> state = nullptr) {
    yp::PluginLoaderConfig c;
    c.state = std::move(state);
    c.log = [&log](const std::string& id, std::uint32_t, const std::string& msg) { log.add(id, msg); };
    return c;
}

fs::path fresh_dir(const std::string& name) {
    const fs::path p = fs::path(LOADER_TMP_DIR) / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

}  // namespace

TEST_CASE("plugin folders: overrides, XDG, home and the application folder") {
    yp::FolderInputs in;
    in.plugin_path_env = "/dev/plugins-a:relative/ignored:/dev/plugins-b";
    in.xdg_data_home = "/xdg";
    in.home = "/home/someone";
    in.executable_dir = "/opt/yantra/bin";
    yp::PluginFolders f = yp::plugin_folders(in);
    REQUIRE(f.plugins.size() == 4);
    CHECK(f.plugins[0] == fs::path("/dev/plugins-a"));
    CHECK(f.plugins[1] == fs::path("/dev/plugins-b"));
    CHECK(f.user == fs::path("/xdg/yantra/plugins"));
    CHECK(f.application == fs::path("/opt/yantra/lib/yantra/plugins"));
    CHECK(f.plugins[2] == f.user);
    CHECK(f.plugins[3] == f.application);
    REQUIRE(f.radios.size() == 4);
    CHECK(f.radios[2] == fs::path("/xdg/yantra/plugins/radios"));

    in.xdg_data_home.clear();
    CHECK(yp::plugin_folders(in).user == fs::path("/home/someone/.local/share/yantra/plugins"));
    in.user_plugin_dir = "/tests/user";
    CHECK(yp::plugin_folders(in).user == fs::path("/tests/user"));
    in = {};
    CHECK(yp::plugin_folders(in).plugins.empty());
}

TEST_CASE("plugin folders: discovery lists only plugin files, sorted per folder") {
    const fs::path a = fresh_dir("discover/a");
    const fs::path b = fresh_dir("discover/b");
    for (const char* n : {"z.so", "a.so", "notes.txt"}) std::ofstream(a / n) << "x";
    fs::create_directories(a / "radios");
    std::ofstream(a / "radios" / "r.so") << "x";
    std::ofstream(b / "m.so") << "x";
    const auto files = yp::discover_plugin_files({a, b, fs::path(LOADER_TMP_DIR) / "missing"});
    REQUIRE(files.size() == 3);
    CHECK(files[0] == a / "a.so");
    CHECK(files[1] == a / "z.so");
    CHECK(files[2] == b / "m.so");
}

TEST_CASE("loader: the C test plugin loads, initialises and shuts down") {
    Log log;
    {
        yp::PluginLoader loader(config(log));
        const yp::PluginEntry e = loader.load_file(CTEST_OK_PATH);
        INFO(e.message);
        CHECK(e.status == yp::PluginStatus::Enabled);
        CHECK(e.id == "org.example.ctest");
        CHECK(e.name == "C test plugin");
        CHECK(e.licence == "GPL-3.0-or-later");
        CHECK(e.kinds == (YANTRA_KIND_TECHNOLOGY | YANTRA_KIND_FEATURE | YANTRA_KIND_EXPORTER));
        CHECK(log.has("org.example.ctest: ctest plugin ready"));
        CHECK(loader.descriptor("org.example.ctest") != nullptr);
        CHECK(loader.instance("org.example.ctest") != nullptr);
        REQUIRE(loader.info("org.example.ctest") != nullptr);
        CHECK(loader.info("org.example.ctest")->technology.has_value());
        // The same file again is the same plugin, not a duplicate.
        CHECK(loader.load_file(CTEST_OK_PATH).status == yp::PluginStatus::Enabled);
        CHECK(loader.list().size() == 1);
    }
}

TEST_CASE("loader: wrong API version, failing init, no entry, no library, duplicate") {
    Log log;
    yp::PluginLoader loader(config(log));
    REQUIRE(loader.load_file(CTEST_OK_PATH).status == yp::PluginStatus::Enabled);

    const yp::PluginEntry v2 = loader.load_file(CTEST_V2_PATH);
    INFO(v2.message);
    CHECK(v2.status == yp::PluginStatus::Refused);
    CHECK(v2.message.find("2.") != std::string::npos);
    CHECK(v2.api_version.major == 2);

    const yp::PluginEntry noentry = loader.load_file(CTEST_NOENTRY_PATH);
    CHECK(noentry.status == yp::PluginStatus::Refused);
    CHECK(noentry.message.find("does not export 'yantra_plugin_entry'") != std::string::npos);

    const fs::path bogus = fresh_dir("bogus") / "bogus.so";
    std::ofstream(bogus) << "not a library";
    const yp::PluginEntry bad = loader.load_file(bogus);
    CHECK(bad.status == yp::PluginStatus::Refused);
    CHECK(bad.message.find("cannot be opened as a shared library") != std::string::npos);

    // Same id as ctest_ok, other file.
    const yp::PluginEntry dup = loader.load_file(CTEST_INITFAIL_PATH);
    CHECK(dup.status == yp::PluginStatus::Refused);
    CHECK(dup.message.find("duplicate plugin id 'org.example.ctest'") != std::string::npos);

    // The good plugin still runs; every file is in the list.
    CHECK(loader.entry("org.example.ctest")->status == yp::PluginStatus::Enabled);
    CHECK(loader.list().size() == 5);
    CHECK(log.has("refused"));
}

TEST_CASE("loader: a failing init leaves the plugin failed with the reason") {
    Log log;
    yp::PluginLoader loader(config(log));
    const yp::PluginEntry e = loader.load_file(CTEST_INITFAIL_PATH);
    CHECK(e.status == yp::PluginStatus::Failed);
    CHECK(e.message.find("init failed") != std::string::npos);
    CHECK(loader.instance("org.example.ctest") == nullptr);
    // enable retries, and fails again the same way.
    CHECK(loader.enable("org.example.ctest") != YANTRA_OK);
    CHECK(loader.entry("org.example.ctest")->status == yp::PluginStatus::Failed);
}

TEST_CASE("loader: enable and disable are kept in the state store") {
    Log log;
    auto state = std::make_shared<yp::MemoryStateStore>();
    state->set_enabled("org.example.ctest", false);
    {
        yp::PluginLoader loader(config(log, state));
        CHECK(loader.load_file(CTEST_OK_PATH).status == yp::PluginStatus::Disabled);
        CHECK(loader.instance("org.example.ctest") == nullptr);
        CHECK(loader.enable("org.example.ctest") == YANTRA_OK);
        CHECK(loader.entry("org.example.ctest")->status == yp::PluginStatus::Enabled);
        CHECK(state->enabled("org.example.ctest") == true);
        CHECK(loader.disable("org.example.ctest") == YANTRA_OK);
        CHECK(loader.entry("org.example.ctest")->status == yp::PluginStatus::Disabled);
        CHECK(state->enabled("org.example.ctest") == false);
        CHECK(loader.enable("org.unknown") == YANTRA_ERR_NOT_FOUND);
    }
    yp::PluginLoader again(config(log, state));
    CHECK(again.load_file(CTEST_OK_PATH).status == yp::PluginStatus::Disabled);
}

TEST_CASE("loader: a failure reported from another thread disables the plugin") {
    Log log;
    yp::PluginLoader loader(config(log));
    REQUIRE(loader.load_file(CTEST_OK_PATH).status == yp::PluginStatus::Enabled);
    std::thread t([&] { loader.report_failure("org.example.ctest", "export call crashed"); });
    t.join();
    CHECK(loader.entry("org.example.ctest")->status == yp::PluginStatus::Enabled);
    CHECK(loader.poll() == 1);
    const auto e = loader.entry("org.example.ctest");
    CHECK(e->status == yp::PluginStatus::Failed);
    CHECK(e->message.find("export call crashed") != std::string::npos);
    CHECK(loader.instance("org.example.ctest") == nullptr);
    CHECK(loader.poll() == 0);
}

TEST_CASE("loader: main-thread calls are refused on other threads") {
    Log log;
    yp::PluginLoader loader(config(log));
    std::thread t([&] {
        CHECK(loader.load_file(CTEST_OK_PATH).status != yp::PluginStatus::Enabled);
        CHECK(loader.enable("org.example.ctest") == YANTRA_ERR_BUSY);
        CHECK(loader.load_all() == 0);
    });
    t.join();
    CHECK(loader.list().empty());
}

TEST_CASE("loader: load_all tries every plugin file of the folders") {
    Log log;
    yp::PluginLoaderConfig c = config(log);
    c.folders = {fs::path(CTEST_OK_PATH).parent_path(), fs::path(CTEST_NOENTRY_PATH).parent_path()};
    yp::PluginLoader loader(std::move(c));
    CHECK(loader.load_all() == 2);
    const auto list = loader.list();
    REQUIRE(list.size() == 2);
    CHECK(loader.entry("org.example.ctest")->status == yp::PluginStatus::Enabled);
}

TEST_CASE("loader: settings_set is refused on a measurement thread (PLG-07)") {
    Log log;
    yp::PluginLoader loader(config(log));
    REQUIRE(loader.load_file(CTEST_OK_PATH).status == yp::PluginStatus::Enabled);
    // The C test plugin's instance starts with its host table (header_c.c).
    struct CtestInstance {
        const yantra_host_api* host;
    };
    const auto* instance =
        reinterpret_cast<const CtestInstance*>(loader.instance("org.example.ctest"));
    REQUIRE(instance != nullptr);
    const yantra_host_api* host = instance->host;
    CHECK_FALSE(yp::on_measurement_thread());
    CHECK(host->settings_set(host->host, "a.key", "main") == YANTRA_OK);
    yantra_status on_worker = YANTRA_OK;
    yantra_status in_scope = YANTRA_OK;
    bool flagged = false;
    std::thread([&] {
        on_worker = host->settings_set(host->host, "a.key", "worker");
        const yp::MeasurementThreadScope scope;
        flagged = yp::on_measurement_thread();
        in_scope = host->settings_set(host->host, "a.key", "measurement");
    }).join();
    CHECK(on_worker == YANTRA_OK);  // any other thread may store settings
    CHECK(flagged);
    CHECK(in_scope == YANTRA_ERR_BUSY);
    CHECK_FALSE(yp::on_measurement_thread());
    CHECK(loader.state().setting("org.example.ctest", "a.key") == "worker");
    CHECK(log.has("settings_set refused on a measurement thread (PLG-07)"));
}

namespace {

// The C test plugin's instance (header_c.c): its host table and the
// number of records its feature's on_record saw.
struct CtestState {
    const yantra_host_api* host;
    int records_seen;
};

struct CountingController final : yp::Controller {
    std::mutex m;
    std::vector<std::string> requests;
    std::string control(const std::string& plugin_id, const std::string& request) override {
        const std::lock_guard lock(m);
        requests.push_back(plugin_id + " " + request);
        return R"({"ok":true,"command":"status","result":{"n":)" + std::to_string(requests.size()) + "}}";
    }
};

}  // namespace

TEST_CASE("loader: control and data_folder host services (plugin API 1.1)") {
    Log log;
    yp::PluginLoaderConfig c = config(log);
    const fs::path root = fresh_dir("data_root");
    c.data_root = root / "plugin-data";
    yp::PluginLoader loader(std::move(c));
    REQUIRE(loader.load_file(CTEST_OK_PATH).status == yp::PluginStatus::Enabled);
    const auto* inst = reinterpret_cast<const CtestState*>(loader.instance("org.example.ctest"));
    REQUIRE(inst != nullptr);
    const yantra_host_api* host = inst->host;
    REQUIRE(YANTRA_HAS_FIELD(host, yantra_host_api, data_folder));
    CHECK(host->api_version.minor >= 1u);

    const char* req = R"({"command":"status"})";
    char reply[256];
    std::uint32_t len = 0;
    yantra_status st = YANTRA_OK;
    // No controller: unsupported; on the main thread: refused.
    std::thread([&] { st = host->control(host->host, req, reply, sizeof reply, &len); }).join();
    CHECK(st == YANTRA_ERR_UNSUPPORTED);
    auto ctl = std::make_shared<CountingController>();
    loader.set_controller(ctl);
    CHECK(host->control(host->host, req, reply, sizeof reply, &len) == YANTRA_ERR_BUSY);
    CHECK(log.has("control called on the main or a measurement thread"));
    std::thread([&] {
        const yp::MeasurementThreadScope scope;
        st = host->control(host->host, req, reply, sizeof reply, &len);
    }).join();
    CHECK(st == YANTRA_ERR_BUSY);
    CHECK(ctl->requests.empty());

    std::thread([&] { st = host->control(host->host, req, reply, sizeof reply, &len); }).join();
    CHECK(st == YANTRA_OK);
    CHECK(std::string(reply) == R"({"ok":true,"command":"status","result":{"n":1}})");
    CHECK(len == std::string(reply).size());
    REQUIRE(ctl->requests.size() == 1);
    CHECK(ctl->requests[0] == std::string("org.example.ctest ") + req);

    // Too small: the length, then the same reply without a second run.
    char tiny[8];
    std::thread([&] { st = host->control(host->host, req, tiny, sizeof tiny, &len); }).join();
    CHECK(st == YANTRA_ERR_BUFFER_TOO_SMALL);
    CHECK(len == std::string(R"({"ok":true,"command":"status","result":{"n":2}})").size());
    std::thread([&] { st = host->control(host->host, req, reply, sizeof reply, &len); }).join();
    CHECK(st == YANTRA_OK);
    CHECK(std::string(reply) == R"({"ok":true,"command":"status","result":{"n":2}})");
    CHECK(ctl->requests.size() == 2);
    std::thread([&] { st = host->control(host->host, req, reply, sizeof reply, &len); }).join();
    CHECK(std::string(reply) == R"({"ok":true,"command":"status","result":{"n":3}})");
    CHECK(host->control(host->host, nullptr, reply, sizeof reply, &len) == YANTRA_ERR_INVALID_ARGUMENT);

    // The plugin's own folder, created for the user only.
    char folder[512];
    REQUIRE(host->data_folder(host->host, folder, sizeof folder, &len) == YANTRA_OK);
    CHECK(fs::path(folder) == root / "plugin-data" / "org.example.ctest");
    CHECK(fs::is_directory(folder));
    CHECK(fs::status(folder).permissions() == fs::perms::owner_all);
    CHECK(host->data_folder(host->host, folder, 4, &len) == YANTRA_ERR_BUFFER_TOO_SMALL);
    CHECK(len == std::string((root / "plugin-data" / "org.example.ctest").string()).size());
}

TEST_CASE("loader: data_folder is unsupported without a data root") {
    Log log;
    yp::PluginLoader loader(config(log));
    REQUIRE(loader.load_file(CTEST_OK_PATH).status == yp::PluginStatus::Enabled);
    const auto* inst = reinterpret_cast<const CtestState*>(loader.instance("org.example.ctest"));
    char folder[64];
    CHECK(inst->host->data_folder(inst->host->host, folder, sizeof folder, nullptr) == YANTRA_ERR_UNSUPPORTED);
}

TEST_CASE("loader: feature records reach on_record of enabled feature plugins (PLG-02)") {
    Log log;
    yp::PluginLoader loader(config(log));
    CHECK_FALSE(loader.has_record_receivers());
    REQUIRE(loader.load_file(CTEST_OK_PATH).status == yp::PluginStatus::Enabled);
    CHECK(loader.has_record_receivers());
    const auto* inst = reinterpret_cast<const CtestState*>(loader.instance("org.example.ctest"));
    REQUIRE(inst != nullptr);
    const int before = inst->records_seen;

    yp::Schema s;
    s.id = "ctest_cell";
    s.label = "Cell";
    yp::Field f;
    f.name = "level";
    f.label = "Level";
    f.unit = "dBm";
    f.type = yp::FieldType::Double;
    f.category = yp::Category::Level;
    s.fields.push_back(f);
    const yp::SchemaView view(s);
    CHECK(view.c().field_count == 1);
    CHECK(std::string(view.c().fields[0]->unit) == "dBm");
    const yantra_value v = yantra_value_double(-70.0);
    yantra_record r{};
    r.struct_size = sizeof r;
    r.value_count = 1;
    r.values = &v;
    yp::Provenance p;
    p.technology_id = "ctest";
    p.plugin_id = "org.example.ctest";
    p.schema_id = "ctest_cell";
    const yantra_provenance cp = yp::to_c(p);
    std::thread([&] {
        const yp::MeasurementThreadScope scope;
        for (int i = 0; i < 3; ++i) loader.deliver_record(cp, view.c(), r);
    }).join();
    CHECK(inst->records_seen == before + 3);

    REQUIRE(loader.disable("org.example.ctest") == YANTRA_OK);
    CHECK_FALSE(loader.has_record_receivers());
    loader.deliver_record(cp, view.c(), r);  // nobody takes it
}
