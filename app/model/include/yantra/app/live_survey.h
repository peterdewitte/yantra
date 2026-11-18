// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The walk-test view's model (IND-20, GEO-02, GEO-05, PLG-27, DEMO-14),
// without drawing: the radios (radio plugins through the core radio host,
// technology plugins for the schema mapping, the measurement pipeline and
// its cycle thread), the plugins' context (core context::BuildingContext:
// the floor shown, the mouse and walk positions, and for a bundled example
// floor its fixed demo scene, DEMO-05; a user's own floor has no scene)
// and one walk test at a time
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
// The points are a session of the floor in the project (IND-20).
//
// Monitoring and recording (REC-01..03, REC-05, REC-06, REC-09, REC-11;
// step 9.5): the chosen radio can measure without a walk (start_radio:
// Monitoring, the survey panels fill); record() / quick_record() write a
// recording session (core recording::Recorder) into the project's
// `recordings/` folder, listed in the project when it stops. A walk test
// always has a recording file (REC-03): it records into the running
// session, or starts one named after the walk and stops it at the end.
// Markers (REC-05), voice notes through the app's AudioIo (REC-06) and a
// ring buffer of the last minutes while the radio runs (REC-09, in
// RadioPaths::ring_folder; save_ring() cuts it into a recording).
// Every cycle goes to the survey panels' model (monitor(), UI-01..04,
// UI-11, NFR-07); the cell database for its names is the project's.
//
// The pipeline's cycle is 1 s for walk tests (RF-01) and 0.2 s for the
// mouse survey, so a slowly moved mouse gives a dense trail; the radio
// host is rebuilt when the period changes (the devices keep their keys).
// Levels are corrected with the user's calibration table when it exists
// (RF-02), else they stay dBFS and are flagged uncalibrated. A radio that
// reports dBm (SELF_CALIBRATED, or LEVEL_DBM such as the simulator's
// default) needs no entry (HW-01b). The mouse survey applies no automatic
// calibration (DEMO-14): the simulator set to dBFS gives uncalibrated points
// until the user calibrates it with the wizard (practice calibration).
//
// Threading: the UI thread calls everything; the cycle thread stores the
// points (the project's change notification marks the windows dirty).
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "yantra/app/audio.h"
#include "yantra/app/result.h"
#include "yantra/app/survey_monitor.h"
#include "yantra/core/building/model.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/recording/writer.h"

namespace yantra::project {
class Project;
}
namespace yantra::radio {
class RadioHost;
class RadioDevice;
class SpectrumStore;
struct PlanRanges;
}
namespace yantra::plugin {
class PluginStateStore;
class PluginLoader;
}
namespace yantra::calibration {
class Table;
}

namespace yantra::app {

using building::Point2;

/// Where the plugins and the calibration table are (AppPaths).
struct RadioPaths {
    std::vector<std::filesystem::path> plugin_folders;  ///< technology plugins (plugin.h)
    std::vector<std::filesystem::path> radio_folders;   ///< radio plugins
    std::filesystem::path calibration_table;            ///< "" none (levels stay dBFS)
    /// The radio devices' stored settings (RAD-47) and the plugins' enabled
    /// state, a JSON file (docs/APP.md "Radio windows"); "" = kept in memory.
    std::filesystem::path plugin_state;
    /// The ring buffer's temporary recordings (REC-09; main(): the user's
    /// cache folder). "" = no ring buffer.
    std::filesystem::path ring_folder;
    /// The fixed demo scenes of the bundled example floors (DEMO-05,
    /// PLG-27 get_scene): <data folder>/demo/scenes, the first that has a
    /// floor's scene (AppState fills it from the data folders when empty).
    /// Empty: no floor has a scene.
    std::vector<std::filesystem::path> scene_folders;
};

/// The plugin state and the loaded technology, feature and exporter
/// plugins, shared with the app (AppState owns them for the plugin
/// manager, About and the plugins' own UI, PLG-06/09). Empty members: the
/// LiveSurvey makes its own (tests).
struct PluginServices {
    std::shared_ptr<plugin::PluginStateStore> state;
    std::shared_ptr<plugin::PluginLoader> loader;  ///< already loaded (load_all)
};

/// The recording settings (REC-01..03, REC-09).
struct RecordingForm {
    std::string name;            ///< Record: the session name ("" = quick-record name)
    std::string notes;           ///< operator notes in the header (REC-03)
    double split_minutes = 0.0;  ///< 0 = off, 1..60 (REC-02)
    bool compress = false;       ///< zstd (REC-03)
    bool ring = false;           ///< REC-09: loop the last minutes while the radio runs
    double ring_minutes = 10.0;  ///< 2..60: kept as files of one minute
    /// Tests: ring files split by this many messages instead of a minute (0 = by time).
    std::uint64_t ring_messages_per_file = 0;
};

/// The radio part of the status bar (UI-08).
struct RadioState {
    bool chosen = false;
    bool running = false;    ///< measuring (monitoring or a walk)
    bool lost = false;       ///< unplugged (NFR-04)
    std::string label;
    std::string text;        ///< "Simulator (sim:0): measuring", "No radio", ...
    std::string state;       ///< the state alone: "measuring", "idle", "No radio", ...
    std::uint64_t cycles = 0;
};

/// A radio the walk test can use: a device of a radio plugin.
struct RadioChoice {
    std::string plugin_id;
    std::string device_key;
    std::string label;  ///< "Simulator (sim:0)"
    bool simulated = false;   ///< simulated data (DEMO-07)
    bool uses_context = false;  ///< follows the plan, cursor and walk position (PLG-27)
    bool spectrum = false;      ///< delivers spectra (YR_CAP_SPECTRUM, radio API 1.2, UI-24)
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
    /// DEMO-14: the unit of the mouse survey's levels: dBm from the radio, or
    /// dBFS uncalibrated with what to do ("" calibrated, or not a mouse survey).
    std::string calibration_note;
};

class LiveSurvey {
public:
    LiveSurvey(std::shared_ptr<project::Project> project, RadioPaths paths, PluginServices services = {});
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
    /// PLG-16: what the last search heard from an enabled plugin that
    /// listed no device (its diagnostic, RAD-52; "" none or not searched).
    std::string search_note(const std::string& plugin_id) const;
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

    // -- monitoring (REC-01) -------------------------------------------------------
    /// The chosen radio measures without a walk (the survey panels fill).
    Result start_radio();
    /// Stops the radio (refused while a walk or a recording runs).
    Result stop_radio();
    bool radio_running() const;
    RadioState radio_state() const;
    /// The survey panels' model (shared with the replay; set by AppState).
    void set_monitor(std::shared_ptr<SurveyMonitor> monitor);
    const std::shared_ptr<SurveyMonitor>& monitor() const;
    /// The technology plugins (loaded on first use), for a replay.
    std::shared_ptr<measure::TechnologyCatalog> catalog();

    // -- recording (REC-01..03, REC-05, REC-06, REC-09, REC-11) ----------------------
    RecordingForm& recording_form() { return recording_form_; }
    /// The project's recordings folder: `<project folder>/recordings`.
    std::filesystem::path recordings_folder() const;
    /// Why a recording cannot start ("" when it can).
    std::string record_reason() const;
    /// Record (the form's name) or Quick-record (`quick` or an empty name).
    Result record(bool quick = false);
    /// Closes the session; its files are listed in the project.
    Result stop_recording();
    /// A numbered marker (REC-05); NewFile / NewFilePause split the file.
    Result marker(const std::string& label, recording::MarkerAction action = recording::MarkerAction::Mark);
    /// After "new file + pause".
    Result resume_recording();
    recording::RecorderStatus recording_status() const;
    bool recording() const;
    /// REC-11: a recording is open (Recording or Paused): the app asks before it exits.
    bool exit_guard() const;
    /// REC-09: the ring buffer's files now (empty when off).
    std::vector<std::filesystem::path> ring_files() const;
    /// Applies recording_form().ring now (also when the radio starts).
    Result apply_ring();
    /// Saves the ring buffer's minutes as recording `name` (a cut, REC-08).
    Result save_ring(const std::string& name);
    /// Voice notes (REC-06): the sound device; start, then stop stores it.
    void set_audio(std::shared_ptr<AudioIo> audio);
    std::string voice_reason() const;
    Result voice_start();
    Result voice_stop(const std::string& label = {});
    bool voice_recording() const;
    double voice_seconds() const;

    // -- connection and the radio windows (PLG-16, step 9.6) -------------------------
    /// Opens the chosen radio without measuring, for its windows (settings,
    /// debug, calibration, device info). A radio that measures is connected.
    Result connect();
    /// Closes the chosen radio (refused while it measures).
    Result disconnect();
    /// The chosen radio is open (connected or measuring).
    bool connected() const;
    /// The open device (nullptr none). Its methods wait for the plugin:
    /// call them from a worker thread (radio_windows.h does).
    std::shared_ptr<radio::RadioDevice> device() const;
    /// The plugin and key of the open device ("" none).
    std::string device_plugin() const;
    std::string device_key() const;
    /// Changes whenever the open device changes (connect, disconnect, a
    /// radio host rebuilt for another cycle period).
    std::uint64_t device_generation() const;
    /// The radio host (nullptr before find_radios). One per process.
    radio::RadioHost* host();
    /// Loads the radio plugins without looking for devices (the plugin
    /// manager lists them); host() is set afterwards unless it failed.
    Result load_radio_plugins();
    /// A plugin is switched on or off (PLG-09): refused while a walk, a
    /// recording or the radio runs. Closes the radio, drops the radio host
    /// and the pipeline (they hold the plugins), runs `change` (which
    /// enables or disables in the loader or the radio host's state), then
    /// takes the technology plugins again; the radios must be found again.
    Result change_plugins(const std::function<Result()>& change);
    /// Observers of the measurement pipeline (the radio windows); kept when
    /// the pipeline is rebuilt.
    void add_observer(std::shared_ptr<measure::MeasurementObserver> observer);
    void remove_observer(const std::shared_ptr<measure::MeasurementObserver>& observer);
    // -- spectrum (UI-24, radio API 1.2 RAD-140) --------------------------------------
    /// Asks the chosen radio for spectra of first_hz..last_hz (the Spectrum
    /// window is open), or for none (nullopt: closed). Only a radio with
    /// SPECTRUM is asked; a measuring radio gets the new plan in update()
    /// (a short restart of its stream without RETUNE_LIVE), except while a
    /// walk test runs: then after it.
    void request_spectrum(std::optional<std::pair<std::uint64_t, std::uint64_t>> band);
    /// The band asked for (nullopt: no spectra asked).
    std::optional<std::pair<std::uint64_t, std::uint64_t>> spectrum_request() const;
    /// The measuring radio's plan has the spectrum flag now.
    bool spectrum_active() const;
    /// Why the asked spectra are not active ("" when they are or none is asked).
    std::string spectrum_note() const;
    /// The spectra received (latest and the waterfall's history); kept
    /// across radio host rebuilds, cleared when the radio or band changes.
    std::shared_ptr<radio::SpectrumStore> spectra() const;
    /// The open device's plan ranges (RAD-32), read once per device; nullopt
    /// when none is open or the plugin gave none.
    std::optional<radio::PlanRanges> plan_ranges();
    /// The calibration table that corrects the levels (RF-02; nullptr none).
    std::shared_ptr<const calibration::Table> calibration_table() const;
    /// Uses `table` from now on and saves it as the user's table
    /// (RadioPaths::calibration_table; in memory only when that is "").
    Result set_calibration_table(const calibration::Table& table);
    const RadioPaths& paths() const;
    /// The stored settings of the radio devices (RAD-47).
    plugin::PluginStateStore& state();

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
    std::map<std::string, std::string> search_notes_;  ///< plugin id -> diagnostic of an empty enumeration
    std::optional<std::size_t> chosen_;
    WalkForm form_;
    RecordingForm recording_form_;
};

}  // namespace yantra::app
