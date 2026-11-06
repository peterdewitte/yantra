// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A walk test on a floor (IND-20, GEO-02, GEO-05, GEO-06): the measurement
// observer that turns measurement cycles into the project's measured
// points (project schema 4) as the surveyor clicks on the plan.
// docs/SURVEY.md "Walk test".
//
// Kinds (`sessions.kind`):
// - walk (GEO-02): point and click or continuous (walk.h). A cycle at time
//   T inside a closed segment is placed at the interpolated point; cycles
//   after an open start wait for the closing click; a pause drops them;
//   delete last segment removes the segment's points.
// - manual (GEO-05): a point at a chosen place averaged over N seconds:
//   the cycles with T in (t, t + N] (at least the first cycle after t)
//   become one point (sample.h average_samples), stored when a cycle with
//   T >= t + N arrives, a new point starts, or the test ends.
// - simulated: every cycle whose simulator position (RAD-19) is on the
//   floor is a point at that position (demos and tests without clicks),
//   at most one per min_spacing_m (the mouse survey, DEMO-14).
//
// A floor change (GEO-06) ends the session and goes on in a new one on
// the other floor that `continues` it; an open segment is dropped there
// (the start on the new floor is the next click).
//
// Every action has a time on the pipeline clock (a cycle's
// time.monotonic_ns); actions are kept in time order. With a recorder,
// every action is written as a `walk` line (and floor changes as `floor`
// lines), so a replay feeding the same WalkTest through apply() gives the
// same points (docs/formats/RECORDING.md). With a walker callback the
// walk position (the last click, or walker()) is published, e.g. to
// context::BuildingContext::set_walk, which the simulator follows
// (position_source walk).
//
// Thread-safe: on_cycle runs on the ticking thread, the actions on the
// application thread.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/core/context/building_context.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/platform/clock.h"
#include "yantra/core/project/project.h"
#include "yantra/core/recording/format.h"
#include "yantra/core/recording/replay.h"
#include "yantra/core/recording/writer.h"
#include "yantra/core/survey/walk.h"

namespace yantra::survey {

struct WalkTestOptions {
    std::shared_ptr<project::Project> project;
    std::string floor_id;
    std::string kind = "walk";  ///< walk, manual or simulated
    WalkMode mode = WalkMode::PointAndClick;
    std::string name, radio, calibration, notes;  ///< the session's fields (IND-20)
    std::shared_ptr<recording::Recorder> recorder;  ///< optional
    std::function<void(const std::optional<context::FloorPoint>&)> walker;  ///< optional
    std::string device;  ///< simulated: only this device's position; "" any
    /// Simulated (the mouse survey, DEMO-14): at most one point per this
    /// distance in metres; a position closer than that to the last stored
    /// point of the session is not stored. 0: every position.
    double min_spacing_m = 0.0;
    /// The recording session the points come from (`sessions.recording_session`)
    /// without a recorder, e.g. when a recording is imported (PP-01, viewer.h).
    std::string recording_session;
    /// The UTC of a session that starts before the first cycle (a start
    /// time has no UTC of its own); nullptr: the system clock. Generated
    /// walks (the demo, DEMO-06) give the pipeline's clock so the session
    /// times follow the generated clock.
    std::shared_ptr<const platform::Clock> clock;
};

class WalkTest final : public measure::MeasurementObserver {
public:
    /// std::invalid_argument without a project or with an unknown kind;
    /// ProjectError NotFound for an unknown floor.
    explicit WalkTest(WalkTestOptions options);

    /// Starts the session (a `start` line). Once.
    void start(std::int64_t t_ns);
    /// A click on the plan (walk). std::invalid_argument when it goes back in time.
    void click(std::int64_t t_ns, Point2 plan);
    void pause(std::int64_t t_ns);
    /// Walk: removes the last segment (and an open start); manual: the
    /// point being averaged, else the last point. false: nothing to remove.
    bool delete_last(std::int64_t t_ns);
    void set_mode(std::int64_t t_ns, WalkMode mode);
    /// Manual point at `plan`, averaged over `seconds` (> 0).
    void point(std::int64_t t_ns, Point2 plan, double seconds, const std::string& label = {});
    /// GEO-06: on to another floor of the project.
    void change_floor(std::int64_t t_ns, const std::string& floor_id);
    /// Ends the test: a collecting manual point is stored, open segments dropped.
    void finish(std::int64_t t_ns);
    /// The walker's position without a click (walk position only).
    void walker(Point2 plan);
    /// A recorded action (replay); not written to the recorder.
    void apply(const recording::WalkLine& line);

    void on_cycle(const measure::MeasurementCycle& cycle) override;

    bool running() const;
    std::string floor_id() const;
    std::int64_t session_id() const;          ///< the current session (0 before start)
    std::vector<std::int64_t> sessions() const;  ///< every session of this test, in order
    std::size_t stored() const;                ///< points stored so far

private:
    struct Pending {
        std::int64_t t = 0, utc = 0;
        nlohmann::json sample;
    };
    void act(const recording::WalkLine& line, bool record);
    void begin_session(std::int64_t t, std::optional<std::int64_t> continues);
    void end_session(std::int64_t end_t);
    void store(std::int64_t t, std::int64_t utc, Point2 plan, std::uint32_t group, std::uint32_t cycles,
               const std::string& label, const nlohmann::json& sample);
    void flush(const WalkSegment& segment);
    void close_manual();
    void publish(const std::optional<Point2>& plan);
    void note_provenance(const measure::MeasurementCycle& cycle);
    recording::WalkLine line(std::int64_t t, const std::string& action, const std::optional<Point2>& plan = {}) const;

    WalkTestOptions o_;
    mutable std::mutex m_;
    bool started_ = false, finished_ = false;
    bool undone_ = false;  // the last delete_last removed something
    std::string floor_;
    std::optional<double> mpu_;  // metres per plan unit of floor_
    WalkRoute route_;
    std::vector<Pending> pending_;
    std::int64_t session_ = 0;
    std::vector<std::int64_t> sessions_;
    std::int64_t next_sequence_ = 1;
    std::size_t stored_ = 0;
    std::uint32_t manual_number_ = 0;
    struct Manual {
        std::int64_t t = 0;
        double seconds = 0.0;
        Point2 plan;
        std::string label;
        std::vector<Pending> samples;
    };
    std::optional<Manual> manual_;
    std::map<std::string, std::string> plugins_;  // provenance (PLG-12)
    std::string radio_;
    std::optional<std::pair<std::int64_t, std::int64_t>> last_cycle_;  // pipeline time, UTC
    std::optional<Point2> last_sim_m_;  // simulated: the last stored position (metres)
};

/// Replay listener feeding a recording's walk lines into a WalkTest (the
/// WalkTest also observes the replay's pipeline for the cycles).
class WalkReplay final : public recording::ReplayListener {
public:
    explicit WalkReplay(std::shared_ptr<WalkTest> test) : test_(std::move(test)) {}
    void on_walk(std::int64_t t, const recording::WalkLine& line) override;

private:
    std::shared_ptr<WalkTest> test_;
};

}  // namespace yantra::survey
