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
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "yantra/app/interpolation_runner.h"
#include "yantra/app/live_survey.h"
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
