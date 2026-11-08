// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The headless runner (PLG-24, API-01, API-03, DEMO-10; OA-01: the core
// library used without a UI): the plugin.h plugins (technology plugins for
// the schema mapping, feature plugins such as the probe), the radio host
// with one radio, the measurement pipeline and its cycle thread, the
// recorder and replay - without any window. All control goes through the
// command set of control/command.h: the start-up state of RunnerConfig,
// execute() (the runner's command line and standard input,
// tools/yantra-probe) and, as the loader's plugin::Controller, the plugin
// API's `control` service (the probe's REST/WebSocket API). Every
// accepted record goes to the feature plugins (measure/feature_feed.h).
//
// Fixed-probe mode (API-03): recording from the start, files split by
// time (REC-02) and only the newest kept (REC-09 ring), "rotate" for the
// next file now; health summaries are the "status" result, which the
// probe plugin writes and sends periodically with its coverage summaries.
//
// Without a feature plugin that listens, the runner has no network
// listener (PLG-24). Technology- and radio-neutral (PLG-01): a radio is a
// plugin and device of the radio host; demo mode picks a device the
// plugin marks simulated (YR_CAP_SIMULATED or YR_DEVICE_SIMULATED).
//
// Threading: construct, start(), poll() and the destructor on the main
// thread; execute()/control() from any other thread (one command at a
// time); status() from any thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/core/control/command.h"
#include "yantra/core/plugin/loader.h"
#include "yantra/core/plugin/state_store.h"

namespace yantra::headless {

struct RunnerConfig {
    std::vector<std::filesystem::path> plugin_folders;  // plugin.h plugins (plugin/folders.h)
    std::vector<std::filesystem::path> radio_folders;   // radio plugins (<folder>/radios)
    std::shared_ptr<plugin::PluginStateStore> state;    // enabled state, settings; nullptr: in memory
    std::filesystem::path data_root;                    // plugins' own folders (data_folder)
    std::filesystem::path recording_folder;             // where recordings go
    std::filesystem::path calibration_table;            // RF-02; "" or missing: levels stay dBFS
    // The radio of monitor.on: "plugin_id" or "plugin_id:device_key"; ""
    // = the first usable device (in demo mode the first simulated one).
    std::string radio;
    bool demo = false;          // DEMO-10: a simulated radio only
    bool monitor = true;        // monitor.on at start
    bool record = false;        // record.start at start (fixed probe, API-03)
    std::string record_name;    // "" = a name from the date and time
    double split_minutes = 0;   // REC-02: 0 off, else 1..60
    std::uint32_t keep_files = 0;  // REC-09: > 0 keeps the newest N files (needs split_minutes)
    bool compress = false;      // zstd (REC-03)
    bool sound = false;         // sound.on at start (kept; no sound output)
    // Log lines of the runner, the plugins and the radios (any thread).
    std::function<void(std::uint32_t level, const std::string& source, const std::string& message)> log;
};

class Runner final : public plugin::Controller {
public:
    explicit Runner(RunnerConfig config);
    ~Runner() override;  // stops everything, unloads the plugins
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    // Loads the plugins, builds the pipeline and applies the start-up
    // state (monitoring, recording, sound). false with `error` when the
    // start-up state cannot be reached (e.g. no radio). Main thread.
    bool start(std::string& error);
    // One main-loop turn: applies plugin failures (PluginLoader::poll).
    void poll();
    // True after "quit".
    bool quit_requested() const noexcept { return quit_.load(); }

    // Runs one command; the reply object of control/command.h. Not on the
    // main thread while plugins may call control (one command at a time).
    nlohmann::json execute(const control::Command& command);
    // A text or JSON line (control::parse_command_any) -> reply.
    nlohmann::json execute_line(const std::string& line);
    // plugin::Controller: a JSON request from a plugin.
    std::string control(const std::string& plugin_id, const std::string& request) override;

    // The "status" result (health summary, API-03): state, radio,
    // recording, replay, pipeline counters, carriers, plugins, demo flag,
    // version and the source link (NFR-01).
    nlohmann::json status() const;

    plugin::PluginLoader& loader();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> quit_{false};
};

}  // namespace yantra::headless
