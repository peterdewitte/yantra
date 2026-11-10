// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The DEMO-14 mouse survey in the app (TST-50, step 9.4), driven through
// the headless UI harness with the simulator radio on a copy of the
// Fraunces demo: find the radios, start the mouse survey, move the mouse
// slowly over the plan (points appear and are drawn), the hint and the
// "move slower" warning on the plan, a click interpolates and the map is
// shown, Esc stops. Real time (the simulator's record rate); waits are on
// observable conditions. Skips when the simulator is not built.
#include <doctest/doctest.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/project/project.h"
#include "yantra/core/radio/radio_host.h"
#include "yantra/core/raytrace/model.h"
#include "yantra/core/recording/replay.h"
#include "yantra/core/report/coverage.h"
#include "yantra/core/survey/points.h"

namespace fs = std::filesystem;
namespace bm = yantra::building;
using namespace yantra::app;
using yantra::test::UiHarness;

TEST_CASE("DEMO-14 in the app: the mouse survey with the simulator, move slowly, click to interpolate, Esc stops") {
    const fs::path sim = fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator.so";
    if (!fs::exists(sim) || !fs::exists(YANTRA_TEST_DEMO_PROJECT)) return;
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "live_ui";
    fs::remove_all(d);
    fs::create_directories(d / "radios");
    fs::copy_file(sim, d / "radios" / "simulator.so");
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, d / "demo.yantra");
    AppPaths paths;
    paths.radios.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
    paths.radios.radio_folders = {d / "radios"};
    paths.interpolation_threads = 2;

    UiHarness ui;
    AppState state(AppSettings{}, paths);
    ImGui::SetCurrentContext(ui.context());
    Gui::apply_style();
    Gui gui(state);
    gui.set_wait_for_tiles(true);
    ui.set_draw([&] { gui.draw(); });
    ui.set_display_size(1400.0F, 1000.0F);
    REQUIRE(ui.frames(3));
    REQUIRE(state.open_project(d / "demo.yantra").ok);
    // The first floor with a scale and antennas.
    const bm::Building b = state.project()->building();
    std::string floor;
    for (const bm::Floor& f : b.floors)
        if (b.has_scale(f) && !f.antennas.empty() && floor.empty()) floor = f.id;
    REQUIRE_FALSE(floor.empty());
    REQUIRE(state.project_window()->select(floor));
    REQUIRE(ui.frames(3));
    const bm::Floor f = *b.find_floor(floor);

    REQUIRE(ui.click("panel.walk"));
    REQUIRE(ui.frames(2));
    REQUIRE(ui.click("walk.find_radios"));
    REQUIRE(ui.frames(2));
    REQUIRE_MESSAGE(state.live()->chosen(), state.live()->radio_note());
    CHECK(state.live()->chosen()->simulated);
    state.live()->form().kind = WalkKind::MouseSurvey;  // the kind combo (no probe label)
    REQUIRE(ui.frames(2));
    REQUIRE(ui.has("walk.spacing"));
    REQUIRE_FALSE(ui.find("walk.start")->disabled);
    REQUIRE(ui.click("walk.start"));
    REQUIRE(ui.frames(2));
    REQUIRE_MESSAGE(state.live()->running(), state.status());
    CHECK(ui.has("walk.stop"));

    // Move the mouse slowly along the middle of the plan until points come.
    const std::size_t drawn0 = gui.canvas().stats().points;
    // Back and forth between 0.3 and 0.7 of the width (never parks); the
    // deadline grows in the sanitizer trees.
    double u = 0.0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30 * YANTRA_TEST_TIME_SCALE);
    while (state.live()->view().points < 5 && std::chrono::steady_clock::now() < end) {
        u = std::fmod(u + 0.0005, 0.8);
        const double fx = 0.30 + (u < 0.4 ? u : 0.8 - u);
        const ImVec2 p = gui.canvas().screen_of({f.plan_width * fx, f.plan_height * 0.5});
        ui.mouse_move(p.x, p.y);
        REQUIRE(ui.frame());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(state.live()->view().points >= 5);
    REQUIRE(ui.frames(3));
    CHECK(gui.tool_overlay().hint.find("click to interpolate") != std::string::npos);
    CHECK_FALSE(gui.tool_overlay().move_slower);
    CHECK(gui.canvas().stats().points > drawn0);  // the new points are drawn (live colouring)

    // A fast sweep: "move slower".
    for (int i = 0; i < 6; ++i) {
        const ImVec2 p = gui.canvas().screen_of({f.plan_width * (i % 2 == 0 ? 0.1 : 0.9), f.plan_height * 0.5});
        ui.mouse_move(p.x, p.y);
        REQUIRE(ui.frame());
    }
    CHECK(gui.tool_overlay().move_slower);

    // A click on the plan interpolates; the map is shown.
    const ImVec2 c = gui.canvas().screen_of({f.plan_width * 0.5, f.plan_height * 0.5});
    ui.click_at(c.x, c.y);
    REQUIRE((state.interpolation()->running() || state.status().find("Interpolat") != std::string::npos));
    state.interpolation()->wait();
    REQUIRE(ui.run_until([&] { return state.status().rfind("Interpolated", 0) == 0; }, 30));
    CHECK(gui.canvas().stats().interpolation);

    // Esc stops; the points stay.
    const std::int64_t session = state.live()->view().session;
    ui.key(ImGuiKey_Escape);
    REQUIRE(ui.frames(2));
    CHECK_FALSE(state.live()->running());
    CHECK(state.project()->points(session).size() >= 5);
    CHECK(ui.has("walk.start"));
}

TEST_CASE("UI-01..04, UI-08, UI-11, REC-01, REC-05, REC-07, REC-08, REC-11 in the app: panels, graphs, status bar, recording and replay") {
    const fs::path sim = fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator.so";
    if (!fs::exists(sim) || !fs::exists(YANTRA_TEST_DEMO_PROJECT)) return;
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "survey_ui";
    fs::remove_all(d);
    fs::create_directories(d / "radios");
    fs::copy_file(sim, d / "radios" / "simulator.so");
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, d / "demo.yantra");
    AppPaths paths;
    paths.radios.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
    paths.radios.radio_folders = {d / "radios"};
    paths.interpolation_threads = 2;

    UiHarness ui;
    AppState state(AppSettings{}, paths);
    ImGui::SetCurrentContext(ui.context());
    Gui::apply_style();
    Gui gui(state);
    ui.set_draw([&] { gui.draw(); });
    ui.set_display_size(1400.0F, 1000.0F);
    REQUIRE(ui.frames(3));
    REQUIRE(state.open_project(d / "demo.yantra").ok);
    const bm::Building b = state.project()->building();
    std::string floor;
    for (const bm::Floor& f : b.floors)
        if (b.has_scale(f) && !f.antennas.empty() && floor.empty()) floor = f.id;
    REQUIRE(state.project_window()->select(floor));
    REQUIRE(ui.frames(3));
    // UI-08: the status bar has the radio, recording, disk and power items.
    CHECK(gui.survey_overlay().status.radio == "Radio: not searched");
    CHECK(gui.survey_overlay().status.recording == "Monitoring (no file)");
    CHECK(gui.survey_overlay().status.disk.rfind("Free", 0) == 0);
    CHECK_FALSE(gui.survey_overlay().status.power.empty());

    // Another page first: the Survey tab is reached from any of them.
    REQUIRE(ui.click("panel.map"));
    REQUIRE(ui.frames(2));
    REQUIRE(ui.click("panel.survey"));
    REQUIRE(ui.frames(2));
    REQUIRE(ui.click("survey.find_radios"));
    REQUIRE(ui.frames(2));
    REQUIRE(ui.has("survey.radio_start"));
    REQUIRE(ui.find("rec.record")->disabled);  // no radio measuring yet
    REQUIRE(ui.click("survey.radio_start"));
    REQUIRE(ui.frames(2));
    REQUIRE_MESSAGE(state.live()->radio_running(), state.status());
    // The panels open by themselves; the serving cell and its large values come.
    const double deadline = 20.0 * YANTRA_TEST_TIME_SCALE;
    const auto until = [&](const std::function<bool()>& done) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(deadline);
        while (!done()) {
            if (std::chrono::steady_clock::now() > end) return false;
            if (!ui.frame()) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return true;
    };
    REQUIRE(until([&] { return gui.survey_overlay().panels && !gui.survey_overlay().serving.empty(); }));
    CHECK(gui.survey_overlay().tab == "cells");
    REQUIRE_FALSE(gui.survey_overlay().large.empty());
    CHECK(gui.survey_overlay().large.front().find("Level ") == 0);
    CHECK(gui.survey_overlay().status.radio.find("measuring") != std::string::npos);

    // Recording with a name and a marker (REC-01, REC-05); Exit is refused (REC-11).
    REQUIRE(ui.fill("rec.name", "Ui_walk"));
    REQUIRE(ui.frames(1));
    REQUIRE(ui.click("rec.record"));
    REQUIRE(ui.frames(2));
    REQUIRE_MESSAGE(state.live()->recording(), state.status());
    CHECK(gui.survey_overlay().status.recording_active);
    CHECK(gui.survey_overlay().status.recording.find("Ui_walk") != std::string::npos);
    CHECK_FALSE(state.action(Action::Exit).enabled);
    CHECK_FALSE(state.request_exit().ok);
    CHECK(ui.find("rec.voice")->disabled);  // the default copy has no sound device
    REQUIRE(ui.find("survey.radio_stop")->disabled);
    const std::uint64_t c0 = state.live()->cycles();
    REQUIRE(until([&] { return state.live()->cycles() >= c0 + 3; }));
    REQUIRE(ui.click("rec.marker"));
    REQUIRE(ui.frames(1));
    CHECK(state.status() == "Marker 1.");
    REQUIRE(until([&] { return state.live()->cycles() >= c0 + 5; }));

    // UI-04: the graphs; a click sets the cursor and the cells follow it.
    REQUIRE(ui.click("survey.graphs"));
    REQUIRE(until([&] { return gui.survey_overlay().graph_samples >= 3; }));
    CHECK(gui.survey_overlay().graph_traces >= 1);
    const auto plot = ui.find("graph.level_plot");
    REQUIRE(plot);
    ui.click_at(plot->x0 + (plot->x1 - plot->x0) * 0.98F, plot->cy());
    REQUIRE(ui.frames(2));
    CHECK(state.monitor()->cursor());
    CHECK(gui.survey_overlay().cursor);
    REQUIRE(ui.click("graph.live"));
    REQUIRE(ui.frames(1));
    CHECK_FALSE(state.monitor()->cursor());
    REQUIRE(ui.click("survey.events"));
    REQUIRE(ui.frames(2));
    CHECK(gui.survey_overlay().event_rows >= 2);  // radio started, recording started, marker
    REQUIRE(ui.click("survey.stats"));
    REQUIRE(ui.frames(2));
    CHECK(gui.survey_overlay().stats.find("% of the measurements") != std::string::npos);
    REQUIRE(ui.click("survey.scanner"));
    REQUIRE(ui.frames(2));
    CHECK(gui.survey_overlay().tab == "scanner");

    REQUIRE(ui.click("rec.stop"));
    REQUIRE(ui.frames(2));
    CHECK_FALSE(state.live()->recording());
    CHECK(state.action(Action::Exit).enabled);
    CHECK(fs::exists(d / "recordings" / "Ui_walk_0000.yrec"));

    // Replay (REC-07): choose, open (the radio stops), play to the end, cut (REC-08).
    REQUIRE(ui.click("replay.item.0"));
    REQUIRE(ui.frames(1));
    REQUIRE(ui.click("replay.open"));
    REQUIRE(ui.frames(2));
    REQUIRE_MESSAGE(state.replay()->is_open(), state.status());
    CHECK_FALSE(state.live()->radio_running());
    CHECK(gui.survey_overlay().status.radio == "Replay: Ui_walk");
    REQUIRE(ui.click("survey.graphs"));
    REQUIRE(ui.frames(2));
    CHECK(gui.survey_overlay().graph_samples >= 5);
    CHECK(gui.survey_overlay().cursor);
    const auto rplot = ui.find("graph.level_plot");
    REQUIRE(rplot);
    ui.click_at(rplot->cx(), rplot->cy());  // seek to the middle
    REQUIRE(state.replay()->wait([](auto, auto t) { return t > 0; }, static_cast<int>(deadline * 1000)));
    REQUIRE(ui.fill("replay.cut_name", "Ui_head"));
    REQUIRE(ui.frames(1));
    REQUIRE(ui.click("replay.cut_head"));
    REQUIRE(ui.frames(1));
    INFO(state.status());
    CHECK(fs::exists(d / "recordings" / "Ui_head_0000.yrec"));
    REQUIRE(ui.click("replay.play"));
    REQUIRE(ui.frames(1));
    REQUIRE(state.replay()->wait([](auto s, auto) { return s != yantra::recording::ReplayPlayer::State::Playing; },
                                 static_cast<int>(deadline * 4000)));
    REQUIRE(ui.click("replay.close"));
    REQUIRE(ui.frames(2));
    CHECK_FALSE(state.replay()->is_open());
}

TEST_CASE("DEMO-11 DEMO-14 the guided tour's mouse survey: Show me starts the simulator, move slowly, click to interpolate") {
    const fs::path sim = fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator.so";
    if (!fs::exists(sim)) return;
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "live_tour";
    fs::remove_all(d);
    fs::create_directories(d / "radios");
    fs::copy_file(sim, d / "radios" / "simulator.so");
    AppPaths paths;
    paths.data_folders = {YANTRA_TEST_SOURCE_DATA_DIR};
    paths.user_data = d / "user";
    paths.settings_file = d / "config" / "app.json";
    paths.radios.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
    paths.radios.radio_folders = {d / "radios"};
    paths.radios.calibration_table = d / "config" / "calibration.json";
    paths.interpolation_threads = 2;

    UiHarness ui;
    AppState state(AppSettings{}, paths);
    ImGui::SetCurrentContext(ui.context());
    Gui::apply_style();
    Gui gui(state);
    gui.set_wait_for_tiles(true);
    ui.set_draw([&] { gui.draw(); });
    ui.set_display_size(1400.0F, 1000.0F);
    REQUIRE(ui.frames(3));

    // The Fraunces Tavern up to the mouse survey, by Show me: a new project.
    REQUIRE(state.trigger(Action::Tour).ok);
    Tour& t = state.tour();
    REQUIRE(t.choose("fraunces").ok);
    while (t.view().step != TourStep::MouseSurvey) {
        CAPTURE(t.view().key);
        if (t.view().can_show) {
            const Result r = t.show_me();
            CAPTURE(r.message);
            REQUIRE(r.ok);
        }
        REQUIRE(t.next().ok);
        REQUIRE(ui.frames(2));
    }
    CHECK_FALSE(t.view().done);
    REQUIRE(ui.frames(2));
    CHECK(gui.tour_overlay().target == "panel.walk");
    const Result r = t.show_me();
    REQUIRE_MESSAGE(r.ok, r.message);
    REQUIRE(state.live()->running());
    REQUIRE(state.live()->chosen()->simulated);  // "Demo (simulated network)"
    // The practice calibration of the mouse survey works on dBFS levels:
    // the simulator's "Levels" setting dBFS (yantra-radios RAD-61; its
    // default is dBm since radio API 1.1). Step 13.7 reworks DEMO-14.
    REQUIRE(state.live()->device());
    REQUIRE(state.live()->device()->set_setting("level_unit", yantra::plugin::Value{yantra::plugin::EnumValue{1}}) == YR_OK);
    REQUIRE(ui.frames(3));
    CHECK(gui.tour_overlay().target == "walk.stop");
    CHECK(gui.tour_overlay().window);
    CHECK(gui.tour_overlay().banner);

    // DEMO-14: move slowly until points come, then click to interpolate.
    const std::string floor = state.project_window()->selected();
    const bm::Floor f = *state.project()->building().find_floor(floor);
    // Until enough points carry a level (the simulated cell is decoded
    // after its first frames).
    auto with_level = [&] {
        yantra::survey::PointQuery q;
        q.floor_id = floor;
        q.sessions = {state.live()->view().session};
        const auto list = yantra::survey::point_list(*state.project(), q);
        return std::count_if(list.points.begin(), list.points.end(),
                             [](const yantra::survey::SurveyPoint& p) { return p.value && p.metres; });
    };
    double u = 0.0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30 * YANTRA_TEST_TIME_SCALE);
    while ((state.live()->view().points < 5 || with_level() < 3) && std::chrono::steady_clock::now() < end) {
        u = std::fmod(u + 0.0005, 0.8);
        const double fx = 0.30 + (u < 0.4 ? u : 0.8 - u);
        const ImVec2 p = gui.canvas().screen_of({f.plan_width * fx, f.plan_height * 0.5});
        ui.mouse_move(p.x, p.y);
        REQUIRE(ui.frame());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(state.live()->view().points >= 5);
    REQUIRE(with_level() >= 3);
    CHECK(t.view().progress.find("points measured") != std::string::npos);
    // No calibration of the user's: the simulated practice calibration, said in the UI.
    const auto practice = state.live()->practice_calibration();
    REQUIRE(practice);
    CHECK(practice->simulated);
    CHECK(practice->method == yantra::calibration::kMethodPractice);
    CHECK(state.live()->view().calibration_note.find("simulated practice calibration") != std::string::npos);
    CHECK_FALSE(t.view().done);  // the step asks for the interpolation too
    const ImVec2 c = gui.canvas().screen_of({f.plan_width * 0.5, f.plan_height * 0.5});
    ui.click_at(c.x, c.y);
    state.interpolation()->wait();
    // The click produced the interpolation: the levels are in dBm.
    REQUIRE(ui.run_until([&] { return state.status().find("nterpolat") != std::string::npos; }, 30));
    REQUIRE_MESSAGE(state.status().rfind("Interpolated", 0) == 0, state.status());
    REQUIRE(ui.run_until([&] { return t.view().done; }, 10));
    const std::int64_t session = state.live()->view().session;
    const std::filesystem::path recordings = state.live()->recordings_folder();
    ui.key(ImGuiKey_Escape);
    REQUIRE(ui.frames(2));
    CHECK_FALSE(state.live()->running());
    CHECK_FALSE(state.live()->practice_calibration());  // only while the survey runs
    CHECK_FALSE(fs::exists(paths.radios.calibration_table));  // the user's table is not touched

    // The points: at the simulated positions on the cursor's path, flagged
    // simulated, in dBm, with the simulator's levels (its multi-wall model:
    // the ray tracer without reflections, TST-36).
    yantra::survey::PointQuery q;
    q.floor_id = floor;
    q.sessions = {session};
    const yantra::survey::PointList list = yantra::survey::point_list(*state.project(), q);
    REQUIRE(list.count() >= 5);
    CHECK(list.unit == "dBm");
    CHECK_FALSE(list.mixed_units);
    const bm::Building walked = state.project()->building();
    std::vector<bm::Point2> where;
    std::vector<double> levels;
    for (const yantra::survey::SurveyPoint& p : list.points) {
        CHECK(p.kind == "simulated");
        CHECK(p.plan.y == doctest::Approx(f.plan_height * 0.5).epsilon(0.02));
        CHECK(p.plan.x >= f.plan_width * 0.09);
        CHECK(p.plan.x <= f.plan_width * 0.91);
        if (p.value && p.metres) {
            where.push_back(*p.metres);
            levels.push_back(*p.value);
        }
    }
    REQUIRE(where.size() >= 3);
    namespace rt = yantra::raytrace;
    rt::ModelParams model;
    model.max_reflections = 0;
    const rt::PointLevels want = rt::evaluate(walked, rt::trace_points(walked, floor, where, model), model);
    std::size_t close = 0;
    for (std::size_t i = 0; i < where.size(); ++i)
        if (std::abs(levels[i] - want.best_dbm[i]) <= 3.0) ++close;
    CHECK(close * 10 >= where.size() * 8);

    // The report shows the same points (TST-34) and says the calibration is simulated.
    yantra::report::CoverageOptions ro;
    ro.data_folders = paths.data_folders;
    ro.created_utc_ns = 1793000000LL * 1000000000LL;
    const fs::path html = d / "report.html";
    const yantra::report::CoverageReport report = yantra::report::write_coverage_report(*state.project(), ro, d / "report.pdf", html);
    bool floor_found = false;
    for (const yantra::report::FloorSection& s : report.floors) {
        if (s.floor_id != floor) continue;
        floor_found = true;
        yantra::survey::PointQuery all;
        all.floor_id = floor;
        all.metric = s.points.metric;
        all.scale = s.points.scale;
        const yantra::survey::PointList window = yantra::survey::point_list(*state.project(), all);
        CHECK(s.points == window);
        CHECK(s.drawn_points == window.count());
        CHECK(window.count() >= list.count());
    }
    CHECK(floor_found);
    std::ifstream in(html);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("simulated (practice)") != std::string::npos);

    // The recording says the same, and its replay gives the same dBm cycles.
    std::vector<fs::path> files;
    for (const yantra::project::RecordingFile& rf : state.project()->recordings()) files.push_back(recordings / rf.file_name);
    REQUIRE_FALSE(files.empty());
    yantra::recording::ReplayOptions replay_options;
    replay_options.catalog = state.live()->catalog();
    std::string replay_error;
    auto replay = yantra::recording::Replay::open(files, replay_options, replay_error);
    REQUIRE_MESSAGE(replay, replay_error);
    REQUIRE_FALSE(replay->files().front().header.info.calibration.empty());
    CHECK(replay->files().front().header.info.calibration.front().simulated);
    CHECK(replay->files().front().header.info.calibration.front().method == yantra::calibration::kMethodPractice);
    (void)replay->advance_to(replay->duration_ns());
    CHECK(replay->mismatches() == 0);
    const auto last = replay->last_cycle();
    REQUIRE(last);
    bool calibrated = false;
    for (const auto& sample : last->samples) calibrated = calibrated || sample.latest.correction.calibrated;
    CHECK(calibrated);

    // The results, then another floor.
    REQUIRE(t.next().ok);
    REQUIRE(t.show_me().ok);
    REQUIRE(ui.frames(3));
    CHECK(gui.canvas().stats().points > 0);
    CHECK(gui.canvas().stats().interpolation);
    REQUIRE(t.next().ok);
    CHECK_FALSE(t.view().done);
    REQUIRE(t.show_me().ok);
    REQUIRE(ui.frames(2));
    CHECK(t.view().done);
    CHECK(state.project_window()->selected() != floor);
    t.leave();
    REQUIRE(state.close_project().ok);
    REQUIRE(ui.frames(2));
}
