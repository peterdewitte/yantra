// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The walk-test view's model, see live_survey.h.
#include "yantra/app/live_survey.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <cmath>
#include <ctime>
#include <exception>
#include <mutex>

#include <yantra/radio.h>

#include "yantra/core/calibration/corrector.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/context/building_context.h"
#include "yantra/core/measure/catalog.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/plugin/folders.h"
#include "yantra/core/plugin/loader.h"
#include "yantra/core/plugin/state_store.h"
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
    return std::string(k == WalkKind::MouseSurvey ? "Mouse survey " : k == WalkKind::Manual ? "Manual points " : "Walk ") + buf;
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
    case WalkKind::PointAndClick: return "Point and click";
    case WalkKind::Continuous: return "Continuous";
    case WalkKind::Manual: return "Manual points";
    case WalkKind::MouseSurvey: return "Mouse survey";
    }
    return "";
}

struct LiveSurvey::Impl {
    std::shared_ptr<project::Project> project;
    RadioPaths paths;
    int token = 0;
    std::shared_ptr<std::atomic<bool>> building_dirty = std::make_shared<std::atomic<bool>>(true);

    // Technology plugins (schema mapping, C1) and the plugin settings.
    std::shared_ptr<plugin::MemoryStateStore> state = std::make_shared<plugin::MemoryStateStore>();
    std::unique_ptr<plugin::PluginLoader> loader;
    std::shared_ptr<measure::TechnologyCatalog> catalog = std::make_shared<measure::TechnologyCatalog>();
    std::shared_ptr<calibration::Table> table;

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
        }
        runner.reset();
        device.reset();
        device_label.clear();
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
        monitor->note(EventKind::Recording, "Recording " + st.name + " stopped (" + std::to_string(st.files.size()) +
                                                (st.files.size() == 1 ? " file)." : " files)."));
        return st.files.size();
    }

    void stop_ring() {
        if (!ring) return;
        ring_kept = ring->status().files;
        ring->detach();
        ring.reset();
    }

    // Opens and starts the radio with this cycle period.
    std::string open_radio(const RadioChoice& radio, std::int64_t period) {
        ensure_host(period);
        std::string error;
        device = host->open(radio.plugin_id, radio.device_key, error);
        if (!device) return "The radio could not be opened: " + error;
        runner = std::make_unique<measure::CycleRunner>(*pipeline);
        radio::Plan plan;
        plan.type = YR_PLAN_SERVING;
        if (const yr_status st = device->start(plan); st != YR_OK) {
            const std::string why = device->last_error();
            close_radio();
            return "The radio did not start: " + (why.empty() ? std::string("error ") + std::to_string(st) : why);
        }
        device_label = radio.label;
        device_plugin = radio.plugin_id;
        device_key = radio.device_key;
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

    void ensure_loader() {
        if (loader) return;
        plugin::PluginLoaderConfig c;
        c.folders = paths.plugin_folders;
        c.state = state;
        loader = std::make_unique<plugin::PluginLoader>(std::move(c));
        loader->load_all();
        for (const plugin::PluginEntry& e : loader->list()) {
            if (e.status != plugin::PluginStatus::Enabled) continue;
            if (const plugin::PluginInfo* info = loader->info(e.id))
                (void)catalog->add(*info, loader->descriptor(e.id), loader->instance(e.id));
        }
        if (!paths.calibration_table.empty()) {
            auto t = std::make_shared<calibration::Table>();
            std::string error;
            if (calibration::Table::load_if_present(paths.calibration_table, *t, error) && error.empty()) table = t;
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

LiveSurvey::LiveSurvey(std::shared_ptr<project::Project> project, RadioPaths paths) : impl_(std::make_unique<Impl>()) {
    impl_->project = std::move(project);
    impl_->paths = std::move(paths);
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
    if (running()) return Result{false, "Stop the walk test first."};
    if (radio_running()) return Result{false, "Stop the radio first."};
    searched_ = true;
    radios_.clear();
    chosen_.reset();
    radio_note_.clear();
    try {
        impl_->ensure_host(impl_->period_ns != 0 ? impl_->period_ns : kWalkPeriodNs);
    } catch (const std::exception& e) {
        radio_note_ = std::string("The radio plugins could not be loaded: ") + e.what();
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
                c.reason = d.flags_reason.empty() ? std::string("The device is in use or not accessible.") : d.flags_reason;
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
        radio_note_ = "No radio found. Connect a radio, or enable a radio plugin (the simulator needs no hardware).";
        if (!problems.empty()) radio_note_ += " " + problems;
        return Result{false, radio_note_};
    }
    if (!problems.empty()) radio_note_ = problems;
    return Result{true, std::to_string(radios_.size()) + (radios_.size() == 1 ? " radio found." : " radios found.")};
}

Result LiveSurvey::choose_radio(std::size_t index) {
    if (running()) return Result{false, "Stop the walk test first."};
    if (radio_running()) return Result{false, "Stop the radio first."};
    if (index >= radios_.size()) return Result{false, "There is no such radio."};
    if (!radios_[index].usable) return Result{false, radios_[index].reason};
    chosen_ = index;
    return {};
}

const RadioChoice* LiveSurvey::chosen() const { return chosen_ ? &radios_[*chosen_] : nullptr; }

// -- context ----------------------------------------------------------------------------

void LiveSurvey::show_floor(const std::optional<std::string>& floor_id) {
    if (floor_id == impl_->shown) return;
    update();
    impl_->shown = floor_id;
    impl_->context.show_floor(floor_id);
    impl_->cursor_m.reset();
}

void LiveSurvey::update() {
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
    if (running()) return "A walk test is running: stop it first.";
    const building::Building b = impl_->project->building();
    const building::Floor* f = b.find_floor(floor_id);
    if (f == nullptr) return "Choose a floor first.";
    if (!searched_) return "Find the radios first.";
    const RadioChoice* r = chosen();
    if (r == nullptr) return radio_note_.empty() ? std::string("Choose a radio first.") : radio_note_;
    if (form_.kind == WalkKind::MouseSurvey) {
        if (!(r->simulated && r->uses_context))
            return "The mouse survey needs the simulator as the radio: it calculates the levels where the mouse is.";
        if (!b.has_scale(*f)) return "The mouse survey needs a floor with a scale (Plan and scale...).";
    }
    if (impl_->recorder && impl_->period_ns != (form_.kind == WalkKind::MouseSurvey ? kMousePeriodNs : kWalkPeriodNs))
        return "Stop the recording first: the mouse survey measures every 0.2 s, walks every second.";
    if (form_.kind == WalkKind::Manual && !(form_.manual_seconds > 0.0 && form_.manual_seconds <= 600.0))
        return "The time per point must be between 0 and 600 s.";
    if (form_.kind == WalkKind::MouseSurvey &&
        !(form_.spacing_m >= 0.0 && form_.spacing_m <= 10.0 && form_.max_speed_mps > 0.0 && form_.max_speed_mps <= 50.0))
        return "The point spacing must be 0 to 10 m and the speed limit above 0 and at most 50 m/s.";
    return {};
}

Result LiveSurvey::start(const std::string& floor_id) {
    if (const std::string why = start_reason(floor_id); !why.empty()) return Result{false, why};
    if (!chosen_ || *chosen_ >= radios_.size()) return Result{false, "Choose a radio first."};
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
            else note = " No recording file: " + r.message;
        }
        o.recorder = m.recorder;
        m.walk = std::make_shared<survey::WalkTest>(o);
        m.pipeline->add_observer(m.walk);
        m.walk->start(m.pipeline->clock().monotonic_ns());
    } catch (const std::exception& e) {
        m.stop_walk();
        if (m.recording_by_walk) (void)m.end_recording();
        if (m.radio_by_walk) m.close_radio();
        return Result{false, std::string("The walk test could not start: ") + e.what()};
    }
    m.radio_label = radio.label;
    m.last_click.reset();
    m.open_start.reset();
    m.manual_point.reset();
    m.segments = 0;
    return Result{true, std::string(walk_kind_label(form_.kind)) + " started with " + radio.label + "." + note};
}

Result LiveSurvey::click(Point2 plan) {
    Impl& m = *impl_;
    if (!running()) return Result{false, "Start a walk test first."};
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
            return Result{true, closes ? "Segment " + std::to_string(m.segments) + " ended." : std::string("Walk on; click where you are next.")};
        }
        case WalkKind::Manual:
            m.walk->point(t, plan, m.form.manual_seconds);
            m.manual_point = plan;
            m.last_click = plan;
            return Result{true, "Measuring here for " + std::to_string(static_cast<int>(std::lround(m.form.manual_seconds))) + " s..."};
        case WalkKind::MouseSurvey: break;
        }
    } catch (const std::exception& e) {
        return Result{false, e.what()};
    }
    return Result{false, "In the mouse survey a click interpolates."};
}

Result LiveSurvey::pause() {
    Impl& m = *impl_;
    if (!running() || (m.kind != WalkKind::PointAndClick && m.kind != WalkKind::Continuous))
        return Result{false, "Pause is for a walk (point and click or continuous)."};
    m.walk->pause(m.pipeline->clock().monotonic_ns());
    m.open_start.reset();
    return Result{true, "Paused: the open segment is dropped; click to start again."};
}

Result LiveSurvey::delete_last() {
    Impl& m = *impl_;
    if (!running() || m.kind == WalkKind::MouseSurvey) return Result{false, "Delete last is for a walk or manual points."};
    const bool removed = m.walk->delete_last(m.pipeline->clock().monotonic_ns());
    if (m.kind != WalkKind::Manual && removed && m.segments > 0 && !m.open_start) --m.segments;
    m.open_start.reset();
    m.manual_point.reset();
    return removed ? Result{true, m.kind == WalkKind::Manual ? "The last point was removed." : "The last segment was removed."}
                   : Result{false, "Nothing to remove."};
}

Result LiveSurvey::stop() {
    Impl& m = *impl_;
    if (!m.walk) return Result{false, "No walk test is running."};
    const std::size_t n = m.walk->stored();
    m.stop_walk();
    if (m.recording_by_walk) (void)m.end_recording();
    if (m.radio_by_walk) m.close_radio();
    m.open_start.reset();
    m.manual_point.reset();
    return Result{true, "Walk test stopped: " + std::to_string(n) + (n == 1 ? " point" : " points") + " stored."};
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
    switch (m.kind) {
    case WalkKind::PointAndClick:
        v.hint = m.open_start ? "Walk to the end of the segment and click there." : "Click the start point, then walk.";
        break;
    case WalkKind::Continuous:
        v.hint = m.open_start ? "Click where you are at each turn; every click ends a segment and starts the next."
                              : "Click the start point, then walk.";
        break;
    case WalkKind::Manual: v.hint = "Stand still at a point and click it: it is measured for the set time."; break;
    case WalkKind::MouseSurvey:
        v.hint = "Move the mouse slowly over the plan to measure; click to interpolate. Esc stops.";
        v.too_fast = m.speed > m.form.max_speed_mps;
        break;
    }
    return v;
}

// -- monitoring ----------------------------------------------------------------------------

Result LiveSurvey::start_radio() {
    Impl& m = *impl_;
    if (m.running_radio()) return Result{false, "The radio is measuring already."};
    if (!searched_) return Result{false, "Find the radios first."};
    const RadioChoice* r = chosen();
    if (r == nullptr) return Result{false, radio_note_.empty() ? std::string("Choose a radio first.") : radio_note_};
    try {
        update();
        if (std::string e = m.open_radio(*r, m.period_ns != 0 ? m.period_ns : kWalkPeriodNs); !e.empty())
            return Result{false, e};
    } catch (const std::exception& e) {
        m.close_radio();
        return Result{false, std::string("The radio could not start: ") + e.what()};
    }
    std::string note;
    if (recording_form_.ring)
        if (const Result rr = apply_ring(); !rr.ok) note = " " + rr.message;
    m.monitor->note(EventKind::Radio, r->label + " measuring (monitoring).");
    return Result{true, r->label + " is measuring." + note};
}

Result LiveSurvey::stop_radio() {
    Impl& m = *impl_;
    if (running()) return Result{false, "Stop the walk test first."};
    if (m.recorder) return Result{false, "Stop the recording first."};
    if (!m.running_radio()) return Result{false, "The radio is not measuring."};
    const std::string label = m.device_label;
    m.close_radio();
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
    if (!searched_) s.text = "Radio: not searched";
    else if (!s.chosen && !s.running) s.text = "No radio";
    else if (s.lost) s.text = s.label + ": unplugged, waiting";
    else if (s.running) s.text = s.label + (running() ? ": walk test" : ": measuring");
    else s.text = s.label + ": idle";
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
    if (m.recorder) return "A recording is running.";
    if (!m.running_radio()) return "Start the radio (or a walk test) first: a recording holds its measurements.";
    const RecordingForm& f = recording_form_;
    if (f.split_minutes != 0.0 && !(f.split_minutes >= 1.0 && f.split_minutes <= 60.0))
        return "Split by time must be off (0) or 1 to 60 minutes.";
    if (!f.name.empty() && !recording::valid_session_name(f.name))
        return "The name cannot be a file name (no / or \\, at most 120 characters).";
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
        return Result{false, std::string("The recording could not start: ") + e.what()};
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
        return Result{false, "The recording could not start: " + error};
    }
    m.recorder = r;
    const recording::RecorderStatus st = r->status();
    m.monitor->note(EventKind::Recording, "Recording " + st.name + " started.");
    return Result{true, "Recording " + st.name + " into " + c.folder.filename().string() + "/."};
}

Result LiveSurvey::stop_recording() {
    Impl& m = *impl_;
    if (!m.recorder) return Result{false, "No recording is running."};
    if (running() && m.recording_by_walk) return Result{false, "The walk test records into this file: stop the walk test."};
    const std::string name = m.recorder->status().name;
    std::string problems;
    const std::size_t n = m.end_recording(&problems);
    if (!problems.empty()) return Result{false, "Recording " + name + " stopped, but not all files are listed: " + problems};
    return Result{true, "Recording " + name + " stopped: " + std::to_string(n) + (n == 1 ? " file" : " files") +
                            " listed in the project."};
}

Result LiveSurvey::marker(const std::string& label, recording::MarkerAction action) {
    Impl& m = *impl_;
    if (!m.recorder || m.recorder->status().state != recording::RecorderState::Recording)
        return Result{false, "Markers are set while recording."};
    const std::uint32_t n = m.recorder->marker(label, action);
    if (n == 0) return Result{false, "The marker could not be set."};
    const std::string text = "Marker " + std::to_string(n) + (label.empty() ? "" : ": " + label) +
                             (action == recording::MarkerAction::NewFile        ? " (new file)"
                              : action == recording::MarkerAction::NewFilePause ? " (new file, paused)"
                                                                                : "");
    m.monitor->note(EventKind::Marker, text + ".");
    return Result{true, text + "."};
}

Result LiveSurvey::resume_recording() {
    Impl& m = *impl_;
    if (!m.recorder) return Result{false, "No recording is running."};
    std::string error;
    if (!m.recorder->resume(error)) return Result{false, "The recording could not go on: " + error};
    m.monitor->note(EventKind::Recording, "Recording resumed in a new file.");
    return Result{true, "Recording resumed in a new file."};
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
        return Result{true, "The ring buffer is off."};
    }
    if (m.paths.ring_folder.empty()) return Result{false, "There is no folder for the ring buffer."};
    if (!(f.ring_minutes >= 2.0 && f.ring_minutes <= 60.0)) return Result{false, "The ring buffer keeps 2 to 60 minutes."};
    if (!m.running_radio()) return Result{true, "The ring buffer starts with the radio."};
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
    c.info = m.header_info("ring buffer");
    try {
        m.ring = recording::Recorder::create(m.pipeline, c);
    } catch (const std::exception& e) {
        return Result{false, std::string("The ring buffer could not start: ") + e.what()};
    }
    std::string error;
    if (!m.ring->start("ring", error)) {
        m.ring->detach();
        m.ring.reset();
        return Result{false, "The ring buffer could not start: " + error};
    }
    m.ring_kept.clear();
    return Result{true, "The ring buffer keeps the last " + std::to_string(c.ring_files) + " minutes."};
}

Result LiveSurvey::save_ring(const std::string& name) {
    Impl& m = *impl_;
    std::vector<fs::path> files = ring_files();
    if (files.empty()) return Result{false, "The ring buffer is empty."};
    if (!recording::valid_session_name(name)) return Result{false, "The name cannot be a file name."};
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
    q.notes = "saved from the ring buffer";
    const recording::CutResult c = recording::cut_recording(q);
    if (!c.ok) return Result{false, "The ring buffer could not be saved: " + c.error};
    const std::string problems = m.list_files(c.files);
    m.monitor->note(EventKind::Recording, "The ring buffer was saved as " + name + ".");
    if (!problems.empty()) return Result{false, "Saved as " + name + ", but not listed: " + problems};
    return Result{true, "The last " + std::to_string((c.to_ns - c.from_ns) / 1'000'000'000) + " s were saved as " + name + "."};
}

void LiveSurvey::set_audio(std::shared_ptr<AudioIo> audio) { impl_->audio = std::move(audio); }

std::string LiveSurvey::voice_reason() const {
    const Impl& m = *impl_;
    if (!m.recorder || m.recorder->status().state != recording::RecorderState::Recording)
        return "Voice notes go into a running recording: start one first.";
    if (!m.audio) return "There is no sound device.";
    return m.audio->capture_problem();
}

Result LiveSurvey::voice_start() {
    Impl& m = *impl_;
    if (m.voice) return Result{false, "A voice note is being recorded."};
    if (const std::string why = voice_reason(); !why.empty()) return Result{false, why};
    std::string error;
    if (!m.audio->start_capture(16000, error)) return Result{false, "The microphone did not start: " + error};
    m.voice = true;
    m.voice_t0 = m.pipeline->clock().monotonic_ns();
    return Result{true, "Speak now; stop to store the note."};
}

Result LiveSurvey::voice_stop(const std::string& label) {
    Impl& m = *impl_;
    if (!m.voice) return Result{false, "No voice note is being recorded."};
    m.voice = false;
    const recording::Audio a = m.audio->stop_capture();
    if (a.samples.empty()) return Result{false, "Nothing was recorded."};
    if (!m.recorder) return Result{false, "The recording has stopped: the note was not stored."};
    std::string error;
    const auto v = m.recorder->add_voice(a, m.voice_t0, label, error);
    if (!v) return Result{false, "The voice note was not stored: " + error};
    char secs[32];
    std::snprintf(secs, sizeof secs, "%.1f s", v->seconds);
    const std::string text = "Voice note " + std::to_string(v->number) + " (" + secs + ")" + (label.empty() ? "" : ": " + label);
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
