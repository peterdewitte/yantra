// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The headless runner (PLG-24, API-01, API-03, DEMO-10), see runner.h.
// Command set and replies copied from yantra-v1 c24e8f5:
// core/engine/probe.cpp (status/health fields, the command handlers),
// rewritten for the core's radio host, pipeline, recorder and replay.

#include "yantra/core/headless/runner.h"

#include <algorithm>
#include <chrono>
#include <system_error>

#include "yantra/core/build_info.h"
#include "yantra/core/calibration/corrector.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/measure/catalog.h"
#include "yantra/core/measure/feature_feed.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/radio/radio_host.h"
#include "yantra/core/recording/format.h"
#include "yantra/core/recording/replay.h"
#include "yantra/core/recording/writer.h"

namespace yantra::headless {

namespace fs = std::filesystem;
using control::Command;
using control::CommandId;
using control::CommandResult;
using nlohmann::json;

namespace {

// The newest cycle's carriers, for the status (any thread).
struct CycleCounter final : measure::MeasurementObserver {
    mutable std::mutex m;
    std::uint64_t cycles = 0;
    std::size_t carriers = 0;
    std::size_t synced = 0;
    void on_cycle(const measure::MeasurementCycle& c) override {
        std::size_t s = 0;
        for (const auto& cs : c.samples)
            if ((cs.flags & measure::sample_flag::kNoSync) == 0) ++s;
        const std::lock_guard lock(m);
        ++cycles;
        carriers = c.samples.size();
        synced = s;
    }
};

double seconds(std::int64_t ns) { return static_cast<double>(ns) / 1e9; }

}  // namespace

struct Runner::Impl {
    RunnerConfig config;
    std::shared_ptr<plugin::PluginStateStore> state;
    std::shared_ptr<plugin::PluginLoader> loader;
    std::shared_ptr<measure::TechnologyCatalog> catalog;
    std::shared_ptr<measure::FeatureFeed> feed;
    std::shared_ptr<CycleCounter> counter = std::make_shared<CycleCounter>();
    std::shared_ptr<measure::MeasurementPipeline> pipeline;
    std::unique_ptr<radio::RadioHost> host;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    std::mutex command_mutex;  // one command at a time
    bool closing = false;      // the runner is going: commands refused (under command_mutex)
    mutable std::mutex mutex;  // the parts below (status reads them)
    std::shared_ptr<radio::RadioDevice> device;
    std::unique_ptr<measure::CycleRunner> cycles;
    std::string device_plugin, device_key, device_label;
    bool device_simulated = false;
    std::shared_ptr<recording::Recorder> recorder;
    std::vector<fs::path> last_files;  // of the last stopped session
    std::shared_ptr<recording::Replay> replay;
    std::unique_ptr<recording::ReplayPlayer> player;
    std::string replay_source;
    bool sound = false;
    std::string last_error;

    void log(std::uint32_t level, const std::string& source, const std::string& message) const {
        if (config.log) config.log(level, source, message);
    }

    // --- radio ---------------------------------------------------------------

    struct Choice {
        std::string plugin, key, label;
        bool simulated = false;
    };

    // The device monitor.on opens (RunnerConfig::radio, demo).
    std::string choose(Choice& out) const {
        std::string want_plugin = config.radio, want_key;
        if (const auto colon = want_plugin.find(':'); colon != std::string::npos) {
            want_key = want_plugin.substr(colon + 1);
            want_plugin.resize(colon);
        }
        std::string problems;
        for (const radio::RadioPluginEntry& e : host->list()) {
            if (!want_plugin.empty() && e.id != want_plugin) continue;
            if (e.status != radio::RadioPluginStatus::Enabled) {
                if (!e.message.empty()) problems += "; " + e.path.filename().string() + ": " + e.message;
                continue;
            }
            const radio::EnumerateResult r = host->enumerate(e.id);
            if (r.devices.empty() && !r.diagnostic.empty()) problems += "; " + e.name + ": " + r.diagnostic;
            for (const radio::DeviceInfo& d : r.devices) {
                if (!want_key.empty() && d.key != want_key) continue;
                const bool simulated =
                    (e.capabilities & YR_CAP_SIMULATED) != 0 || (d.flags & YR_DEVICE_SIMULATED) != 0;
                if (config.demo && !simulated) continue;
                if (d.flags & (YR_DEVICE_IN_USE | YR_DEVICE_NO_PERMISSION)) continue;
                out = {e.id, d.key, e.name + " (" + (d.label.empty() ? d.key : d.label) + ")", simulated};
                return {};
            }
        }
        std::string what = config.demo ? "no simulated radio found (demo mode needs the simulator radio plugin)"
                                       : "no radio found";
        if (!config.radio.empty()) what += " for '" + config.radio + "'";
        return what + problems;
    }

    CommandResult monitor_on() {
        if (device) return CommandResult::success({{"monitoring", true}, {"radio", device_label}});
        stop_replay();
        Choice c;
        if (std::string why = choose(c); !why.empty()) return CommandResult::failure(why);
        std::string error;
        auto d = host->open(c.plugin, c.key, error);
        if (!d) return CommandResult::failure("the radio could not be opened: " + error);
        radio::Plan plan;
        plan.type = YR_PLAN_SERVING;
        if (const yr_status st = d->start(plan); st != YR_OK) {
            const std::string why = d->last_error();
            d->close();
            return CommandResult::failure("the radio did not start: " +
                                          (why.empty() ? "error " + std::to_string(st) : why));
        }
        const std::lock_guard lock(mutex);
        device = std::move(d);
        device_plugin = c.plugin;
        device_key = c.key;
        device_label = c.label;
        device_simulated = c.simulated;
        cycles = std::make_unique<measure::CycleRunner>(*pipeline);
        log(YANTRA_LOG_INFO, "", "monitoring " + c.label);
        return CommandResult::success({{"monitoring", true}, {"radio", c.label}, {"simulated", c.simulated}});
    }

    CommandResult monitor_off() {
        record_stop();
        std::shared_ptr<radio::RadioDevice> d;
        {
            const std::lock_guard lock(mutex);
            d = std::move(device);
            cycles.reset();
        }
        if (d) {
            (void)d->stop();
            d->close();
            log(YANTRA_LOG_INFO, "", "monitoring stopped");
        }
        return CommandResult::success({{"monitoring", false}});
    }

    // --- recording -------------------------------------------------------------

    CommandResult record_start(const std::string& name) {
        {
            const std::lock_guard lock(mutex);
            if (recorder) return CommandResult::success(recorder_json());
            if (!device) return CommandResult::failure("start monitoring first: a recording holds its measurements");
        }
        recording::RecorderConfig c;
        c.folder = config.recording_folder;
        c.compression = config.compress ? recording::Compression::Zstd : recording::Compression::None;
        c.split_minutes = config.split_minutes;
        c.ring_files = config.keep_files;
        {
            const std::lock_guard lock(mutex);
            recording::RadioInfo r;
            r.plugin_id = device_plugin;
            r.device_key = device_key;
            r.label = device_label;
            if (auto e = host->entry(device_plugin)) r.plugin_version = e->version;
            c.info.radios.push_back(r);
        }
        c.info.notes = "headless runner (yantra-probe)";
        std::shared_ptr<recording::Recorder> r;
        try {
            r = recording::Recorder::create(pipeline, c);
        } catch (const std::exception& e) {
            return CommandResult::failure(std::string("the recording could not start: ") + e.what());
        }
        std::string error;
        const bool ok = name.empty() ? r->quick_record(error) : r->start(name, error);
        if (!ok) {
            r->detach();
            return CommandResult::failure("the recording could not start: " + error);
        }
        const std::lock_guard lock(mutex);
        recorder = r;
        log(YANTRA_LOG_INFO, "", "recording " + r->status().name + " into " + c.folder.string());
        return CommandResult::success(recorder_json());
    }

    void record_stop() {
        std::shared_ptr<recording::Recorder> r;
        {
            const std::lock_guard lock(mutex);
            r = std::move(recorder);
        }
        if (!r) return;
        const recording::RecorderStatus st = r->status();
        r->stop();
        r->detach();
        const std::lock_guard lock(mutex);
        last_files = st.files;
        log(YANTRA_LOG_INFO, "", "recording " + st.name + " stopped (" + std::to_string(st.files.size()) + " files)");
    }

    // Caller holds `mutex` (or the recorder is private).
    json recorder_json() const {
        json j = {{"recording", recorder != nullptr}};
        if (!recorder) return j;
        const recording::RecorderStatus st = recorder->status();
        j["state"] = recording::recorder_state_name(st.state);
        j["name"] = st.name;
        j["file"] = st.file.filename().string();
        j["files"] = st.files.size();
        j["durable_inputs"] = st.durable_inputs;
        j["dropped"] = st.dropped;
        j["ring_deleted"] = st.ring_deleted;
        if (!st.error.empty()) j["error"] = st.error;
        return j;
    }

    CommandResult rotate() {
        const std::lock_guard lock(mutex);
        if (!recorder) return CommandResult::failure("no recording runs");
        if (recorder->marker("rotate", recording::MarkerAction::NewFile) == 0)
            return CommandResult::failure("the recording could not start its next file");
        return CommandResult::success(recorder_json());
    }

    // --- replay ----------------------------------------------------------------

    std::vector<fs::path> play_files(const std::string& source, std::string& error) const {
        fs::path p(source);
        if (p.is_relative() && !fs::exists(p)) p = config.recording_folder / p;
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return {p};
        if (!fs::is_directory(p, ec)) {
            error = "no such recording file or folder: " + source;
            return {};
        }
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(p, ec)) {
            const std::string n = e.path().filename().string();
            const auto ends = [&](const std::string& x) {
                return n.size() > x.size() && n.compare(n.size() - x.size(), x.size(), x) == 0;
            };
            if (e.is_regular_file() && (ends(recording::kExtension) || ends(recording::kCompressedExtension)))
                files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
        if (files.empty()) error = "no recording files in " + p.string();
        return files;
    }

    CommandResult play(const std::string& source, const std::string& speed_text) {
        double speed = 1.0;
        if (!speed_text.empty()) {
            try {
                std::size_t used = 0;
                speed = std::stod(speed_text, &used);
                if (used != speed_text.size()) throw std::invalid_argument("x");
            } catch (const std::exception&) {
                return CommandResult::failure("speed must be a number (1 = real time, 0 = as fast as possible)");
            }
            if (!(speed == 0.0 || (speed >= 0.1 && speed <= 100.0)))
                return CommandResult::failure("speed must be 0 or 0.1 to 100");
        }
        std::string error;
        const std::vector<fs::path> files = play_files(source, error);
        if (files.empty()) return CommandResult::failure(error);
        monitor_off();
        stop_replay();
        recording::ReplayOptions o;
        o.catalog = catalog;
        o.log = [this](const std::string& m) { log(YANTRA_LOG_WARNING, "", "replay: " + m); };
        std::shared_ptr<recording::Replay> r = recording::Replay::open(files, o, error);
        if (!r) return CommandResult::failure("the recording could not be opened: " + error);
        r->add_observer(feed);
        r->add_observer(counter);
        auto p = std::make_unique<recording::ReplayPlayer>(r);
        p->play(speed);
        const std::lock_guard lock(mutex);
        replay = std::move(r);
        player = std::move(p);
        replay_source = source;
        log(YANTRA_LOG_INFO, "", "playing " + source);
        return CommandResult::success(replay_json());
    }

    void stop_replay() {
        std::unique_ptr<recording::ReplayPlayer> p;
        std::shared_ptr<recording::Replay> r;
        {
            const std::lock_guard lock(mutex);
            p = std::move(player);
            r = std::move(replay);
        }
        p.reset();  // joins its thread before the replay goes
    }

    // Caller holds `mutex`.
    json replay_json() const {
        json j = {{"playing", player != nullptr}};
        if (!player) return j;
        j["state"] = recording::player_state_name(player->state());
        j["source"] = replay_source;
        j["position_s"] = seconds(player->position_ns());
        j["duration_s"] = seconds(replay->duration_ns());
        j["speed"] = player->speed();
        return j;
    }

    // --- status ----------------------------------------------------------------

    json status() const {
        const core::BuildInfo& b = core::build_info();
        json j;
        j["application"] = "yantra-probe";
        j["version"] = std::string(b.version_string);
        j["source_url"] = std::string(b.source_url);
        j["uptime_s"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        j["sound"] = false;
        json plugins = json::array();
        if (loader)
            for (const plugin::PluginEntry& e : loader->list())
                plugins.push_back({{"id", e.id}, {"version", e.version}, {"status", plugin::status_text(e.status)}});
        j["plugins"] = plugins;
        j["recording_folder"] = config.recording_folder.string();
        {
            const std::lock_guard lock(counter->m);
            j["cycles"] = counter->cycles;
            j["carriers"] = counter->carriers;
            j["synced_carriers"] = counter->synced;
        }
        const std::lock_guard lock(mutex);
        j["sound"] = sound;
        const bool playing = player != nullptr;
        j["state"] = recorder ? "recording" : device ? "monitoring" : playing ? "playing" : "stopped";
        j["monitoring"] = device != nullptr;
        j["demo"] = config.demo || (device && device_simulated);
        if (device) {
            const radio::DeviceStats ds = device->stats();
            j["radio"] = {{"plugin", device_plugin},
                          {"device", device_key},
                          {"label", device_label},
                          {"simulated", device_simulated},
                          {"state", radio::state_text(device->state())},
                          {"records", ds.sink.delivered},
                          {"dropped", ds.sink.dropped},
                          {"reopens", ds.reopens}};
        } else {
            j["radio"] = nullptr;
        }
        j["recording"] = recorder_json();
        j["play"] = replay_json();
        if (pipeline) {
            const measure::PipelineStats ps = pipeline->stats();
            j["pipeline"] = {{"records", ps.records}, {"accepted", ps.accepted}, {"cycles", ps.cycles},
                             {"late", ps.late}, {"implausible", ps.implausible}};
        }
        if (!last_error.empty()) j["last_error"] = last_error;
        return j;
    }

    CommandResult run(const Command& c, std::atomic<bool>& quit) {
        switch (c.id) {
        case CommandId::RecordStart: return record_start(c.arg("name"));
        case CommandId::RecordStop: {
            record_stop();
            const std::lock_guard lock(mutex);
            json j = {{"recording", false}};
            json files = json::array();
            for (const fs::path& f : last_files) files.push_back(f.filename().string());
            j["files"] = files;
            return CommandResult::success(j);
        }
        case CommandId::MonitorOn: return monitor_on();
        case CommandId::MonitorOff: return monitor_off();
        case CommandId::Play: return play(c.arg("file"), c.arg("speed"));
        case CommandId::SoundOn:
        case CommandId::SoundOff: {
            const std::lock_guard lock(mutex);
            sound = c.id == CommandId::SoundOn;
            return CommandResult::success({{"sound", sound}, {"output", "none (headless)"}});
        }
        case CommandId::Status: return CommandResult::success(status());
        case CommandId::Rotate: return rotate();
        case CommandId::Quit:
            record_stop();
            quit = true;
            return CommandResult::success({{"quit", true}});
        }
        return CommandResult::failure("not supported");
    }
};

Runner::Runner(RunnerConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
    impl_->sound = impl_->config.sound;
}

Runner::~Runner() {
    Impl& m = *impl_;
    // No more commands from the plugins (PLG-24): a command still running
    // finishes first, later ones are refused.
    if (m.loader) m.loader->set_controller(nullptr);
    {
        const std::lock_guard lock(m.command_mutex);
        m.closing = true;
    }
    // The measurements stop before the plugins go: a radio's stream thread
    // maps its records with the technology plugins, whose modules Windows
    // unmaps when they are unloaded (a crash at exit, HW-06).
    m.record_stop();
    m.stop_replay();
    m.monitor_off();
    if (m.loader) m.loader->unload_all();
    m.host.reset();
}

bool Runner::start(std::string& error) {
    Impl& m = *impl_;
    m.state = m.config.state ? m.config.state : std::make_shared<plugin::MemoryStateStore>();
    std::error_code ec;
    if (!m.config.recording_folder.empty()) fs::create_directories(m.config.recording_folder, ec);

    plugin::PluginLoaderConfig lc;
    lc.folders = m.config.plugin_folders;
    lc.state = m.state;
    lc.data_root = m.config.data_root;
    lc.log = [&m](const std::string& id, std::uint32_t level, const std::string& msg) { m.log(level, id, msg); };
    lc.notify = lc.log;
    m.loader = std::make_shared<plugin::PluginLoader>(std::move(lc));
    m.loader->set_controller(std::shared_ptr<plugin::Controller>(this, [](plugin::Controller*) {}));
    // Technology plugins first: the catalog is built before the features start.
    m.loader->load_all();

    m.catalog = std::make_shared<measure::TechnologyCatalog>();
    for (const plugin::PluginEntry& e : m.loader->list()) {
        if (e.status != plugin::PluginStatus::Enabled) continue;
        if (const plugin::PluginInfo* info = m.loader->info(e.id))
            (void)m.catalog->add(*info, m.loader->descriptor(e.id), m.loader->instance(e.id));
    }
    measure::MeasurementPipeline::Options o;
    o.catalog = m.catalog;
    o.log = [&m](const std::string& msg) { m.log(YANTRA_LOG_WARNING, "", msg); };
    if (!m.config.calibration_table.empty()) {
        auto t = std::make_shared<calibration::Table>();
        std::string cal_error;
        if (calibration::Table::load_if_present(m.config.calibration_table, *t, cal_error) && cal_error.empty())
            o.corrector = std::make_shared<calibration::TableCorrector>(t, calibration::AntennaSettings{});
        else if (!cal_error.empty())
            m.log(YANTRA_LOG_WARNING, "", "calibration table not used: " + cal_error);
    }
    m.pipeline = std::make_shared<measure::MeasurementPipeline>(std::move(o));
    m.feed = std::make_shared<measure::FeatureFeed>(m.loader);
    m.pipeline->add_observer(m.feed);
    m.pipeline->add_observer(m.counter);

    radio::RadioHostConfig hc;
    hc.folders = m.config.radio_folders;
    hc.state = m.state;
    hc.pipeline = m.pipeline;
    hc.log = [&m](const std::string& id, const std::string& dev, std::uint32_t level, const std::string& msg) {
        m.log(level, dev.empty() ? id : id + "/" + dev, msg);
    };
    m.host = std::make_unique<radio::RadioHost>(std::move(hc));
    m.host->load_all();

    const auto apply = [&](CommandId id, std::map<std::string, std::string> args = {}) {
        Command c;
        c.id = id;
        c.args = std::move(args);
        const json reply = execute(c);
        if (!reply.value("ok", false)) {
            error = std::string(control::command_name(id)) + ": " + reply.value("error", std::string("failed"));
            return false;
        }
        return true;
    };
    if (m.config.sound && !apply(CommandId::SoundOn)) return false;
    if ((m.config.monitor || m.config.record) && !apply(CommandId::MonitorOn)) return false;
    if (m.config.record) {
        std::map<std::string, std::string> args;
        if (!m.config.record_name.empty()) args["name"] = m.config.record_name;
        if (!apply(CommandId::RecordStart, args)) return false;
    }
    return true;
}

void Runner::poll() {
    if (impl_->loader) impl_->loader->poll();
}

json Runner::execute(const Command& command) {
    Impl& m = *impl_;
    const std::lock_guard lock(m.command_mutex);
    CommandResult r;
    try {
        r = m.closing ? CommandResult::failure("the runner is closing") : m.run(command, quit_);
    } catch (const std::exception& e) {
        r = CommandResult::failure(e.what());
    }
    if (!r.ok) {
        const std::lock_guard l(m.mutex);
        m.last_error = std::string(control::command_name(command.id)) + ": " + r.error;
    }
    return control::reply_json(control::command_name(command.id), r);
}

json Runner::execute_line(const std::string& line) {
    std::string error;
    const auto c = control::parse_command_any(line, &error);
    if (!c) return control::reply_json("", CommandResult::failure(error));
    return execute(*c);
}

std::string Runner::control(const std::string& plugin_id, const std::string& request) {
    json reply;
    try {
        std::string error;
        const auto c = control::parse_command_json(json::parse(request), &error);
        reply = c ? execute(*c) : control::reply_json("", CommandResult::failure(error));
    } catch (const json::exception& e) {
        reply = control::reply_json("", CommandResult::failure(std::string("not a JSON request: ") + e.what()));
    }
    if (!reply.value("ok", false))
        impl_->log(YANTRA_LOG_DEBUG, plugin_id, "command refused: " + reply.value("error", std::string()));
    return reply.dump();
}

json Runner::status() const { return impl_->status(); }

plugin::PluginLoader& Runner::loader() { return *impl_->loader; }

}  // namespace yantra::headless
