// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interpolation layer's commands, see interpolation_runner.h.
#include "yantra/app/interpolation_runner.h"

#include <cmath>
#include <cstdio>
#include <exception>

#include "yantra/core/survey/points.h"

namespace yantra::app {

InterpolationRunner::InterpolationRunner(std::shared_ptr<project::Project> project, Options options)
    : project_(std::move(project)), options_(options) {}

InterpolationRunner::~InterpolationRunner() {
    if (job_) {
        job_->cancel();
        job_->wait();
    }
}

bool InterpolationRunner::running() const {
    return job_ && job_->status().state == util::JobState::Running;
}

std::string InterpolationRunner::run_reason(const std::string& floor_id) const {
    if (running()) return "An interpolation is running; wait for it or cancel it.";
    const building::Building b = project_->building();
    const building::Floor* f = b.find_floor(floor_id);
    if (f == nullptr) return "Choose a floor first.";
    if (!b.has_scale(*f)) return "The floor has no scale: set one in Plan and scale... first (IND-08).";
    if (project_->point_count(floor_id) == 0) return "The floor has no measured points yet: walk it first.";
    return {};
}

Result InterpolationRunner::run(const std::string& floor_id, const survey::Interpolation* again) {
    if (const std::string why = run_reason(floor_id); !why.empty()) return Result{false, why};
    survey::InterpolationRequest r;
    if (again != nullptr && again->request.floor_id == floor_id) {
        r = again->request;
        r.grid_m.reset();  // the project's current grid setting
    } else {
        r.floor_id = floor_id;
        r.metric = survey::Metric{};  // the serving carrier's level, as the points layer
    }
    const auto f = project_->floor(floor_id);
    job_.reset();
    {
        const std::lock_guard l(shared_->m);
        shared_->status = util::JobStatus{};
        ++shared_->generation;
    }
    std::weak_ptr<Shared> weak = shared_;
    survey::InterpolationJob::Options o;
    o.threads = options_.threads;
    try {
        job_ = survey::InterpolationJob::start(project_, r, o, [weak](const util::JobStatus& s) {
            if (const auto sh = weak.lock()) {
                const std::lock_guard l(sh->m);
                sh->status = s;
                ++sh->generation;
            }
        });
    } catch (const std::exception& e) {
        return Result{false, std::string("The interpolation could not start: ") + e.what()};
    }
    job_floor_ = floor_id;
    job_floor_name_ = f ? f->name : floor_id;
    reported_ = false;
    return Result{true, "Interpolating " + job_floor_name_ + " in the background..."};
}

void InterpolationRunner::cancel() {
    if (job_) job_->cancel();
}

InterpolationProgress InterpolationRunner::progress() const {
    InterpolationProgress p;
    if (!job_) return p;
    util::JobStatus s;
    {
        const std::lock_guard l(shared_->m);
        s = shared_->status;
    }
    // The job's own state is the truth (the listener may lag a little).
    const util::JobStatus now = job_->status();
    if (now.state != util::JobState::Running) s = now;
    p.running = s.state == util::JobState::Running;
    p.floor_id = job_floor_;
    p.progress = s.progress;
    p.stage = s.stage;
    char pct[16];
    std::snprintf(pct, sizeof pct, "%.0f %%", s.progress * 100.0);
    p.text = "Interpolating " + job_floor_name_ + (s.stage.empty() ? std::string() : ": " + s.stage) + " (" + pct + ")";
    return p;
}

std::optional<Result> InterpolationRunner::poll(std::int64_t* stored_id) {
    if (stored_id != nullptr) *stored_id = 0;
    if (!job_ || reported_) return std::nullopt;
    const util::JobStatus s = job_->status();
    if (s.state == util::JobState::Running) return std::nullopt;
    reported_ = true;
    switch (s.state) {
    case util::JobState::Done: {
        const auto r = job_->result();
        if (r && stored_id != nullptr) *stored_id = r->id;
        char buf[96] = "";
        if (r) std::snprintf(buf, sizeof buf, " (fit: RMS %.1f dB over %zu points)", r->quality.rms_db, r->quality.points);
        return Result{true, "Interpolated " + job_floor_name_ + buf + (r && r->estimated_sources ? ", estimated sources." : ".")};
    }
    case util::JobState::Cancelled: return Result{true, "The interpolation of " + job_floor_name_ + " was cancelled; nothing was stored."};
    case util::JobState::Failed: return Result{false, "The interpolation of " + job_floor_name_ + " failed: " + s.error};
    case util::JobState::Running: break;
    }
    return std::nullopt;
}

void InterpolationRunner::wait() const {
    if (job_) job_->wait();
}

std::uint64_t InterpolationRunner::generation() const {
    const std::lock_guard l(shared_->m);
    return shared_->generation;
}

PropagationForm& InterpolationRunner::open_settings() {
    form_.settings = project_->propagation();
    form_.floor_attenuation_db = project_->building().floor_attenuation_db;
    return form_;
}

void InterpolationRunner::default_settings() {
    form_.settings = project::PropagationSettings{};
    form_.floor_attenuation_db = building::Building{}.floor_attenuation_db;
}

Result InterpolationRunner::apply_settings() {
    const auto problems = form_.settings.validate();
    if (!problems.empty()) return Result{false, "Not stored: " + problems.front()};
    if (!std::isfinite(form_.floor_attenuation_db) || form_.floor_attenuation_db < 0.0 || form_.floor_attenuation_db > 100.0)
        return Result{false, "Not stored: the attenuation per floor must be between 0 and 100 dB."};
    try {
        project_->set_propagation(form_.settings);
        if (project_->building().floor_attenuation_db != form_.floor_attenuation_db)
            project_->set_floor_attenuation_db(form_.floor_attenuation_db);
    } catch (const std::exception& e) {
        return Result{false, std::string("Not stored: ") + e.what()};
    }
    return Result{true, "Propagation settings stored. Maps made with other settings are marked stale until run again."};
}

}  // namespace yantra::app
