// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replay of recordings (REC-07; docs/formats/RECORDING.md "Replay"):
// one or more recording files played in order on one time axis through
// a measurement pipeline of their own, with the same technology plugins
// as live, so the views get the same records, events and cycles as during
// the walk (the same pipeline code, cycle clock and statistics).
//
// Replay is the engine (single-threaded): advance_to(t) plays every step
// up to replay time t (inputs at their arrival times on a ManualClock
// with their recorded level corrections, ticks at the recorded tick
// times); seek(t) gives the state at instant t (restores the newest
// keyframe before t and plays on to t silently). ReplayPlayer drives it
// in real time or faster on its own thread: play, pause, stop, play again.
// Technology-neutral (PLG-01).
#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/platform/clock.h"
#include "yantra/core/recording/format.h"

namespace yantra::recording {

struct ReplayOptions {
    // The technology plugins (the live run's, for the same mapping and C1).
    std::shared_ptr<measure::TechnologyCatalog> catalog;
    std::function<void(const std::string& message)> log;
};

// What a replay passes on besides the pipeline's output, with its replay
// time (ns from the start of the first file). Every method is optional.
class ReplayListener {
public:
    virtual ~ReplayListener() = default;
    virtual void on_file(std::size_t index, const File& file) { (void)index, (void)file; }
    virtual void on_marker(std::int64_t t, const MarkerLine& marker) { (void)t, (void)marker; }
    virtual void on_position(std::int64_t t, const PositionLine& position) { (void)t, (void)position; }
    virtual void on_floor(std::int64_t t, const FloorLine& floor) { (void)t, (void)floor; }
    // After seek(): the panels show the state at `t` (Replay::last_cycle()).
    virtual void on_seek(std::int64_t t) { (void)t; }
};

class Replay {
public:
    // Reads `paths` (played in this order; REC-07 "a sequence of files").
    static std::unique_ptr<Replay> open(const std::vector<std::filesystem::path>& paths, ReplayOptions options,
                                        std::string& error);
    static std::unique_ptr<Replay> from_files(std::vector<File> files, ReplayOptions options, std::string& error);
    ~Replay();
    Replay(const Replay&) = delete;
    Replay& operator=(const Replay&) = delete;

    // Receivers of the replay pipeline's output (on_input, on_record,
    // on_event, on_cycle); silent during seek().
    void add_observer(std::shared_ptr<measure::MeasurementObserver> observer);
    void add_listener(std::shared_ptr<ReplayListener> listener);

    std::int64_t duration_ns() const noexcept { return duration_; }
    std::int64_t position_ns() const noexcept { return position_; }
    bool at_end() const noexcept;
    // Plays every step up to replay time `t` (clamped to the duration);
    // the number of cycles emitted.
    std::size_t advance_to(std::int64_t t);
    // The state at instant `t` (REC-07 seek): no output except on_seek.
    void seek(std::int64_t t);

    const std::vector<File>& files() const noexcept { return files_; }
    std::size_t current_file() const noexcept { return file_; }
    // Replay time of a pipeline-clock time of file `index`.
    std::int64_t time_of(std::size_t index, std::int64_t mono_ns) const;
    // Markers and positions of all files with their replay times (seek by
    // a click on the plan or the graph, REC-07).
    std::vector<std::pair<std::int64_t, MarkerLine>> markers() const;
    std::vector<std::pair<std::int64_t, PositionLine>> positions() const;
    // The replay time of the position nearest to (x, y) on `floor_id`
    // (plan units); nullopt when the floor has none.
    std::optional<std::int64_t> time_near(const std::string& floor_id, double x, double y) const;

    // The newest cycle emitted (also during seek), nullopt before the first.
    std::optional<measure::MeasurementCycle> last_cycle() const;
    // Replayed cycles that differ from the recorded cycle summary (a
    // different technology plugin or Yantra version), with the first
    // differences.
    std::size_t mismatches() const noexcept { return mismatches_; }
    const std::vector<std::string>& mismatch_messages() const noexcept { return mismatch_messages_; }
    measure::MeasurementPipeline& pipeline() noexcept { return *pipeline_; }

private:
    struct Step;
    struct Forwarder;
    struct FixedCorrector;
    struct Plan;
    Replay(std::vector<File> files, ReplayOptions options);
    bool prepare(std::string& error);
    void start_run(std::size_t run, std::size_t keyframe);
    std::size_t run_step(const Step& step);

    std::vector<File> files_;
    ReplayOptions options_;
    std::unique_ptr<Plan> plan_;              // the runs of files that follow on directly
    std::size_t run_ = 0;                     // the current run
    std::size_t side_ = 0;                    // next side line of the current run
    std::vector<std::int64_t> offsets_;       // replay time of each file's start
    std::int64_t duration_ = 0;
    std::shared_ptr<platform::ManualClock> clock_;
    std::shared_ptr<FixedCorrector> corrector_;
    std::shared_ptr<Forwarder> forwarder_;
    std::unique_ptr<measure::MeasurementPipeline> pipeline_;
    std::vector<std::shared_ptr<ReplayListener>> listeners_;
    std::size_t file_ = 0;
    std::size_t step_ = 0;
    std::uint64_t skip_inputs_upto_ = 0;
    std::int64_t position_ = 0;
    bool muted_ = false;
    std::size_t mismatches_ = 0;
    std::vector<std::string> mismatch_messages_;
};

// Plays a Replay on its own thread (REC-07): real time (speed 1), faster
// (speed > 1) or as fast as possible (speed 0), against `wall` (the
// system clock unless a test gives a ManualClock). Observers and
// listeners of the replay are called on the player's thread.
class ReplayPlayer {
public:
    enum class State { Stopped, Playing, Paused, Ended };
    explicit ReplayPlayer(std::shared_ptr<Replay> replay, std::shared_ptr<const platform::Clock> wall = nullptr);
    ~ReplayPlayer();
    ReplayPlayer(const ReplayPlayer&) = delete;
    ReplayPlayer& operator=(const ReplayPlayer&) = delete;

    // From the current position; at the end: from the start. speed 0 = as
    // fast as possible, else 0.1..100 times real time.
    void play(double speed = 1.0);
    void pause();
    void stop();        // pause and back to the start
    void play_again();  // from the start, at the last speed
    void seek(std::int64_t t);

    State state() const;
    std::int64_t position_ns() const;
    double speed() const;
    // Waits until `pred(state, position)` holds; false after `timeout`.
    bool wait(const std::function<bool(State, std::int64_t)>& pred, std::chrono::milliseconds timeout) const;

private:
    void loop();
    std::shared_ptr<Replay> replay_;
    std::shared_ptr<const platform::Clock> wall_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    State state_ = State::Stopped;
    double speed_ = 1.0;
    std::int64_t base_position_ = 0;
    std::int64_t base_wall_ = 0;
    std::int64_t position_ = 0;
    bool quit_ = false;
    std::thread thread_;
};

const char* player_state_name(ReplayPlayer::State state) noexcept;

}  // namespace yantra::recording
