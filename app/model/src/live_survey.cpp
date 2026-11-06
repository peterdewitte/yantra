// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The walk-test view's model, see live_survey.h.
#include "yantra/app/live_survey.h"

#include <chrono>
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
#include "yantra/core/survey/walk_test.h"

namespace yantra::app {

namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kWalkPeriodNs = 1'000'000'000;   // RF-01
constexpr std::int64_t kMousePeriodNs = 200'000'000;    // DEMO-14: a dense trail

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

    ~Impl() {
        stop_walk();
        close_radio();
        if (token != 0) project->unsubscribe(token);
    }

    void close_radio() {
        if (device) {
            (void)device->stop();
            device->close();
        }
        runner.reset();
        device.reset();
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
    try {
        m.ensure_host(form_.kind == WalkKind::MouseSurvey ? kMousePeriodNs : kWalkPeriodNs);
        show_floor(floor_id);
        update();
        std::string error;
        m.device = m.host->open(radio.plugin_id, radio.device_key, error);
        if (!m.device) return Result{false, "The radio could not be opened: " + error};
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
        m.walk = std::make_shared<survey::WalkTest>(o);
        m.pipeline->add_observer(m.walk);
        m.runner = std::make_unique<measure::CycleRunner>(*m.pipeline);
        radio::Plan plan;
        plan.type = YR_PLAN_SERVING;
        if (const yr_status st = m.device->start(plan); st != YR_OK) {
            const std::string why = m.device->last_error();
            m.stop_walk();
            m.close_radio();
            return Result{false, "The radio did not start: " + (why.empty() ? std::string("error ") + std::to_string(st) : why)};
        }
        m.walk->start(m.pipeline->clock().monotonic_ns());
    } catch (const std::exception& e) {
        m.stop_walk();
        m.close_radio();
        return Result{false, std::string("The walk test could not start: ") + e.what()};
    }
    m.radio_label = radio.label;
    m.last_click.reset();
    m.open_start.reset();
    m.manual_point.reset();
    m.segments = 0;
    return Result{true, std::string(walk_kind_label(form_.kind)) + " started with " + radio.label + "."};
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
    m.close_radio();
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

std::uint64_t LiveSurvey::cycles() const {
    return impl_->pipeline ? impl_->pipeline->stats().cycles : 0;
}

bool LiveSurvey::wait_context_idle(int timeout_ms) const {
    const RadioChoice* r = chosen();
    if (!impl_->host || r == nullptr) return true;
    return impl_->host->wait_context_idle(r->plugin_id, std::chrono::milliseconds(timeout_ms));
}

}  // namespace yantra::app
