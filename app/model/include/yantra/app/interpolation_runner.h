// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interpolation layer's commands (IND-41..45, DEMO-14 "click to
// interpolate"), without drawing: run or re-run the ray-tracing
// interpolation of a floor in the background (survey::InterpolationJob,
// OA-03), its progress and stage through the job's listener, cancel, and
// the propagation settings dialog (project::PropagationSettings, stored in
// the project, IND-45). The result is stored in the project by the job
// (its change notification marks the project window dirty); the project
// window then shows it (ProjectWindow::show_interpolation).
//
// - run(floor): a new map of the floor's points with the points' metric
//   (the serving carrier's level, as the points layer), or, when a stored
//   map is given, the same request again (a stale map run again, IND-45).
// - One job at a time; a run while one runs is refused with the reason.
// - poll() on the UI thread each frame: when the job has ended it returns
//   the result message once (done, cancelled, failed with the reason).
//
// Threading: the commands on the UI thread; the listener runs on the
// job's worker and only copies the status under a lock.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "yantra/app/result.h"
#include "yantra/core/project/project.h"
#include "yantra/core/survey/interpolation.h"

namespace yantra::app {

/// What the panel shows of the job.
struct InterpolationProgress {
    bool running = false;
    std::string floor_id;
    double progress = 0.0;  ///< 0..1
    std::string stage;      ///< "Fit", "Map", ...
    std::string text;       ///< "Interpolating Ground floor: Fit (40 %)"
};

/// The propagation settings dialog (IND-40, IND-44): the project's
/// settings and the building's dB per floor.
struct PropagationForm {
    project::PropagationSettings settings;
    double floor_attenuation_db = 15.0;
};

class InterpolationRunner {
public:
    struct Options {
        unsigned threads = 0;  ///< worker threads of a run (0: all)
    };
    InterpolationRunner(std::shared_ptr<project::Project> project, Options options);
    explicit InterpolationRunner(std::shared_ptr<project::Project> project)
        : InterpolationRunner(std::move(project), Options{}) {}
    /// Cancels a running job and waits for it.
    ~InterpolationRunner();
    InterpolationRunner(const InterpolationRunner&) = delete;
    InterpolationRunner& operator=(const InterpolationRunner&) = delete;

    /// Why a floor cannot be interpolated now ("" when it can): no scale,
    /// no measured point, a job running.
    std::string run_reason(const std::string& floor_id) const;
    /// Starts a run (see above); `again` is a stored map whose request is used.
    Result run(const std::string& floor_id, const survey::Interpolation* again = nullptr);
    void cancel();
    bool running() const;
    InterpolationProgress progress() const;
    /// The message of a job that has ended since the last call (once), and
    /// the stored id of a finished map (0 otherwise).
    std::optional<Result> poll(std::int64_t* stored_id = nullptr);
    /// Blocks until the job has ended (tests; the UI never waits).
    void wait() const;
    /// Changes whenever the job's status changed (the UI redraws).
    std::uint64_t generation() const;

    // -- propagation settings ---------------------------------------------------
    /// Opens the form with the project's values.
    PropagationForm& open_settings();
    PropagationForm& settings_form() { return form_; }
    /// Validates and stores; the stored maps become stale (their settings changed).
    Result apply_settings();
    /// The defaults in the form (not stored until applied).
    void default_settings();

private:
    std::shared_ptr<project::Project> project_;
    Options options_;
    std::unique_ptr<survey::InterpolationJob> job_;
    std::string job_floor_;
    std::string job_floor_name_;
    bool reported_ = true;
    struct Shared {
        std::mutex m;
        util::JobStatus status;
        std::uint64_t generation = 0;
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    PropagationForm form_;
};

}  // namespace yantra::app
