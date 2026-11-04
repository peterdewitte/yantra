// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Radio host (PLG-25, OA-10, RAD-10..36 host side): loads the radio
// plugins of yantra-radios (yantra/radio.h) from the radio plugin folders
// (PLG-09: <plugins folder>/radios), refuses an incompatible radio API
// version (RAD-11), passes the radio host table (RAD-14) and runs the
// common stream control the same way for every plugin (RAD-30..35):
// enumerate, open, apply the stored settings, tune (plan), start, stop,
// close. The record sink of each stream copies every record, checks it
// against its schema, drops and counts when behind (RAD-24) and hands it
// to the measurement pipeline (OA-13, measure/pipeline.h) on a delivery
// thread of its own. DEVICE_LOST/DEVICE_BACK (and devices_changed) close,
// reopen, re-apply the settings and restart the stream (RAD-35, NFR-04).
// SCENE plugins get the context callbacks (PLG-27, RAD-18/19) answered
// from a context::ContextProvider's snapshots and on_context_event calls
// on a notifier thread (CURSOR_MOVED at most 30 per second).
//
// A plugin that fails to load, refuses its version or crashes a call is
// disabled with a message and the app keeps running. "Crashes a call"
// here means: a control call that does not return within its RAD-15 limit
// (1 s, open and self_test 10 s; times config.call_slack), or that returns
// a result code outside RAD-16. A crash of the process itself cannot be
// caught in-process.
//
// Threading (RAD-15): load, enable/disable and the destructor run on the
// constructing (main) thread, which also calls init and shutdown. Each
// plugin's control functions run on a control thread of that plugin, one
// call at a time; RadioDevice methods wait for them, so call them from a
// worker thread, never from the UI thread. Sink, log and context
// callbacks may come from any plugin thread and never block.
//
// No DSP, decoder, SoapySDR or IQ code here (RAD-26): records only. The
// core stays free of technology- and radio-specific code (PLG-01): the
// host is the same for every radio plugin.
// Design from yantra-radios fb194f5: tools/yantra-radio-host/host.hpp
// (sink, notifier, version check); no code of the radio plugins.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <yantra/radio.h>

#include "yantra/core/context/context.h"
#include "yantra/core/measure/pipeline.h"
#include "yantra/core/plugin/record.h"
#include "yantra/core/plugin/state_store.h"

namespace yantra::radio {

enum class RadioPluginStatus {
    Enabled,   // initialised; devices can be used
    Disabled,  // switched off by the user
    Failed,    // init failed or a call crashed (message says why)
    Refused,   // not loaded: not a radio plugin, radio API version, invalid, licence, duplicate
};

const char* status_text(RadioPluginStatus status) noexcept;

// One line of the radio plugin list (PLG-09, PLG-27: uses_context).
struct RadioPluginEntry {
    std::filesystem::path path;
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string licence;
    std::string description;
    std::string source_url;
    std::string technology_id;
    yr_api_version api_version{};
    std::uint32_t capabilities = 0;  // YR_CAP_*
    bool uses_context = false;       // SCENE: asks for plan, cursor, walk position and scene
    RadioPluginStatus status = RadioPluginStatus::Refused;
    std::string message;
};

// Text of the capability bits, e.g. "simulated, scene".
std::string capabilities_text(std::uint32_t capabilities);

// A device entry (RAD-30), copied.
struct DeviceInfo {
    std::string key;
    std::string label;
    std::string driver;
    std::string serial;
    std::string module_version;
    std::string hardware_info;
    std::string flags_reason;
    std::uint32_t flags = 0;  // YR_DEVICE_*
};

struct EnumerateResult {
    yr_status status = YR_OK;  // YR_ERR_NO_DEVICE: none, `diagnostic` says why (RAD-52)
    std::vector<DeviceInfo> devices;
    std::string diagnostic;
};

// A measurement plan (RAD-32).
struct Plan {
    std::uint32_t type = YR_PLAN_SERVING;
    std::uint64_t serving_hz = 0;  // SERVING: 0 = the strongest
    std::vector<std::uint64_t> carriers_hz;
    std::uint32_t flags = 0;       // YR_PLAN_*
    std::uint64_t scan_first_hz = 0;
    std::uint64_t scan_last_hz = 0;
    std::uint64_t scan_step_hz = 0;
};

enum class DeviceState { Closed, Open, Streaming, Lost, Failed };
const char* state_text(DeviceState state) noexcept;

// Host-side counters of a device's streams (all streams since open, also
// across reopens).
struct SinkCounters {
    std::uint64_t accepted = 0;  // copied and queued for the pipeline
    std::uint64_t delivered = 0; // handed to the pipeline
    std::uint64_t dropped = 0;   // queue full: dropped and counted (RAD-24)
    std::uint64_t invalid = 0;   // did not match the schema (RAD-21): INVALID_ARGUMENT
    std::uint64_t late = 0;      // pushed after stop_stream returned (RAD-33)
    std::vector<std::string> problems;  // first schema findings
};

struct DeviceStats {
    SinkCounters sink;
    std::uint64_t streams = 0;   // streams started (incl. restarts)
    std::uint64_t reopens = 0;   // reopen + restart after DEVICE_BACK (NFR-04)
    bool plugin_ok = false;      // stream_stats answered
    yr_stream_stats plugin{};    // the plugin's stream_stats (RAD-24)
};

struct RadioHostConfig {
    // Radio plugin folders searched by load_all() (plugin_folders(...).radios).
    std::vector<std::filesystem::path> folders;
    // Enabled state and stored device settings; nullptr = own in-memory store.
    std::shared_ptr<plugin::PluginStateStore> state;
    // Receiver of records and events; nullptr = records are counted and dropped.
    std::shared_ptr<measure::MeasurementInput> pipeline;
    // Floor plan, positions and scenes for SCENE plugins; nullptr = NO_PLAN.
    std::shared_ptr<context::ContextProvider> context;
    // Log of the plugins (device_key "" for plugin-level lines) and of the
    // host (plugin_id ""). Any thread.
    std::function<void(const std::string& plugin_id, const std::string& device_key,
                       std::uint32_t level, const std::string& message)>
        log;
    // Records queued per stream before the sink drops (RAD-24).
    std::size_t sink_capacity = 4096;
    // Factor on the RAD-15 call limits before a call counts as crashed.
    double call_slack = 5.0;
};

class RadioHost;

// An opened device (RAD-31..35). Methods wait for the plugin's control
// thread: not from the UI thread. Thread-safe.
class RadioDevice {
public:
    ~RadioDevice();  // closes
    RadioDevice(const RadioDevice&) = delete;
    RadioDevice& operator=(const RadioDevice&) = delete;

    const std::string& plugin_id() const noexcept;
    const std::string& key() const noexcept;
    DeviceState state() const;
    // The running stream's id (0 when none); new after each restart.
    std::uint64_t stream_id() const;

    // Starts measuring with `plan` (RAD-32, RAD-33); the host restarts it
    // after a reopen until stop().
    yr_status start(const Plan& plan);
    // Changes the plan (live with RETUNE_LIVE, else BUSY while streaming).
    yr_status set_plan(const Plan& plan);
    yr_status stop();
    void close();

    // Stores a setting for this device (PLG-25 "stored settings") and
    // applies it; a RESTART setting while streaming stops, applies and
    // restarts (RAD-41).
    yr_status set_setting(const std::string& key, const plugin::Value& value);
    yr_status get_setting(const std::string& key, plugin::Value& out);

    DeviceStats stats() const;
    std::string last_error() const;

    struct Impl;
    explicit RadioDevice(std::shared_ptr<Impl> impl);

private:
    std::shared_ptr<Impl> impl_;
};

class RadioHost {
public:
    explicit RadioHost(RadioHostConfig config);
    ~RadioHost();  // closes every device, shuts every plugin down
    RadioHost(const RadioHost&) = delete;
    RadioHost& operator=(const RadioHost&) = delete;

    // Loads every radio plugin file of the folders; enables those the user
    // did not disable. Returns the number of files tried. Main thread.
    std::size_t load_all();
    RadioPluginEntry load_file(const std::filesystem::path& path);

    std::vector<RadioPluginEntry> list() const;  // thread-safe
    std::optional<RadioPluginEntry> entry(const std::string& id) const;
    // Raw descriptor of a loaded plugin (main thread; nullptr if not loaded).
    const yr_descriptor* descriptor(const std::string& id) const;

    yr_status enable(const std::string& id);   // main thread; retries a failed plugin
    yr_status disable(const std::string& id);  // main thread; closes its devices

    // RAD-30: the devices present now.
    EnumerateResult enumerate(const std::string& plugin_id);
    // RAD-31: opens a device and applies its stored settings. nullptr with
    // `error` (the plugin's last_error) on failure.
    std::shared_ptr<RadioDevice> open(const std::string& plugin_id, const std::string& key,
                                      std::string& error);

    // Context callbacks a plugin made, by "function:RESULT" (debug window,
    // tests), and the on_context_event calls delivered per YR_CONTEXT_*.
    std::map<std::string, std::uint64_t> context_calls(const std::string& plugin_id) const;
    std::map<std::uint32_t, std::uint64_t> context_notifications(const std::string& plugin_id) const;
    // Waits until the plugin's queued context notifications were delivered.
    bool wait_context_idle(const std::string& plugin_id, std::chrono::milliseconds timeout) const;
    // Waits until a control call that overran its limit has returned (the
    // plugin stays failed; tests release a hanging plugin with this).
    bool wait_calls_returned(const std::string& plugin_id, std::chrono::milliseconds timeout) const;
    // devices_changed and settings_changed calls of a plugin so far.
    std::uint64_t devices_changed_count(const std::string& plugin_id) const;
    std::uint64_t settings_changed_count(const std::string& plugin_id) const;

    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
};

}  // namespace yantra::radio
