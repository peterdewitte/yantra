// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plugin loader (PLG-07, PLG-09, PLG-10) for plugins of the plugin API
// (plugin-api/include/yantra/plugin.h): finds the plugin files (folders.h), opens
// them with dlopen (RTLD_LOCAL, platform/dynlib.h), calls the entry point
// with the host table, checks the descriptor (version first, PLG-04; then
// parse_descriptor with the licence check, PLG-10), keeps the enabled
// state and runs init/shutdown. A plugin that fails to load or reports an
// error is disabled with a message; the others and the app keep running.
// Radio plugins are not plugin.h plugins: RadioHost (radio/radio_host.h)
// loads them from the radios/ subfolders (PLG-25).
//
// Host services of the table: log and notify (to the configured
// callbacks), settings (PluginStateStore), the clocks, timers (on_timer on
// a worker thread), emit_record (a RecordingSink when a recording runs,
// else BUSY), the UI window requests (queued for the UI shell, which
// takes them each frame; phase 9) and, since plugin API 1.1, control (the
// application's Controller) and data_folder (config.data_root). A feature
// plugin's start runs after init and its stop before shutdown; its
// on_record gets the pipeline's records through deliver_record.
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plugin/
// manager.h, adapted for plugin API 1.0 (no radio or IQ parts).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/plugin/descriptor.h"
#include "yantra/core/plugin/record.h"
#include "yantra/core/plugin/state_store.h"

namespace yantra::plugin {

enum class PluginStatus {
    Enabled,   // initialised and running
    Disabled,  // loaded, switched off by the user
    Failed,    // loaded, but init or a later call reported an error (message says why)
    Refused,   // not loaded: not a plugin, wrong API version, invalid, licence, duplicate
};

const char* status_text(PluginStatus status) noexcept;

// One line of the plugin list in the settings (PLG-09).
struct PluginEntry {
    std::filesystem::path path;
    std::string id;  // empty when the descriptor could not be read
    std::string name;
    std::string version;
    std::string author;
    std::string licence;  // SPDX expression (PLG-10)
    std::string description;
    yantra_api_version api_version{};
    std::uint32_t kinds = 0;
    PluginStatus status = PluginStatus::Refused;
    std::string message;  // why it is refused or failed, else ""
};

// Text of the kinds, e.g. "technology, feature".
std::string kinds_text(std::uint32_t kinds);

// Receiver of feature-plugin records (emit_record) while a recording runs
// (REC, later phases). Called on the plugin's thread; must not block.
class RecordingSink {
public:
    virtual ~RecordingSink() = default;
    virtual void on_feature_record(const std::string& plugin_id, const Record& record) = 0;
};

// The application's control commands for yantra_host_api::control
// (API-01/02, plugin API 1.1): the desktop app and the headless runner
// each give one. Called on the plugin's own thread; may wait for the
// application (bounded). Returns the reply JSON object as text.
class Controller {
public:
    virtual ~Controller() = default;
    virtual std::string control(const std::string& plugin_id, const std::string& request) = 0;
};

// A window request of a plugin (ui_open_window / ui_close_window).
struct UiRequest {
    std::string plugin_id;
    std::string window_id;
    bool open = true;
};

struct PluginLoaderConfig {
    // Folders searched by load_all() (plugin_folders(...).plugins).
    std::vector<std::filesystem::path> folders;
    // Enabled state and plugin settings; nullptr = an own in-memory store.
    std::shared_ptr<PluginStateStore> state;
    // Log of all plugins and of the loader ("" plugin id). Any thread.
    std::function<void(const std::string& plugin_id, std::uint32_t level,
                       const std::string& message)>
        log;
    // Root of the plugins' own folders (yantra_host_api::data_folder:
    // <data_root>/<plugin id>, created 0700 on first use); empty = none.
    std::filesystem::path data_root;
    // User notifications (notify). Any thread.
    std::function<void(const std::string& plugin_id, std::uint32_t level,
                       const std::string& text)>
        notify;
};

// Marks the calling thread as a measurement thread while the scope lives
// (PLG-07): the measurement pipeline (and the plugin test kit) runs a
// radio's records on such a thread. Host services that are not allowed
// there (settings_set) return YANTRA_ERR_BUSY and are logged as a warning
// containing "(PLG-07)". Scopes nest.
class MeasurementThreadScope {
public:
    MeasurementThreadScope() noexcept;
    ~MeasurementThreadScope();
    MeasurementThreadScope(const MeasurementThreadScope&) = delete;
    MeasurementThreadScope& operator=(const MeasurementThreadScope&) = delete;

private:
    bool previous_;
};
bool on_measurement_thread() noexcept;

// Owns the plugin.h plugins. The constructing thread is the main thread:
// load, enable, disable, unload and poll must be called on it (else
// YANTRA_ERR_BUSY); list, entry, report_failure and the host services are
// thread-safe.
class PluginLoader {
public:
    explicit PluginLoader(PluginLoaderConfig config);
    ~PluginLoader();  // stops the timers, shuts every plugin down
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    // Discovers and loads every plugin file of the folders; enables those
    // the user did not disable. Returns the number of files tried.
    std::size_t load_all();
    // Loads one plugin file; enables it unless the user disabled it.
    PluginEntry load_file(const std::filesystem::path& path);

    // The plugin list (PLG-09), loaded and refused ones. Thread-safe.
    std::vector<PluginEntry> list() const;
    std::optional<PluginEntry> entry(const std::string& id) const;
    // Checked descriptor of a loaded plugin (main thread); nullptr if not loaded.
    const PluginInfo* info(const std::string& id) const;
    // Raw descriptor and instance of an enabled plugin (main thread), for
    // the core parts that call it (technology rules, export); nullptr
    // otherwise.
    const yantra_plugin_descriptor* descriptor(const std::string& id) const;
    yantra_plugin_instance* instance(const std::string& id) const;

    // User choices, kept in the state store. enable also retries a failed plugin.
    yantra_status enable(const std::string& id);
    yantra_status disable(const std::string& id);
    void unload_all();

    // Reports that a call into a plugin failed (thread-safe); applied by
    // the next poll(), which disables the plugin with the message.
    void report_failure(const std::string& id, const std::string& message);
    // Applies reported failures; call once per main-loop turn. Returns the
    // number of plugins disabled.
    std::size_t poll();

    // Recording that receives emit_record (nullptr: none, emit_record BUSY).
    void set_recording_sink(std::shared_ptr<RecordingSink> sink);
    // Where yantra_host_api::control goes (nullptr: UNSUPPORTED). Thread-safe.
    void set_controller(std::shared_ptr<Controller> controller);

    // Feature records (PLG-02): every enabled feature plugin with on_record
    // gets the record; any thread (the measurement threads,
    // measure/feature_feed.h). Disabling a plugin waits for its running
    // call. has_record_receivers() is a cheap check first.
    bool has_record_receivers() const noexcept;
    void deliver_record(const yantra_provenance& provenance, const yantra_schema& schema,
                        const yantra_record& record);

    // Window requests since the last call (the UI shell takes them each frame).
    std::vector<UiRequest> take_ui_requests();

    PluginStateStore& state() noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace yantra::plugin
