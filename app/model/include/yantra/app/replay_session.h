// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replay in the app (REC-07, REC-08, REC-06; step 9.5; docs/APP.md
// "Survey panels"), without drawing: a recording of the project's
// `recordings/` folder (a session: its files in order) is replayed
// through the core (recording::Replay, ReplayPlayer) with the live run's
// technology plugins into the survey panels' model:
//
// - open(): one pass over the whole recording fills the graphs, the event
//   list (with its markers and voice notes) and the statistics; the
//   monitor's history is then frozen and the player's cycles only set the
//   frame, so every panel shows the state at the player's instant;
// - play, pause, stop, play again, speed, seek (slider, graph cursor);
// - voice notes are played at their instant through the app's AudioIo
//   (tick() on the UI thread);
// - cut(): start to cursor / cursor to end saved as a new recording,
//   listed in the project (REC-08, core recording/cut.h).
// While a replay is open the live radio is stopped (AppState).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "yantra/app/audio.h"
#include "yantra/app/result.h"
#include "yantra/app/survey_monitor.h"
#include "yantra/core/recording/replay.h"

namespace yantra::project {
class Project;
}

namespace yantra::app {

/// A recording session in the recordings folder.
struct RecordingChoice {
    std::string name;  ///< session name
    std::vector<std::filesystem::path> files;  ///< in file-number order
    std::uint64_t bytes = 0;
};

/// Lists the sessions of `folder` (files <name>_<nnnn>.yrec[.zst]), by name.
std::vector<RecordingChoice> list_recordings(const std::filesystem::path& folder);

struct ReplayView {
    bool open = false;
    std::string name;
    recording::ReplayPlayer::State state = recording::ReplayPlayer::State::Stopped;
    std::int64_t position_ns = 0;  ///< replay time
    std::int64_t duration_ns = 0;
    double speed = 1.0;
    std::int64_t cursor_mono = 0;  ///< the position on the pipeline clock (the graphs' time axis)
    std::vector<std::pair<std::int64_t, std::string>> markers;  ///< replay time, "n label"
    std::size_t voice_notes = 0;
    std::size_t voice_played = 0;
};

class ReplaySession {
public:
    ReplaySession(std::shared_ptr<project::Project> project, std::shared_ptr<SurveyMonitor> monitor);
    ~ReplaySession();
    ReplaySession(const ReplaySession&) = delete;
    ReplaySession& operator=(const ReplaySession&) = delete;

    void set_catalog(std::shared_ptr<measure::TechnologyCatalog> catalog);
    void set_audio(std::shared_ptr<AudioIo> audio);
    /// Another observer of the played replay (not of the pass that fills
    /// the graphs): e.g. measure::FeatureFeed, so feature plugins see the
    /// replayed records as they see a walk test's (PLG-02, PLG-24). Kept
    /// for every recording opened later.
    void add_observer(std::shared_ptr<measure::MeasurementObserver> observer);

    /// The recordings of the project's folder.
    std::vector<RecordingChoice> recordings() const;
    std::filesystem::path folder() const;

    Result open(const RecordingChoice& recording);
    Result open_files(const std::vector<std::filesystem::path>& files, const std::string& name);
    void close();
    bool is_open() const;

    Result play(double speed = 1.0);
    Result pause();
    Result stop();
    Result play_again();
    Result seek(std::int64_t t_ns);
    /// The graphs' cursor: a pipeline-clock time of the recording.
    Result seek_mono(std::int64_t mono_ns);
    ReplayView view() const;

    /// REC-08: start to cursor (`head`) or cursor to end, saved as `name`.
    Result cut(bool head, const std::string& name);

    /// UI thread, each frame: plays the voice notes the player reached.
    void tick();
    /// Tests: waits for the player (ReplayPlayer::wait).
    bool wait(const std::function<bool(recording::ReplayPlayer::State, std::int64_t)>& pred, int timeout_ms) const;

private:
    struct Listener;
    std::shared_ptr<project::Project> project_;
    std::shared_ptr<SurveyMonitor> monitor_;
    std::shared_ptr<measure::TechnologyCatalog> catalog_;
    std::shared_ptr<AudioIo> audio_;
    std::vector<std::shared_ptr<measure::MeasurementObserver>> observers_;
    std::shared_ptr<recording::Replay> replay_;
    std::unique_ptr<recording::ReplayPlayer> player_;
    std::shared_ptr<Listener> listener_;
    std::vector<std::filesystem::path> files_;
    std::string name_;
    std::size_t voice_played_ = 0;
};

}  // namespace yantra::app
