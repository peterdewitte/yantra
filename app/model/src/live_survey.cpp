// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The walk-test view's model, see live_survey.h.
#include "yantra/app/live_survey.h"
#include "yantra/app/i18n.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <cmath>
#include <ctime>
#include <exception>
#include <mutex>
#include <thread>
#include <variant>

#include <yantra/radio.h>

#include "yantra/core/calibration/corrector.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/context/building_context.h"
#include "yantra/core/measure/catalog.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/plugin/folders.h"
#include "yantra/core/plugin/loader.h"
#include "yantra/core/plugin/state_store.h"
#include "yantra/app/plugin_state_file.h"
#include "yantra/core/project/project.h"
#include "yantra/core/radio/radio_host.h"
#include "yantra/core/recording/cut.h"
#include "yantra/core/survey/recordings.h"
#include "yantra/core/survey/walk_test.h"

namespace yantra::app {

namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kWalkPeriodNs = 1'000'000'000;   // RF-01
constexpr std::int64_t kMousePeriodNs = 200'000'000;    // DEMO-14: a dense trail

// DEMO-14 practice calibration: the simulator's truth sensor (RAD-61), the
// span of its entry (the offset is the same at every carrier) and how long
// the start waits for the sensor's first value.
constexpr const char* kPracticeSensor = "true_offset_db";
constexpr double kPracticeLowHz = 1e6;
constexpr double kPracticeHighHz = 6e9;
constexpr double kPracticeWaitS = 5.0;

// A walk's name as a recording session name (REC-03).
std::string session_name_of(const std::string& walk) {
    std::string n;
    for (const char c : walk) {
        const auto u = static_cast<unsigned char>(c);
        n += (c == '/' || c == '\\' || c == ':' || u < 0x20 || u == 0x7f) ? '_' : (c == ' ' ? '_' : c);
    }
    if (n.size() > 100) n.resize(100);
    return n.empty() ? std::string("walk") : n;
}

std::string default_name(WalkKind k) {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return std::string(k == WalkKind::MouseSurvey ? tr("Mouse survey ") : k == WalkKind::Manual ? tr("Manual points ") : tr("Walk ")) + buf;
}

}  // namespace

const char* walk_kind_key(WalkKind k) noexcept {
    switch (k) {
    case WalkKind::PointAndClick: return "point_and_click";
    case WalkKind::Continuous: return "continuous";
    case WalkKind::Manual: return "manual";
    case WalkKind::MouseSurvey: return "mouse_survey";
    }
    return "point_and_click";
}

const char* walk_kind_label(WalkKind k) noexcept {
    switch (k) {
    case WalkKind::PointAndClick: return tr("Point and click");
    case WalkKind::Continuous: return tr("Continuous");
    case WalkKind::Manual: return tr("Manual points");
    case WalkKind::MouseSurvey: return tr("Mouse survey");
    }
    return "";
}

struct LiveSurvey::Impl {
    std::shared_ptr<project::Project> project;
    RadioPaths paths;
    int token = 0;
    std::shared_ptr<std::atomic<bool>> building_dirty = std::make_shared<std::atomic<bool>>(true);

    // Technology plugins (schema mapping, C1) and the plugin settings.
    PluginServices services;  // given by the app (empty: own ones)
    std::shared_ptr<plugin::PluginStateStore> state;
    std::shared_ptr<plugin::PluginLoader> loader;
    std::shared_ptr<measure::TechnologyCatalog> catalog = std::make_shared<measure::TechnologyCatalog>();
    std::shared_ptr<const calibration::Table> table;
    // DEMO-14: the simulated practice calibration of a running mouse survey
    // (in memory; never in the user's table, never for a real radio).
    bool practice_wanted = false;
    std::optional<calibration::Entry> practice;
    std::chrono::steady_clock::time_point practice_read{};
    // Observers of every pipeline (radio windows) and the device generation.
    std::vector<std::shared_ptr<measure::MeasurementObserver>> observers;
    std::uint64_t device_generation = 0;
    bool connected = false;  // connect(): the device stays open without measuring

    // The plugins' context.
    std::shared_ptr<context::ContextPublisher> publisher = std::make_shared<context::ContextPublisher>();
    context::BuildingContext context{publisher, radio::SceneExportOptions{}};
    std::optional<std::string> shown;

    // The radio host behind one pipeline (rebuilt when the period changes).
    std::int64_t period_ns = 0;
    std::shared_ptr<measure::MeasurementPipeline> pipeline;
    std::unique_ptr<radio::RadioHost> host;
    std::shared_ptr<radio::RadioDevice> device;
    std::unique_ptr<measure::CycleRunner> runner;

    // The walk.
    std::shared_ptr<survey::WalkTest> walk;
    WalkKind kind = WalkKind::PointAndClick;
    WalkForm form;  // as started
    std::string radio_label;
    std::optional<Point2> last_click, open_start, manual_point;
    std::size_t segments = 0;

    // The cursor (DEMO-14 speed).
    std::optional<Point2> cursor_m;
    double cursor_t = 0.0;
    double speed = 0.0;

    // Monitoring, recording, voice notes, ring buffer (REC-01..03, REC-05/06/09).
    std::shared_ptr<SurveyMonitor> monitor = std::make_shared<SurveyMonitor>();
    std::string device_label;
    bool radio_by_walk = false;      // the walk started the radio: it stops it
    bool recording_by_walk = false;  // the walk started the recording: it stops it
    bool unique_name = false;        // record(): a name in use gets _2, _3 ... (a walk's file)
    std::shared_ptr<recording::Recorder> recorder;
    std::shared_ptr<recording::Recorder> ring;
    std::vector<fs::path> ring_kept;  // the ring's files when it stopped
    std::shared_ptr<AudioIo> audio;
    std::int64_t voice_t0 = 0;
    bool voice = false;

    ~Impl() {
        stop_walk();
        end_recording();
        stop_ring();
        close_radio();
        if (token != 0) project->unsubscribe(token);
    }

    bool running_radio() const { return device && runner; }

    void close_radio() {
        stop_ring();
        if (device) {
            (void)device->stop();
            device->close();
            ++device_generation;
        }
        runner.reset();
        device.reset();
        device_label.clear();
        radio_by_walk = false;
    }

    // Stops measuring; a connected radio stays open (its windows go on).
    void release_radio() {
        if (!connected || !device) {
            close_radio();
            return;
        }
        stop_ring();
        (void)device->stop();
        runner.reset();
        radio_by_walk = false;
    }

    fs::path recordings_folder() const { return project->path().parent_path() / "recordings"; }

    recording::HeaderInfo header_info(const std::string& notes) const {
        recording::HeaderInfo i;
        if (device) {
            recording::RadioInfo r;
            r.plugin_id = device_plugin;
            r.device_key = device_key;
            for (const radio::RadioPluginEntry& e : host->list())
                if (e.id == device_plugin) r.plugin_version = e.version;
            r.label = device_label;
            i.radios.push_back(r);
            // REC-03, RF-02: the calibration entry in use.
            const auto key = device->calibration_key();
            const auto t = effective_table();
            if (const calibration::Entry* e = key && t ? t->find(*key) : nullptr)
                i.calibration.push_back({key->id(), e->created_unix, e->valid_days, e->method, e->simulated});
        }
        recording::ProjectLink link;
        link.file = project->path().filename().string();
        link.floor_id = shown.value_or("");
        i.project = link;
        if (!project->cell_database().entries().empty()) i.cell_db = "project";
        i.notes = notes;
        return i;
    }
    std::string device_plugin, device_key;

    // Lists a stopped session's files in the project (REC-02, IND-20).
    std::string list_files(const std::vector<fs::path>& files) {
        std::string problems;
        for (const fs::path& f : files) {
            try {
                (void)survey::add_recording_file(*project, f, "recordings/" + f.filename().string());
            } catch (const project::ProjectError& e) {
                if (e.code() != project::ProjectError::Code::Exists)
                    problems += (problems.empty() ? "" : "; ") + f.filename().string() + ": " + e.what();
            } catch (const std::exception& e) {
                problems += (problems.empty() ? "" : "; ") + f.filename().string() + ": " + e.what();
            }
        }
        return problems;
    }

    // Stops the recording session, lists its files; the number of files.
    std::size_t end_recording(std::string* problems = nullptr) {
        if (!recorder) return 0;
        if (voice && audio) (void)audio->stop_capture();
        voice = false;
        const recording::RecorderStatus st = recorder->status();
        recorder->detach();
        recorder.reset();
        recording_by_walk = false;
        const std::string p = list_files(st.files);
        if (problems) *problems = p;
        monitor->note(EventKind::Recording, tr("Recording ") + st.name + tr(" stopped (") + std::to_string(st.files.size()) +
                                                (st.files.size() == 1 ? " file)." : " files)."));
        return st.files.size();
    }

    void stop_ring() {
        if (!ring) return;
        ring_kept = ring->status().files;
        ring->detach();
        ring.reset();
    }

    // Opens the radio (with this cycle period's host) unless it is open.
    std::string open_device(const RadioChoice& radio, std::int64_t period) {
        ensure_host(period);
        if (device && device_plugin == radio.plugin_id && device_key == radio.device_key) return {};
        if (device) close_radio();
        std::string error;
        device = host->open(radio.plugin_id, radio.device_key, error);
        if (!device) return tr("The radio could not be opened: ") + error;
        ++device_generation;
        device_label = radio.label;
        device_plugin = radio.plugin_id;
        device_key = radio.device_key;
        return {};
    }

    // Opens and starts the radio with this cycle period.
    std::string open_radio(const RadioChoice& radio, std::int64_t period) {
        if (std::string e = open_device(radio, period); !e.empty()) return e;
        runner = std::make_unique<measure::CycleRunner>(*pipeline);
        radio::Plan plan;
        plan.type = YR_PLAN_SERVING;
        if (const yr_status st = device->start(plan); st != YR_OK) {
            const std::string why = device->last_error();
            release_radio();
            return tr("The radio did not start: ") + (why.empty() ? std::string("error ") + std::to_string(st) : why);
        }
        return {};
    }

    void stop_walk() {
        if (!walk) return;
        try {
            if (walk->running()) walk->finish(pipeline->clock().monotonic_ns());
        } catch (const std::exception&) {
        }
        if (pipeline) pipeline->remove_observer(walk);
        walk.reset();
    }

    void ensure_state() {
        if (state) return;
        if (services.state) {
            state = services.state;
            return;
        }
        if (!paths.plugin_state.empty()) state = std::make_shared<PluginStateFile>(paths.plugin_state);
        else state = std::make_shared<plugin::MemoryStateStore>();
    }

    void ensure_loader() {
        if (loader) return;
        ensure_state();
        if (services.loader) {
            loader = services.loader;
        } else {
            plugin::PluginLoaderConfig c;
            c.folders = paths.plugin_folders;
            c.state = state;
            loader = std::make_shared<plugin::PluginLoader>(std::move(c));
            loader->load_all();
        }
        fill_catalog();
        if (!paths.calibration_table.empty()) {
            auto t = std::make_shared<calibration::Table>();
            std::string error;
            if (calibration::Table::load_if_present(paths.calibration_table, *t, error) && error.empty()) table = t;
        }
    }

    // The table the levels are corrected with: the user's, plus the mouse
    // survey's practice entry.
    std::shared_ptr<const calibration::Table> effective_table() const {
        if (!practice) return table;
        auto t = table ? std::make_shared<calibration::Table>(*table) : std::make_shared<calibration::Table>();
        t->put(*practice);
        return t;
    }

    void apply_corrector() {
        if (!pipeline) return;
        std::shared_ptr<const measure::LevelCorrector> c;  // none: identity
        if (const auto t = effective_table()) c = std::make_shared<calibration::TableCorrector>(t, calibration::AntennaSettings{});
        pipeline->set_corrector(std::move(c));
    }

    // DEMO-14: with no calibration of the user's for the simulator's key
    // (HW-01b), the practice entry from the plugin's documented truth sensor
    // (RAD-61; docs/CALIBRATION.md "Making an entry"). The offset does not depend
    // on the carrier, so two points span every band. `wait_s`: the sensor
    // has a value once the stream runs its first interval, before its first
    // record. Follows a changed key or offset while the survey runs.
    void refresh_practice(double wait_s) {
        if (!practice_wanted || !device) return;
        const auto now = std::chrono::steady_clock::now();
        const auto key = device->calibration_key();
        if (key && table && table->find(*key) != nullptr) {
            if (practice) {
                practice.reset();
                apply_corrector();
            }
            return;
        }
        if (wait_s <= 0.0 && practice && key && practice->key == *key && now - practice_read < std::chrono::seconds(1)) return;
        practice_read = now;
        const auto end = now + std::chrono::duration<double>(wait_s);
        plugin::Value v;
        while ((device->read_sensor(kPracticeSensor, v) != YR_OK || !std::holds_alternative<double>(v) || !device->calibration_key()) &&
               std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto k = device->calibration_key();
        const double* offset = std::get_if<double>(&v);
        if (!k || offset == nullptr) return;
        if (practice && practice->key == *k && practice->points.front().offset_db == *offset) return;
        calibration::Entry e;
        e.key = *k;
        e.created_unix = calibration::now_unix();
        e.method = calibration::kMethodPractice;
        e.simulated = true;
        e.radio_plugin_id = device_plugin;
        for (const double hz : {kPracticeLowHz, kPracticeHighHz}) {
            calibration::Point p;
            p.frequency_hz = hz;
            p.offset_db = *offset;
            e.points.push_back(p);
        }
        practice = std::move(e);
        apply_corrector();
    }

    void end_practice() {
        practice_wanted = false;
        if (!practice) return;
        practice.reset();
        apply_corrector();
    }

    // The enabled technology plugins (schema mapping, C1).
    void fill_catalog() {
        catalog = std::make_shared<measure::TechnologyCatalog>();
        for (const plugin::PluginEntry& e : loader->list()) {
            if (e.status != plugin::PluginStatus::Enabled) continue;
            if (const plugin::PluginInfo* info = loader->info(e.id))
                (void)catalog->add(*info, loader->descriptor(e.id), loader->instance(e.id));
        }
    }

    // The host and pipeline with this cycle period.
    void ensure_host(std::int64_t period) {
        ensure_loader();
        if (host && period_ns == period) return;
        close_radio();
        host.reset();
        measure::MeasurementPipeline::Options o;
        o.config.period_ns = period;
        o.catalog = catalog;
        if (table) o.corrector = std::make_shared<calibration::TableCorrector>(table, calibration::AntennaSettings{});
        pipeline = std::make_shared<measure::MeasurementPipeline>(std::move(o));
        pipeline->add_observer(monitor);
        for (const auto& ob : observers) pipeline->add_observer(ob);
        radio::RadioHostConfig c;
        c.folders = paths.radio_folders;
        c.state = state;
        c.pipeline = pipeline;
        c.context = publisher;
        host = std::make_unique<radio::RadioHost>(std::move(c));
        host->load_all();
        period_ns = period;
    }
};

LiveSurvey::LiveSurvey(std::shared_ptr<project::Project> project, RadioPaths paths, PluginServices services)
    : impl_(std::make_unique<Impl>()) {
    impl_->project = std::move(project);
    impl_->paths = std::move(paths);
    impl_->services = std::move(services);
    std::weak_ptr<std::atomic<bool>> dirty = impl_->building_dirty;
    impl_->token = impl_->project->subscribe([dirty](const project::Change& c) {
        using K = project::Change::Kind;
        if (c.kind == K::Points || c.kind == K::Sessions || c.kind == K::Interpolations || c.kind == K::Saved) return;
        if (const auto d = dirty.lock()) d->store(true);
    });
}

LiveSurvey::~LiveSurvey() = default;

// -- radios ------------------------------------------------------------------------------

Result LiveSurvey::find_radios() {
    if (running()) return Result{false, tr("Stop the walk test first.")};
    if (radio_running()) return Result{false, tr("Stop the radio first.")};
    impl_->close_radio();
    impl_->connected = false;
    searched_ = true;
    radios_.clear();
    chosen_.reset();
    radio_note_.clear();
    try {
        impl_->ensure_host(impl_->period_ns != 0 ? impl_->period_ns : kWalkPeriodNs);
    } catch (const std::exception& e) {
        radio_note_ = std::string(tr("The radio plugins could not be loaded: ")) + e.what();
        return Result{false, radio_note_};
    }
    std::string problems;
    for (const radio::RadioPluginEntry& e : impl_->host->list()) {
        if (e.status != radio::RadioPluginStatus::Enabled) {
            if (e.status == radio::RadioPluginStatus::Failed || e.status == radio::RadioPluginStatus::Refused)
                problems += (problems.empty() ? "" : "; ") + e.path.filename().string() + ": " + e.message;
            continue;
        }
        const radio::EnumerateResult r = impl_->host->enumerate(e.id);
        if (r.devices.empty() && !r.diagnostic.empty())
            problems += (problems.empty() ? "" : "; ") + e.name + ": " + r.diagnostic;
        for (const radio::DeviceInfo& d : r.devices) {
            RadioChoice c;
            c.plugin_id = e.id;
            c.device_key = d.key;
            c.label = e.name + " (" + (d.label.empty() ? d.key : d.label) + ")";
            c.simulated = (e.capabilities & YR_CAP_SIMULATED) != 0 || (d.flags & YR_DEVICE_SIMULATED) != 0;
            c.uses_context = e.uses_context;
            if (d.flags & (YR_DEVICE_IN_USE | YR_DEVICE_NO_PERMISSION)) {
                c.usable = false;
                c.reason = d.flags_reason.empty() ? std::string(tr("The device is in use or not accessible.")) : d.flags_reason;
            }
            radios_.push_back(std::move(c));
        }
    }
    // The simulator by default when there is one (demo, tour), else the first usable.
    for (std::size_t i = 0; i < radios_.size() && !chosen_; ++i)
        if (radios_[i].usable && radios_[i].simulated && radios_[i].uses_context) chosen_ = i;
    for (std::size_t i = 0; i < radios_.size() && !chosen_; ++i)
        if (radios_[i].usable) chosen_ = i;
    if (radios_.empty()) {
        radio_note_ = tr("No radio found. Connect a radio, or enable a radio plugin (the simulator needs no hardware).");
        if (!problems.empty()) radio_note_ += " " + problems;
        return Result{false, radio_note_};
    }
    if (!problems.empty()) radio_note_ = problems;
    return Result{true, std::to_string(radios_.size()) + (radios_.size() == 1 ? tr(" radio found.") : tr(" radios found."))};
}

Result LiveSurvey::choose_radio(std::size_t index) {
    if (running()) return Result{false, tr("Stop the walk test first.")};
    if (radio_running()) return Result{false, tr("Stop the radio first.")};
    if (index >= radios_.size()) return Result{false, tr("There is no such radio.")};
    if (!radios_[index].usable) return Result{false, radios_[index].reason};
    if (chosen_ != index && impl_->device) {
        impl_->close_radio();
        impl_->connected = false;
    }
    chosen_ = index;
    return {};
}

// -- connection and the radio windows ------------------------------------------------------

Result LiveSurvey::connect() {
    Impl& m = *impl_;
    if (!searched_) return Result{false, tr("Find the radios first.")};
    const RadioChoice* r = chosen();
    if (r == nullptr) return Result{false, radio_note_.empty() ? std::string(tr("Choose a radio first.")) : radio_note_};
    if (m.device) {
        m.connected = true;
        return Result{true, r->label + tr(" is connected.")};
    }
    try {
        if (std::string e = m.open_device(*r, m.period_ns != 0 ? m.period_ns : kWalkPeriodNs); !e.empty())
            return Result{false, e};
    } catch (const std::exception& e) {
        return Result{false, std::string(tr("The radio could not be opened: ")) + e.what()};
    }
    m.connected = true;
    m.monitor->note(EventKind::Radio, r->label + " connected.");
    return Result{true, r->label + tr(" is connected.")};
}

Result LiveSurvey::disconnect() {
    Impl& m = *impl_;
    if (m.running_radio()) return Result{false, tr("Stop the radio first: it is measuring.")};
    if (!m.device) {
        m.connected = false;
        return Result{false, tr("No radio is connected.")};
    }
    const std::string label = m.device_label;
    m.close_radio();
    m.connected = false;
    m.monitor->note(EventKind::Radio, label + " disconnected.");
    return Result{true, label + " disconnected."};
}

bool LiveSurvey::connected() const { return impl_->device != nullptr; }
std::shared_ptr<radio::RadioDevice> LiveSurvey::device() const { return impl_->device; }
std::string LiveSurvey::device_plugin() const { return impl_->device ? impl_->device_plugin : std::string(); }
std::string LiveSurvey::device_key() const { return impl_->device ? impl_->device_key : std::string(); }
std::uint64_t LiveSurvey::device_generation() const { return impl_->device_generation; }
radio::RadioHost* LiveSurvey::host() { return impl_->host.get(); }

void LiveSurvey::add_observer(std::shared_ptr<measure::MeasurementObserver> observer) {
    if (!observer) return;
    impl_->observers.push_back(observer);
    if (impl_->pipeline) impl_->pipeline->add_observer(std::move(observer));
}

void LiveSurvey::remove_observer(const std::shared_ptr<measure::MeasurementObserver>& observer) {
    std::erase(impl_->observers, observer);
    if (impl_->pipeline) impl_->pipeline->remove_observer(observer);
}

std::shared_ptr<const calibration::Table> LiveSurvey::calibration_table() const { return impl_->table; }

std::optional<calibration::Entry> LiveSurvey::practice_calibration() const { return impl_->practice; }

Result LiveSurvey::set_calibration_table(const calibration::Table& table) {
    Impl& m = *impl_;
    if (!m.paths.calibration_table.empty()) {
        std::error_code ec;
        fs::create_directories(m.paths.calibration_table.parent_path(), ec);
        std::string error;
        if (!table.save(m.paths.calibration_table, error))
            return Result{false, tr("The calibration table could not be saved: ") + error};
    }
    m.table = std::make_shared<const calibration::Table>(table);
    m.practice.reset();  // a calibration of the user's wins (refresh_practice)
    m.apply_corrector();
    m.refresh_practice(0.0);
    return Result{true, m.paths.calibration_table.empty() ? std::string(tr("Calibration table updated (not saved: no file)."))
                                                          : tr("Calibration table saved to ") + m.paths.calibration_table.string() + "."};
}

const RadioPaths& LiveSurvey::paths() const { return impl_->paths; }

plugin::PluginStateStore& LiveSurvey::state() {
    impl_->ensure_state();
    return *impl_->state;
}

const RadioChoice* LiveSurvey::chosen() const { return chosen_ ? &radios_[*chosen_] : nullptr; }

Result LiveSurvey::load_radio_plugins() {
    Impl& m = *impl_;
    if (m.host) return Result{true, ""};
    try {
        m.ensure_host(m.period_ns != 0 ? m.period_ns : kWalkPeriodNs);
    } catch (const std::exception& e) {
        return Result{false, std::string(tr("The radio plugins could not be loaded: ")) + e.what()};
    }
    return Result{true, ""};
}

Result LiveSurvey::change_plugins(const std::function<Result()>& change) {
    Impl& m = *impl_;
    if (running()) return Result{false, tr("Stop the walk test first.")};
    if (recording()) return Result{false, tr("Stop the recording first.")};
    if (radio_running()) return Result{false, tr("Stop the radio first.")};
    m.ensure_loader();
    // Nothing may call a plugin while it changes: the device, the host
    // (radio plugins) and the pipeline (the catalog of technology plugins).
    m.close_radio();
    m.connected = false;
    m.host.reset();
    m.pipeline.reset();
    m.period_ns = 0;
    m.catalog = std::make_shared<measure::TechnologyCatalog>();
    ++m.device_generation;
    radios_.clear();
    chosen_.reset();
    searched_ = false;
    const Result r = change();
    m.fill_catalog();
    radio_note_ = tr("The plugins changed: find the radios again.");
    return r;
}

// -- context ----------------------------------------------------------------------------

void LiveSurvey::show_floor(const std::optional<std::string>& floor_id) {
    if (floor_id == impl_->shown) return;
    update();
    impl_->shown = floor_id;
    impl_->context.show_floor(floor_id);
    impl_->cursor_m.reset();
}

void LiveSurvey::update() {
    impl_->refresh_practice(0.0);
    if (impl_->building_dirty->exchange(false)) {
        impl_->monitor->set_cell_database(std::make_shared<celldb::CellDatabase>(impl_->project->cell_database()));
        impl_->context.set_building(impl_->project->building());
        if (impl_->shown) impl_->context.show_floor(impl_->shown);
    }
}

void LiveSurvey::cursor(std::optional<Point2> plan, double t_s) {
    update();
    impl_->context.set_cursor(plan);
    // The cursor's speed in plan-scale metres per second (DEMO-14),
    // smoothed over about a quarter of a second.
    std::optional<Point2> m;
    if (plan && impl_->shown) {
        const building::Building& b = impl_->context.building();
        if (const building::Floor* f = b.find_floor(*impl_->shown)) m = b.to_plan_metres(*f, *plan);
    }
    if (!m) {
        impl_->cursor_m.reset();
        impl_->speed = 0.0;
        return;
    }
    if (impl_->cursor_m && t_s > impl_->cursor_t) {
        const double dt = t_s - impl_->cursor_t;
        const double v = std::hypot(m->x - impl_->cursor_m->x, m->y - impl_->cursor_m->y) / dt;
        const double a = std::min(1.0, dt / 0.25);
        impl_->speed += a * (v - impl_->speed);
    }
    if (!impl_->cursor_m || t_s > impl_->cursor_t) {
        impl_->cursor_m = m;
        impl_->cursor_t = t_s;
    }
}

// -- walk test ---------------------------------------------------------------------------

bool LiveSurvey::running() const { return impl_->walk && impl_->walk->running(); }

std::string LiveSurvey::start_reason(const std::string& floor_id) const {
    if (running()) return tr("A walk test is running: stop it first.");
    const building::Building b = impl_->project->building();
    const building::Floor* f = b.find_floor(floor_id);
    if (f == nullptr) return tr("Choose a floor first.");
    if (!searched_) return tr("Find the radios first.");
    const RadioChoice* r = chosen();
    if (r == nullptr) return radio_note_.empty() ? std::string(tr("Choose a radio first.")) : radio_note_;
    if (form_.kind == WalkKind::MouseSurvey) {
        if (!(r->simulated && r->uses_context))
            return tr("The mouse survey needs the simulator as the radio: it calculates the levels where the mouse is.");
        if (!b.has_scale(*f)) return tr("The mouse survey needs a floor with a scale (Plan and scale...).");
    }
    if (impl_->recorder && impl_->period_ns != (form_.kind == WalkKind::MouseSurvey ? kMousePeriodNs : kWalkPeriodNs))
        return tr("Stop the recording first: the mouse survey measures every 0.2 s, walks every second.");
    if (form_.kind == WalkKind::Manual && !(form_.manual_seconds > 0.0 && form_.manual_seconds <= 600.0))
        return tr("The time per point must be between 0 and 600 s.");
    if (form_.kind == WalkKind::MouseSurvey &&
        !(form_.spacing_m >= 0.0 && form_.spacing_m <= 10.0 && form_.max_speed_mps > 0.0 && form_.max_speed_mps <= 50.0))
        return tr("The point spacing must be 0 to 10 m and the speed limit above 0 and at most 50 m/s.");
    return {};
}

Result LiveSurvey::start(const std::string& floor_id) {
    if (const std::string why = start_reason(floor_id); !why.empty()) return Result{false, why};
    if (!chosen_ || *chosen_ >= radios_.size()) return Result{false, tr("Choose a radio first.")};
    const RadioChoice radio = radios_[*chosen_];
    Impl& m = *impl_;
    m.kind = form_.kind;
    m.form = form_;
    const std::int64_t period = form_.kind == WalkKind::MouseSurvey ? kMousePeriodNs : kWalkPeriodNs;
    std::string note;
    try {
        // The radio: the one measuring already when it fits, else (re)started for the walk.
        const bool fits = m.running_radio() && m.period_ns == period && m.device_plugin == radio.plugin_id &&
                          m.device_key == radio.device_key;
        if (!fits) {
            if (m.running_radio()) m.close_radio();
            if (std::string e = m.open_radio(radio, period); !e.empty()) return Result{false, e};
            m.radio_by_walk = true;
            if (recording_form_.ring) (void)apply_ring();
        }
        // DEMO-14: dBm levels without the user's calibration. Only here, only
        // for a simulated radio (HW-01b); it ends with the survey.
        if (form_.kind == WalkKind::MouseSurvey && radio.simulated) {
            m.practice_wanted = true;
            m.refresh_practice(kPracticeWaitS);
        }
        show_floor(floor_id);
        update();
        survey::WalkTestOptions o;
        o.project = m.project;
        o.floor_id = floor_id;
        o.name = form_.name.empty() ? default_name(form_.kind) : form_.name;
        o.notes = form_.notes;
        o.radio = radio.label;
        switch (form_.kind) {
        case WalkKind::PointAndClick: o.kind = "walk"; o.mode = survey::WalkMode::PointAndClick; break;
        case WalkKind::Continuous: o.kind = "walk"; o.mode = survey::WalkMode::Continuous; break;
        case WalkKind::Manual: o.kind = "manual"; break;
        case WalkKind::MouseSurvey:
            o.kind = "simulated";
            o.device = radio.device_key;
            o.min_spacing_m = form_.spacing_m;
            break;
        }
        if (form_.kind != WalkKind::MouseSurvey) {
            // The walk position goes to the plugins (a context radio follows it).
            context::BuildingContext* ctx = &m.context;
            o.walker = [ctx](const std::optional<context::FloorPoint>& at) { ctx->set_walk(at); };
        }
        // REC-03: the walk's recording file (the running session, else one of its own).
        if (!m.recorder) {
            const RecordingForm keep = recording_form_;
            recording_form_.name = session_name_of(o.name);
            if (recording_form_.notes.empty()) recording_form_.notes = o.notes;
            m.unique_name = true;
            const Result r = record(false);
            m.unique_name = false;
            recording_form_ = keep;
            if (r.ok) m.recording_by_walk = true;
            else note = tr(" No recording file: ") + r.message;
        }
        o.recorder = m.recorder;
        if (m.practice) o.calibration = m.practice->key.id() + " (" + calibration::kMethodPractice + ", simulated)";
        m.walk = std::make_shared<survey::WalkTest>(o);
        m.pipeline->add_observer(m.walk);
        m.walk->start(m.pipeline->clock().monotonic_ns());
    } catch (const std::exception& e) {
        m.stop_walk();
        if (m.recording_by_walk) (void)m.end_recording();
        if (m.radio_by_walk) m.close_radio();
        m.end_practice();
        return Result{false, std::string(tr("The walk test could not start: ")) + e.what()};
    }
    m.radio_label = radio.label;
    m.last_click.reset();
    m.open_start.reset();
    m.manual_point.reset();
    m.segments = 0;
    return Result{true, std::string(walk_kind_label(form_.kind)) + tr(" started with ") + radio.label + "." + note};
}

Result LiveSurvey::click(Point2 plan) {
    Impl& m = *impl_;
    if (!running()) return Result{false, tr("Start a walk test first.")};
    const std::int64_t t = m.pipeline->clock().monotonic_ns();
    try {
        switch (m.kind) {
        case WalkKind::PointAndClick:
        case WalkKind::Continuous: {
            const bool closes = m.open_start.has_value();
            m.walk->click(t, plan);
            if (closes) ++m.segments;
            if (m.kind == WalkKind::Continuous || !closes) m.open_start = plan;
            else m.open_start.reset();
            m.last_click = plan;
            return Result{true, closes ? tr("Segment ") + std::to_string(m.segments) + " ended." : std::string(tr("Walk on; click where you are next."))};
        }
        case WalkKind::Manual:
            m.walk->point(t, plan, m.form.manual_seconds);
            m.manual_point = plan;
            m.last_click = plan;
            return Result{true, tr("Measuring here for ") + std::to_string(static_cast<int>(std::lround(m.form.manual_seconds))) + " s..."};
        case WalkKind::MouseSurvey: break;
        }
    } catch (const std::exception& e) {
        return Result{false, e.what()};
    }
    return Result{false, tr("In the mouse survey a click interpolates.")};
}

Result LiveSurvey::pause() {
    Impl& m = *impl_;
    if (!running() || (m.kind != WalkKind::PointAndClick && m.kind != WalkKind::Continuous))
        return Result{false, tr("Pause is for a walk (point and click or continuous).")};
    m.walk->pause(m.pipeline->clock().monotonic_ns());
    m.open_start.reset();
    return Result{true, tr("Paused: the open segment is dropped; click to start again.")};
}

Result LiveSurvey::delete_last() {
    Impl& m = *impl_;
    if (!running() || m.kind == WalkKind::MouseSurvey) return Result{false, tr("Delete last is for a walk or manual points.")};
    const bool removed = m.walk->delete_last(m.pipeline->clock().monotonic_ns());
    if (m.kind != WalkKind::Manual && removed && m.segments > 0 && !m.open_start) --m.segments;
    m.open_start.reset();
    m.manual_point.reset();
    return removed ? Result{true, m.kind == WalkKind::Manual ? tr("The last point was removed.") : tr("The last segment was removed.")}
                   : Result{false, tr("Nothing to remove.")};
}

Result LiveSurvey::stop() {
    Impl& m = *impl_;
    if (!m.walk) return Result{false, tr("No walk test is running.")};
    const std::size_t n = m.walk->stored();
    m.stop_walk();
    if (m.recording_by_walk) (void)m.end_recording();
    if (m.radio_by_walk) m.release_radio();
    m.end_practice();
    m.open_start.reset();
    m.manual_point.reset();
    return Result{true, tr("Walk test stopped: ") + std::to_string(n) + (n == 1 ? " point" : " points") + " stored."};
}

WalkView LiveSurvey::view() const {
    const Impl& m = *impl_;
    WalkView v;
    v.kind = running() ? m.kind : form_.kind;
    v.running = running();
    v.speed_mps = m.speed;
    if (!v.running) return v;
    v.floor_id = m.walk->floor_id();
    v.points = m.walk->stored();
    v.session = m.walk->session_id();
    v.radio = m.radio_label;
    v.open_start = m.open_start;
    v.last_click = m.last_click;
    v.segments = m.segments;
    v.manual_point = m.manual_point;
    if (m.practice)
        v.calibration_note = tr("Levels: simulated practice calibration (dBm), for this mouse survey only. Your calibration table is not changed.");
    switch (m.kind) {
    case WalkKind::PointAndClick:
        v.hint = m.open_start ? tr("Walk to the end of the segment and click there.") : tr("Click the start point, then walk.");
        break;
    case WalkKind::Continuous:
        v.hint = m.open_start ? tr("Click where you are at each turn; every click ends a segment and starts the next.")
                              : tr("Click the start point, then walk.");
        break;
    case WalkKind::Manual: v.hint = tr("Stand still at a point and click it: it is measured for the set time."); break;
    case WalkKind::MouseSurvey:
        v.hint = tr("Move the mouse slowly over the plan to measure; click to interpolate. Esc stops.");
        v.too_fast = m.speed > m.form.max_speed_mps;
        break;
    }
    return v;
}

// -- monitoring ----------------------------------------------------------------------------

Result LiveSurvey::start_radio() {
    Impl& m = *impl_;
    if (m.running_radio()) return Result{false, tr("The radio is measuring already.")};
    if (!searched_) return Result{false, tr("Find the radios first.")};
    const RadioChoice* r = chosen();
    if (r == nullptr) return Result{false, radio_note_.empty() ? std::string(tr("Choose a radio first.")) : radio_note_};
    try {
        update();
        if (std::string e = m.open_radio(*r, m.period_ns != 0 ? m.period_ns : kWalkPeriodNs); !e.empty())
            return Result{false, e};
    } catch (const std::exception& e) {
        m.release_radio();
        return Result{false, std::string(tr("The radio could not start: ")) + e.what()};
    }
    std::string note;
    if (recording_form_.ring)
        if (const Result rr = apply_ring(); !rr.ok) note = " " + rr.message;
    m.monitor->note(EventKind::Radio, r->label + tr(" measuring (monitoring)."));
    return Result{true, r->label + tr(" is measuring.") + note};
}

Result LiveSurvey::stop_radio() {
    Impl& m = *impl_;
    if (running()) return Result{false, tr("Stop the walk test first.")};
    if (m.recorder) return Result{false, tr("Stop the recording first.")};
    if (!m.running_radio()) return Result{false, tr("The radio is not measuring.")};
    const std::string label = m.device_label;
    m.release_radio();
    m.monitor->note(EventKind::Radio, label + " stopped.");
    return Result{true, label + " stopped."};
}

bool LiveSurvey::radio_running() const { return impl_->running_radio(); }

RadioState LiveSurvey::radio_state() const {
    const Impl& m = *impl_;
    RadioState s;
    const RadioChoice* c = chosen();
    s.chosen = c != nullptr;
    s.running = m.running_radio();
    s.label = s.running ? m.device_label : c ? c->label : std::string();
    s.cycles = cycles();
    s.lost = s.running && (m.monitor->last_flags() & measure::sample_flag::kDeviceLost) != 0;
    if (!searched_) s.text = tr("Radio: not searched");
    else if (!s.chosen && !s.running) s.text = tr("No radio");
    else if (s.lost) s.text = s.label + tr(": unplugged, waiting");
    else if (s.running) s.text = s.label + (running() ? tr(": walk test") : tr(": measuring"));
    else s.text = s.label + tr(": idle");
    return s;
}

void LiveSurvey::set_monitor(std::shared_ptr<SurveyMonitor> monitor) {
    Impl& m = *impl_;
    if (!monitor || monitor == m.monitor) return;
    if (m.pipeline) {
        m.pipeline->remove_observer(m.monitor);
        m.pipeline->add_observer(monitor);
    }
    m.monitor = std::move(monitor);
    impl_->building_dirty->store(true);
}

const std::shared_ptr<SurveyMonitor>& LiveSurvey::monitor() const { return impl_->monitor; }

std::shared_ptr<measure::TechnologyCatalog> LiveSurvey::catalog() {
    impl_->ensure_loader();
    return impl_->catalog;
}

// -- recording ----------------------------------------------------------------------------

fs::path LiveSurvey::recordings_folder() const { return impl_->recordings_folder(); }

std::string LiveSurvey::record_reason() const {
    const Impl& m = *impl_;
    if (m.recorder) return tr("A recording is running.");
    if (!m.running_radio()) return tr("Start the radio (or a walk test) first: a recording holds its measurements.");
    const RecordingForm& f = recording_form_;
    if (f.split_minutes != 0.0 && !(f.split_minutes >= 1.0 && f.split_minutes <= 60.0))
        return tr("Split by time must be off (0) or 1 to 60 minutes.");
    if (!f.name.empty() && !recording::valid_session_name(f.name))
        return tr("The name cannot be a file name (no / or \\, at most 120 characters).");
    return {};
}

Result LiveSurvey::record(bool quick) {
    Impl& m = *impl_;
    if (const std::string why = record_reason(); !why.empty()) return Result{false, why};
    recording::RecorderConfig c;
    c.folder = m.recordings_folder();
    c.compression = recording_form_.compress ? recording::Compression::Zstd : recording::Compression::None;
    c.split_minutes = recording_form_.split_minutes;
    c.info = m.header_info(recording_form_.notes);
    std::string error;
    std::shared_ptr<recording::Recorder> r;
    try {
        r = recording::Recorder::create(m.pipeline, c);
    } catch (const std::exception& e) {
        return Result{false, std::string(tr("The recording could not start: ")) + e.what()};
    }
    if (m.shown) {
        const building::Building b = m.project->building();
        const building::Floor* f = b.find_floor(*m.shown);
        r->set_floor(*m.shown, f ? f->name : std::string());
    }
    bool ok = false;
    if (quick || recording_form_.name.empty()) {
        ok = r->quick_record(error);
    } else {
        // A name in use gets _2, _3 ... (a walk's recording, REC-03); a typed one is refused.
        ok = r->start(recording_form_.name, error);
        for (int n = 2; !ok && m.unique_name && n < 100 && error.find("exists") != std::string::npos; ++n)
            ok = r->start(recording_form_.name + "_" + std::to_string(n), error);
    }
    if (!ok) {
        r->detach();
        return Result{false, tr("The recording could not start: ") + error};
    }
    m.recorder = r;
    const recording::RecorderStatus st = r->status();
    m.monitor->note(EventKind::Recording, tr("Recording ") + st.name + " started.");
    return Result{true, tr("Recording ") + st.name + " into " + c.folder.filename().string() + "/."};
}

Result LiveSurvey::stop_recording() {
    Impl& m = *impl_;
    if (!m.recorder) return Result{false, tr("No recording is running.")};
    if (running() && m.recording_by_walk) return Result{false, tr("The walk test records into this file: stop the walk test.")};
    const std::string name = m.recorder->status().name;
    std::string problems;
    const std::size_t n = m.end_recording(&problems);
    if (!problems.empty()) return Result{false, tr("Recording ") + name + tr(" stopped, but not all files are listed: ") + problems};
    return Result{true, tr("Recording ") + name + " stopped: " + std::to_string(n) + (n == 1 ? " file" : " files") +
                            tr(" listed in the project.")};
}

Result LiveSurvey::marker(const std::string& label, recording::MarkerAction action) {
    Impl& m = *impl_;
    if (!m.recorder || m.recorder->status().state != recording::RecorderState::Recording)
        return Result{false, tr("Markers are set while recording.")};
    const std::uint32_t n = m.recorder->marker(label, action);
    if (n == 0) return Result{false, tr("The marker could not be set.")};
    const std::string text = tr("Marker ") + std::to_string(n) + (label.empty() ? "" : ": " + label) +
                             (action == recording::MarkerAction::NewFile        ? tr(" (new file)")
                              : action == recording::MarkerAction::NewFilePause ? tr(" (new file, paused)")
                                                                                : "");
    m.monitor->note(EventKind::Marker, text + ".");
    return Result{true, text + "."};
}

Result LiveSurvey::resume_recording() {
    Impl& m = *impl_;
    if (!m.recorder) return Result{false, tr("No recording is running.")};
    std::string error;
    if (!m.recorder->resume(error)) return Result{false, tr("The recording could not go on: ") + error};
    m.monitor->note(EventKind::Recording, tr("Recording resumed in a new file."));
    return Result{true, tr("Recording resumed in a new file.")};
}

recording::RecorderStatus LiveSurvey::recording_status() const {
    return impl_->recorder ? impl_->recorder->status() : recording::RecorderStatus{};
}

bool LiveSurvey::recording() const { return impl_->recorder != nullptr; }

bool LiveSurvey::exit_guard() const { return impl_->recorder && impl_->recorder->exit_guard(); }

std::vector<fs::path> LiveSurvey::ring_files() const {
    return impl_->ring ? impl_->ring->status().files : impl_->ring_kept;
}

Result LiveSurvey::apply_ring() {
    Impl& m = *impl_;
    const RecordingForm& f = recording_form_;
    if (!f.ring) {
        if (!m.ring) return {};
        m.stop_ring();
        return Result{true, tr("The ring buffer is off.")};
    }
    if (m.paths.ring_folder.empty()) return Result{false, tr("There is no folder for the ring buffer.")};
    if (!(f.ring_minutes >= 2.0 && f.ring_minutes <= 60.0)) return Result{false, tr("The ring buffer keeps 2 to 60 minutes.")};
    if (!m.running_radio()) return Result{true, tr("The ring buffer starts with the radio.")};
    if (m.ring) return {};
    // The ring's folder is temporary: its old files go (a ring is saved with save_ring).
    std::error_code ec;
    fs::create_directories(m.paths.ring_folder, ec);
    for (const auto& e : fs::directory_iterator(m.paths.ring_folder, ec)) {
        const std::string n = e.path().filename().string();
        if (n.rfind("ring", 0) == 0) fs::remove(e.path(), ec);
    }
    recording::RecorderConfig c;
    c.folder = m.paths.ring_folder;
    if (f.ring_messages_per_file > 0) c.split_messages = f.ring_messages_per_file;
    else c.split_minutes = 1.0;
    c.ring_files = static_cast<std::uint32_t>(std::max(2.0, std::ceil(f.ring_minutes)));
    c.info = m.header_info(tr("ring buffer"));
    try {
        m.ring = recording::Recorder::create(m.pipeline, c);
    } catch (const std::exception& e) {
        return Result{false, std::string(tr("The ring buffer could not start: ")) + e.what()};
    }
    std::string error;
    if (!m.ring->start("ring", error)) {
        m.ring->detach();
        m.ring.reset();
        return Result{false, tr("The ring buffer could not start: ") + error};
    }
    m.ring_kept.clear();
    return Result{true, tr("The ring buffer keeps the last ") + std::to_string(c.ring_files) + " minutes."};
}

Result LiveSurvey::save_ring(const std::string& name) {
    Impl& m = *impl_;
    std::vector<fs::path> files = ring_files();
    if (files.empty()) return Result{false, tr("The ring buffer is empty.")};
    if (!recording::valid_session_name(name)) return Result{false, tr("The name cannot be a file name.")};
    if (m.ring) {
        std::string error;
        (void)m.ring->flush(error);
    }
    recording::CutRequest q;
    q.files = files;
    q.from_ns = 0;
    q.to_ns = std::numeric_limits<std::int64_t>::max();
    q.folder = m.recordings_folder();
    q.name = name;
    q.catalog = m.catalog;
    q.notes = tr("saved from the ring buffer");
    const recording::CutResult c = recording::cut_recording(q);
    if (!c.ok) return Result{false, tr("The ring buffer could not be saved: ") + c.error};
    const std::string problems = m.list_files(c.files);
    m.monitor->note(EventKind::Recording, tr("The ring buffer was saved as ") + name + ".");
    if (!problems.empty()) return Result{false, tr("Saved as ") + name + tr(", but not listed: ") + problems};
    return Result{true, tr("The last ") + std::to_string((c.to_ns - c.from_ns) / 1'000'000'000) + tr(" s were saved as ") + name + "."};
}

void LiveSurvey::set_audio(std::shared_ptr<AudioIo> audio) { impl_->audio = std::move(audio); }

std::string LiveSurvey::voice_reason() const {
    const Impl& m = *impl_;
    if (!m.recorder || m.recorder->status().state != recording::RecorderState::Recording)
        return tr("Voice notes go into a running recording: start one first.");
    if (!m.audio) return tr("There is no sound device.");
    return m.audio->capture_problem();
}

Result LiveSurvey::voice_start() {
    Impl& m = *impl_;
    if (m.voice) return Result{false, tr("A voice note is being recorded.")};
    if (const std::string why = voice_reason(); !why.empty()) return Result{false, why};
    std::string error;
    if (!m.audio->start_capture(16000, error)) return Result{false, tr("The microphone did not start: ") + error};
    m.voice = true;
    m.voice_t0 = m.pipeline->clock().monotonic_ns();
    return Result{true, tr("Speak now; stop to store the note.")};
}

Result LiveSurvey::voice_stop(const std::string& label) {
    Impl& m = *impl_;
    if (!m.voice) return Result{false, tr("No voice note is being recorded.")};
    m.voice = false;
    const recording::Audio a = m.audio->stop_capture();
    if (a.samples.empty()) return Result{false, tr("Nothing was recorded.")};
    if (!m.recorder) return Result{false, tr("The recording has stopped: the note was not stored.")};
    std::string error;
    const auto v = m.recorder->add_voice(a, m.voice_t0, label, error);
    if (!v) return Result{false, tr("The voice note was not stored: ") + error};
    char secs[32];
    std::snprintf(secs, sizeof secs, "%.1f s", v->seconds);
    const std::string text = tr("Voice note ") + std::to_string(v->number) + " (" + secs + ")" + (label.empty() ? "" : ": " + label);
    m.monitor->note(EventKind::VoiceNote, text + ".", m.voice_t0);
    return Result{true, text + " stored."};
}

bool LiveSurvey::voice_recording() const { return impl_->voice; }

double LiveSurvey::voice_seconds() const {
    return impl_->voice && impl_->audio ? impl_->audio->captured_seconds() : 0.0;
}

std::uint64_t LiveSurvey::cycles() const {
    return impl_->pipeline ? impl_->pipeline->stats().cycles : 0;
}

bool LiveSurvey::wait_context_idle(int timeout_ms) const {
    const RadioChoice* r = chosen();
    if (!impl_->host || r == nullptr) return true;
    return impl_->host->wait_context_idle(r->plugin_id, std::chrono::milliseconds(timeout_ms));
}

}  // namespace yantra::app
