// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The walk-test view's model with the simulator radio (TST-50, step 9.4;
// IND-20, GEO-02, GEO-05, PLG-27, DEMO-14): the radios found through the
// radio host, the simulator chosen by default, the mouse survey on the
// Fraunces demo (points where the simulator calculated them, at most one
// per spacing, "move slower", click to interpolate), a point-and-click
// walk and manual points. Real time (the simulator streams at its record
// rate); the waits are on observable conditions with a deadline. Skips
// when the simulator is not built. No hardware.
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "yantra/app/audio.h"
#include "yantra/app/interpolation_runner.h"
#include "yantra/app/live_survey.h"
#include "yantra/app/replay_session.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "live" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

/// The longest wait for the simulator (seconds; the sanitizer trees run
/// it several times slower).
constexpr double kDeadline = 20.0 * YANTRA_TEST_TIME_SCALE;

/// Polls `done` until it holds or `seconds` pass (an observable condition,
/// not a fixed sleep); `step` runs between polls (e.g. the cursor moving).
bool wait_for(const std::function<bool()>& done, double seconds, const std::function<void()>& step = {}) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        if (step) step();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

/// A copy of the demo project and a radio folder holding only the
/// simulator (the other radio plugins would look for hardware).
struct Rig {
    std::shared_ptr<pj::Project> project;
    std::string floor;  ///< the first floor with a scale
    RadioPaths paths;
    bool ok = false;
    explicit Rig(const std::string& name) {
        const fs::path sim = fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator.so";
        if (!fs::exists(sim) || !fs::exists(YANTRA_TEST_DEMO_PROJECT)) return;
        const fs::path d = scratch(name);
        fs::create_directories(d / "radios");
        fs::copy_file(sim, d / "radios" / "simulator.so");
        fs::copy_file(YANTRA_TEST_DEMO_PROJECT, d / "demo.yantra");
        project = pj::Project::open(d / "demo.yantra");
        const bm::Building b = project->building();
        for (const bm::Floor& f : b.floors)
            if (b.has_scale(f) && !f.antennas.empty()) {
                floor = f.id;
                break;
            }
        REQUIRE_FALSE(floor.empty());
        paths.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
        paths.radio_folders = {d / "radios"};
        ok = true;
    }
    bm::Floor data() const { return *project->floor(floor); }
};

/// A plan point at (fx, fy) of the floor's plan size.
bm::Point2 at(const bm::Floor& f, double fx, double fy) { return {f.plan_width * fx, f.plan_height * fy}; }

}  // namespace

TEST_CASE("PLG-27: the radios are found and the simulator is chosen by default; start needs a radio") {
    Rig r("find");
    if (!r.ok) return;  // the simulator is not built
    LiveSurvey live(r.project, r.paths);
    CHECK(live.start_reason(r.floor) == "Find the radios first.");
    const Result found = live.find_radios();
    INFO(found.message, " ", live.radio_note());
    REQUIRE(found.ok);
    REQUIRE_FALSE(live.radios().empty());
    const RadioChoice* c = live.chosen();
    REQUIRE(c != nullptr);
    CHECK(c->simulated);
    CHECK(c->uses_context);
    CHECK(c->label.find("(") != std::string::npos);
    CHECK(live.start_reason(r.floor).empty());
    CHECK(live.start_reason("no-such-floor") == "Choose a floor first.");
    CHECK_FALSE(live.choose_radio(99).ok);
    live.form().kind = WalkKind::Manual;
    live.form().manual_seconds = 0.0;
    CHECK_FALSE(live.start_reason(r.floor).empty());
    CHECK_FALSE(live.running());
    CHECK_FALSE(live.click({1, 1}).ok);
}

TEST_CASE("DEMO-14: the mouse survey stores points where the simulator measured, one per spacing; click interpolates") {
    Rig r("mouse");
    if (!r.ok) return;
    const bm::Floor f = r.data();
    const std::size_t before = r.project->point_count(r.floor);
    LiveSurvey live(r.project, r.paths);
    REQUIRE(live.find_radios().ok);
    live.show_floor(r.floor);
    live.form().kind = WalkKind::MouseSurvey;
    live.form().spacing_m = 0.25;
    const Result s = live.start(r.floor);
    INFO(s.message);
    REQUIRE(s.ok);
    REQUIRE(live.running());
    WalkView v = live.view();
    CHECK(v.kind == WalkKind::MouseSurvey);
    CHECK(v.hint.find("Move the mouse slowly") != std::string::npos);
    CHECK(v.hint.find("click to interpolate") != std::string::npos);
    CHECK_FALSE(v.too_fast);

    // Move the "mouse" slowly along the middle of the plan (in UI time).
    double t = 0.0;
    double fx = 0.30;
    const auto clock0 = std::chrono::steady_clock::now();
    auto step = [&] {
        t = std::chrono::duration<double>(std::chrono::steady_clock::now() - clock0).count();
        // Back and forth between 0.3 and 0.7 of the width (never parks).
        const double u = std::fmod(0.02 * t, 0.8);
        fx = 0.30 + (u < 0.4 ? u : 0.8 - u);
        live.cursor(at(f, fx, 0.5), t);
    };
    REQUIRE(wait_for([&] { return live.view().points >= 6; }, kDeadline, step));
    v = live.view();
    CHECK(v.speed_mps < live.form().max_speed_mps);
    CHECK_FALSE(v.too_fast);
    REQUIRE(v.session != 0);
    const auto pts = r.project->points(v.session);
    REQUIRE(pts.size() >= 6);
    // The points are where the cursor went (the simulator's position) and
    // at least the spacing apart.
    const bm::Building b = r.project->building();
    const bm::Floor& bf = *b.find_floor(r.floor);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        CHECK(pts[i].plan.y == doctest::Approx(f.plan_height * 0.5).epsilon(0.02));
        CHECK(pts[i].plan.x >= f.plan_width * 0.29);
        CHECK(pts[i].plan.x <= f.plan_width * 0.71);
        CHECK(pts[i].kind == "simulated");
        if (i > 0) {
            const bm::Point2 a = b.to_plan_metres(bf, pts[i - 1].plan).value();
            const bm::Point2 c = b.to_plan_metres(bf, pts[i].plan).value();
            CHECK(std::hypot(c.x - a.x, c.y - a.y) >= 0.25 - 1e-9);
        }
    }

    // A fast jump of the mouse: "move slower".
    live.cursor(at(f, 0.9, 0.5), t + 0.05);
    live.cursor(at(f, 0.1, 0.5), t + 0.10);
    CHECK(live.view().too_fast);
    CHECK(live.view().speed_mps > live.form().max_speed_mps);

    // A click interpolates the floor's points (the caller's runner).
    InterpolationRunner run(r.project, InterpolationRunner::Options{2});
    REQUIRE(run.run(r.floor).ok);
    REQUIRE(live.stop().ok);
    CHECK_FALSE(live.running());
    run.wait();
    std::int64_t id = 0;
    const auto msg = run.poll(&id);
    REQUIRE(msg);
    INFO(msg->message);
    CHECK(msg->ok);
    CHECK(id != 0);
    // The points stay after the stop.
    CHECK(r.project->point_count(r.floor) >= before + 6);
}

TEST_CASE("GEO-02, GEO-05: a point-and-click walk and manual points with the simulator") {
    Rig r("walk");
    if (!r.ok) return;
    const bm::Floor f = r.data();
    LiveSurvey live(r.project, r.paths);
    REQUIRE(live.find_radios().ok);
    live.form().kind = WalkKind::PointAndClick;
    live.form().name = "Corridor walk";
    REQUIRE(live.start(r.floor).ok);
    WalkView v = live.view();
    CHECK_FALSE(v.hint.empty());
    REQUIRE(live.click(at(f, 0.3, 0.5)).ok);
    REQUIRE(live.view().open_start);
    const std::uint64_t c0 = live.cycles();
    REQUIRE(wait_for([&] { return live.cycles() >= c0 + 3; }, kDeadline));
    const Result end = live.click(at(f, 0.6, 0.5));
    CHECK(end.message == "Segment 1 ended.");
    v = live.view();
    CHECK_FALSE(v.open_start);
    CHECK(v.segments == 1);
    REQUIRE(v.points >= 2);
    for (const pj::StoredPoint& p : r.project->points(v.session)) {
        CHECK(p.plan.x >= f.plan_width * 0.3 - 1e-6);
        CHECK(p.plan.x <= f.plan_width * 0.6 + 1e-6);
    }
    REQUIRE(live.delete_last().ok);
    CHECK(live.view().points == 0);
    CHECK(live.view().segments == 0);
    const std::int64_t session = v.session;
    REQUIRE(live.stop().ok);
    bool named = false;
    for (const pj::Session& s : r.project->sessions(r.floor))
        if (s.id == session) named = s.name == "Corridor walk" && s.kind == "walk";
    CHECK(named);

    // Manual points: each click measures for the set time (GEO-05).
    live.form().kind = WalkKind::Manual;
    live.form().manual_seconds = 2.0;
    REQUIRE(live.start(r.floor).ok);
    REQUIRE(live.click(at(f, 0.4, 0.4)).ok);
    CHECK(live.view().manual_point);
    REQUIRE(wait_for([&] { return live.view().points >= 1; }, kDeadline));
    const std::int64_t manual = live.view().session;
    REQUIRE(live.stop().ok);
    const auto pts = r.project->points(manual);
    REQUIRE(pts.size() == 1);
    CHECK(pts[0].kind == "manual");
    CHECK(pts[0].plan.x == doctest::Approx(f.plan_width * 0.4));
    CHECK(pts[0].plan.y == doctest::Approx(f.plan_height * 0.4));
}

// -- step 9.5: monitoring, recording, markers, voice notes, ring buffer, replay, cut ------------

namespace {

std::size_t yrec_files(const fs::path& folder) {
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec)) n += e.path().string().find(".yrec") != std::string::npos ? 1 : 0;
    return n;
}

/// The project's listed recording file `name` (nullopt when not listed).
std::optional<pj::RecordingFile> listed_file(const pj::Project& p, const std::string& name) {
    for (const pj::RecordingFile& f : p.recordings())
        if (f.file_name == name) return f;
    return std::nullopt;
}

}  // namespace

TEST_CASE("REC-01, REC-02, REC-05, REC-06, REC-11: monitoring fills the panels; a recording with a marker and a voice note is listed") {
    Rig r("record");
    if (!r.ok) return;
    const std::size_t listed_before = r.project->recordings().size();
    LiveSurvey live(r.project, r.paths);
    auto audio = std::make_shared<FakeAudio>();
    live.set_audio(audio);
    CHECK_FALSE(live.record_reason().empty());  // no radio yet
    CHECK_FALSE(live.start_radio().ok);          // not searched
    REQUIRE(live.find_radios().ok);
    live.show_floor(r.floor);
    const Result started = live.start_radio();
    INFO(started.message);
    REQUIRE(started.ok);
    CHECK(live.radio_running());
    CHECK(live.radio_state().text.find("measuring") != std::string::npos);
    CHECK_FALSE(live.find_radios().ok);  // the radio is measuring
    const auto& monitor = live.monitor();
    REQUIRE(wait_for([&] { return monitor->frame().valid; }, kDeadline));
    CHECK_FALSE(live.voice_reason().empty());  // no recording
    CHECK_FALSE(live.marker("x").ok);

    live.recording_form().name = "Lobby";
    live.recording_form().notes = "monitoring test";
    const Result rec = live.record();
    INFO(rec.message);
    REQUIRE(rec.ok);
    CHECK(live.recording());
    CHECK(live.exit_guard());
    CHECK_FALSE(live.stop_radio().ok);  // recording
    CHECK_FALSE(live.record().ok);      // one at a time
    const std::uint64_t c0 = live.cycles();
    REQUIRE(wait_for([&] { return live.cycles() >= c0 + 2; }, kDeadline));
    const Result mk = live.marker("door");
    CHECK(mk.message == "Marker 1: door.");
    REQUIRE(live.voice_reason().empty());
    REQUIRE(live.voice_start().ok);
    CHECK(live.voice_recording());
    audio->advance(1.0);
    CHECK(live.voice_seconds() == doctest::Approx(1.0).epsilon(0.01));
    const Result vn = live.voice_stop("stairs");
    INFO(vn.message);
    REQUIRE(vn.ok);
    CHECK(live.recording_status().voice_notes == 1);
    // cycles() counts a tick before its observers (the recorder) are done:
    // two more cycles make sure four whole ones went into the recording.
    REQUIRE(wait_for([&] { return live.cycles() >= c0 + 6; }, kDeadline));
    const std::uint64_t recorded_at_stop = 4;
    const Result stop = live.stop_recording();
    INFO(stop.message);
    REQUIRE(stop.ok);
    CHECK_FALSE(live.exit_guard());
    const fs::path folder = live.recordings_folder();
    CHECK(folder == r.project->path().parent_path() / "recordings");
    CHECK(fs::exists(folder / "Lobby_0000.yrec"));
    CHECK(fs::exists(folder / "Lobby_0000_voice_001.wav"));
    CHECK(r.project->recordings().size() == listed_before + 1);
    CHECK(listed_file(*r.project, "Lobby_0000.yrec"));
    std::size_t markers = 0, voices = 0;
    for (const SurveyEvent& e : monitor->events()) {
        markers += e.kind == EventKind::Marker ? 1 : 0;
        voices += e.kind == EventKind::VoiceNote ? 1 : 0;
    }
    CHECK(markers == 1);
    CHECK(voices == 1);
    REQUIRE(live.stop_radio().ok);
    CHECK_FALSE(live.radio_running());

    // Replay (REC-07): the pass fills the graphs; the player plays the voice note; cut (REC-08).
    ReplaySession replay(r.project, monitor);
    replay.set_catalog(live.catalog());
    replay.set_audio(audio);
    const auto sessions = replay.recordings();
    REQUIRE(sessions.size() == 1);  // the demo's recordings are elsewhere
    CHECK(sessions[0].name == "Lobby");
    const Result opened = replay.open(sessions[0]);
    INFO(opened.message);
    REQUIRE(opened.ok);
    CHECK(monitor->history_frozen());
    // The pass gives one graph sample per recorded cycle.
    std::size_t recorded_cycles = 0;
    {
        std::ifstream in(folder / "Lobby_0000.yrec");
        for (std::string line; std::getline(in, line);) recorded_cycles += line.find("\"t\":\"cycle\"") != std::string::npos ? 1 : 0;
    }
    CHECK(recorded_cycles >= recorded_at_stop);
    CHECK(monitor->plot().samples.size() == recorded_cycles);
    ReplayView v = replay.view();
    CHECK(v.duration_ns > 0);
    REQUIRE(v.markers.size() == 1);
    CHECK(v.markers[0].second == "1 door");
    CHECK(v.voice_notes == 1);
    REQUIRE(replay.play(0.0).ok);
    REQUIRE(replay.wait([](auto s, auto) { return s != yantra::recording::ReplayPlayer::State::Playing; },
                        static_cast<int>(kDeadline * 1000)));
    replay.tick();
    CHECK(audio->played() == 1);
    CHECK(replay.view().voice_played == 1);
    REQUIRE(replay.seek(v.duration_ns / 2).ok);
    REQUIRE(replay.wait([&](auto, auto t) { return t == v.duration_ns / 2; }, static_cast<int>(kDeadline * 1000)));
    CHECK(monitor->frame().valid);
    const Result head = replay.cut(true, "Lobby_head");
    INFO(head.message);
    REQUIRE(head.ok);
    CHECK(fs::exists(folder / "Lobby_head_0000.yrec"));
    CHECK(r.project->recordings().size() == listed_before + 2);
    CHECK_FALSE(replay.cut(false, "a/b").ok);
    replay.close();
    CHECK_FALSE(monitor->history_frozen());
    CHECK(monitor->plot().samples.empty());
}

TEST_CASE("REC-03: a walk test writes its recording file and links its session to it") {
    Rig r("walkfile");
    if (!r.ok) return;
    const bm::Floor f = r.data();
    LiveSurvey live(r.project, r.paths);
    REQUIRE(live.find_radios().ok);
    live.form().kind = WalkKind::PointAndClick;
    live.form().name = "East wing";
    const Result s = live.start(r.floor);
    INFO(s.message);
    REQUIRE(s.ok);
    CHECK(live.recording());
    CHECK(live.recording_status().name == "East_wing");
    CHECK_FALSE(live.stop_recording().ok);  // the walk records into it
    REQUIRE(live.click(at(f, 0.3, 0.5)).ok);
    const std::uint64_t c0 = live.cycles();
    REQUIRE(wait_for([&] { return live.cycles() >= c0 + 3; }, kDeadline));
    REQUIRE(live.click(at(f, 0.6, 0.5)).ok);
    const std::int64_t session = live.view().session;
    REQUIRE(live.stop().ok);
    CHECK_FALSE(live.recording());
    CHECK_FALSE(live.radio_running());
    const fs::path file = live.recordings_folder() / "East_wing_0000.yrec";
    REQUIRE(fs::exists(file));
    const auto listed = listed_file(*r.project, "East_wing_0000.yrec");
    REQUIRE(listed);
    std::string linked;
    for (const pj::Session& x : r.project->sessions(r.floor))
        if (x.id == session) linked = x.recording_session;
    CHECK_FALSE(linked.empty());
    CHECK(linked == listed->session_id);
    // The file holds the walk's lines (start, segment, stop).
    std::ifstream in(file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("\"t\":\"walk\"") != std::string::npos);
    // A second walk of the same name gets its own file.
    REQUIRE(live.start(r.floor).ok);
    CHECK(live.recording_status().name == "East_wing_2");
    REQUIRE(live.stop().ok);
}

TEST_CASE("REC-09: the ring buffer keeps the newest files while the radio runs and is saved as a recording") {
    Rig r("ring");
    if (!r.ok) return;
    r.paths.ring_folder = r.project->path().parent_path() / "ring";
    LiveSurvey live(r.project, r.paths);
    REQUIRE(live.find_radios().ok);
    live.show_floor(r.floor);
    live.recording_form().ring = true;
    live.recording_form().ring_minutes = 2.0;           // two files
    live.recording_form().ring_messages_per_file = 8;   // tests: small files
    CHECK(live.save_ring("x").ok == false);             // empty
    const Result st = live.start_radio();
    INFO(st.message);
    REQUIRE(st.ok);
    // Wait until the ring has deleted files (more than two were written).
    REQUIRE(wait_for([&] { return yrec_files(r.paths.ring_folder) >= 2 && live.ring_files().size() >= 2 &&
                                  live.ring_files().front().filename().string() != "ring_0000.yrec"; },
                     kDeadline * 2));
    CHECK(live.ring_files().size() <= 2);
    CHECK(yrec_files(r.paths.ring_folder) <= 3);  // the oldest goes when a new one opens
    const Result saved = live.save_ring("Last_minutes");
    INFO(saved.message);
    REQUIRE(saved.ok);
    CHECK(fs::exists(live.recordings_folder() / "Last_minutes_0000.yrec"));
    CHECK(listed_file(*r.project, "Last_minutes_0000.yrec"));
    REQUIRE(live.stop_radio().ok);
    // After the stop the kept files can still be saved.
    CHECK_FALSE(live.ring_files().empty());
    live.recording_form().ring = false;
    CHECK(live.apply_ring().ok);
}
