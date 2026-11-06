// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The walk-test view's model (IND-20, GEO-02, GEO-05, PLG-27, DEMO-14),
// without drawing: the radios (radio plugins through the core radio host,
// technology plugins for the schema mapping, the measurement pipeline and
// its cycle thread), the plugins' context (core context::BuildingContext:
// the floor shown, the mouse and walk positions, the scene, kept current
// when walls or antennas change) and one walk test at a time
// (survey::WalkTest) storing measured points in the project as the
// surveyor clicks. No radio-specific code: a radio is a plugin and device
// of the radio host; the mouse survey needs a device the plugin marks
// simulated (YR_CAP_SIMULATED) that uses the context (YR_CAP_SCENE).
//
// Kinds:
// - point and click / continuous (GEO-02): click the start, walk, click
//   the end; samples spread along the segment; pause, delete last segment;
// - manual (GEO-05): a click measures at that point for N seconds;
// - mouse survey (DEMO-14): the mouse stands in for the surveyor; every
//   simulator record is a point where the simulator calculated it
//   (sim_position, RAD-19), at most one per spacing_m; a "move slower"
//   warning above max_speed_mps (plan scale); a click runs the
//   interpolation (the caller's InterpolationRunner); Esc stops.
// The points are a session of the floor in the project (IND-20); the
// recording file of a walk (REC-03) is step 9.5's.
//
// The pipeline's cycle is 1 s for walk tests (RF-01) and 0.2 s for the
// mouse survey, so a slowly moved mouse gives a dense trail; the radio
// host is rebuilt when the period changes (the devices keep their keys).
// Levels are corrected with the user's calibration table when it exists
// (RF-02), else they stay dBFS and are flagged uncalibrated.
//
// Threading: the UI thread calls everything; the cycle thread stores the
// points (the project's change notification marks the windows dirty).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/app/result.h"
#include "yantra/core/building/model.h"

namespace yantra::project {
class Project;
}

namespace yantra::app {

using building::Point2;

/// Where the plugins and the calibration table are (AppPaths).
struct RadioPaths {
    std::vector<std::filesystem::path> plugin_folders;  ///< technology plugins (plugin.h)
    std::vector<std::filesystem::path> radio_folders;   ///< radio plugins
    std::filesystem::path calibration_table;            ///< "" none (levels stay dBFS)
};

/// A radio the walk test can use: a device of a radio plugin.
struct RadioChoice {
    std::string plugin_id;
    std::string device_key;
    std::string label;  ///< "Simulator (sim:0)"
    bool simulated = false;   ///< simulated data (DEMO-07)
    bool uses_context = false;  ///< follows the plan, cursor and walk position (PLG-27)
    bool usable = true;
    std::string reason;  ///< why not usable (in use, no permission)
};

enum class WalkKind { PointAndClick, Continuous, Manual, MouseSurvey };
const char* walk_kind_key(WalkKind k) noexcept;  ///< "point_and_click", "continuous", "manual", "mouse_survey"
const char* walk_kind_label(WalkKind k) noexcept;

struct WalkForm {
    WalkKind kind = WalkKind::PointAndClick;
    std::string name;   ///< the session's name ("" a default from the date)
    std::string notes;
    double manual_seconds = 5.0;  ///< GEO-05: averaging per point, > 0
    double spacing_m = 0.25;      ///< DEMO-14: at most one point per this distance
    double max_speed_mps = 2.0;   ///< DEMO-14: "move slower" above this cursor speed
};

/// What the walk-test panel and the plan overlay show.
struct WalkView {
    bool running = false;
    WalkKind kind = WalkKind::PointAndClick;
    std::string floor_id;
    std::size_t points = 0;  ///< stored by this walk
    std::int64_t session = 0;
    std::string radio;       ///< the radio's label
    /// The hint on the plan ("Click the start point...", DEMO-14: "Move the
    /// mouse slowly over the plan to measure; click to interpolate").
    std::string hint;
    bool too_fast = false;   ///< DEMO-14 "move slower"
    double speed_mps = 0.0;  ///< the cursor's speed (mouse survey)
    std::optional<Point2> open_start;  ///< walk: the start of the open segment
    std::optional<Point2> last_click;
    std::size_t segments = 0;
    std::optional<Point2> manual_point;  ///< manual: the point being measured
};

class LiveSurvey {
public:
    LiveSurvey(std::shared_ptr<project::Project> project, RadioPaths paths);
    /// Stops a running walk (its points are kept) and every radio.
    ~LiveSurvey();
    LiveSurvey(const LiveSurvey&) = delete;
    LiveSurvey& operator=(const LiveSurvey&) = delete;

    // -- radios -----------------------------------------------------------------
    /// Loads the plugins (once) and lists every device of every enabled
    /// radio plugin (enumeration may take a second: on request only).
    Result find_radios();
    const std::vector<RadioChoice>& radios() const noexcept { return radios_; }
    bool radios_searched() const noexcept { return searched_; }
    /// Why no radio is listed, or what failed to load ("" none).
    const std::string& radio_note() const noexcept { return radio_note_; }
    Result choose_radio(std::size_t index);
    /// The chosen radio (nullptr none). A simulated context radio is chosen
    /// by default when there is one (the demo and the tour).
    const RadioChoice* chosen() const;

    // -- context (PLG-27) ---------------------------------------------------------
    /// The floor shown in the project window (nullopt: none).
    void show_floor(const std::optional<std::string>& floor_id);
    /// The mouse over the shown floor's plan (plan units), nullopt when it
    /// left the plan; `t_s` is the UI clock in seconds (the cursor speed).
    void cursor(std::optional<Point2> plan, double t_s);
    /// Call each frame: re-exports the scene after a building change.
    void update();

    // -- walk test ------------------------------------------------------------------
    WalkForm& form() { return form_; }
    /// Why a walk of the form's kind cannot start on the floor ("" when it can).
    std::string start_reason(const std::string& floor_id) const;
    Result start(const std::string& floor_id);
    /// A click on the plan: walk kinds a click, manual a point. The mouse
    /// survey's click (interpolate) is the caller's.
    Result click(Point2 plan);
    Result pause();
    /// Walk: the last segment; manual: the last point.
    Result delete_last();
    /// Ends the walk; its points stay in the project.
    Result stop();
    bool running() const;
    WalkView view() const;

    /// Tests: the last cycle time the walk test saw (pipeline clock), the
    /// pipeline's cycle count, and whether the plugin has taken the latest context.
    std::uint64_t cycles() const;
    bool wait_context_idle(int timeout_ms) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<RadioChoice> radios_;
    bool searched_ = false;
    std::string radio_note_;
    std::optional<std::size_t> chosen_;
    WalkForm form_;
};

}  // namespace yantra::app
