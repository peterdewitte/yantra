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
#include "gui_widgets.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/project/project.h"
#include "yantra/core/radio/radio_host.h"
#include "yantra/core/recording/replay.h"
#include "yantra/core/report/coverage.h"
#include "yantra/core/survey/points.h"

namespace fs = std::filesystem;
namespace bm = yantra::building;
using namespace yantra::app;
using yantra::test::UiHarness;

TEST_CASE("DEMO-14 in the app: the mouse survey with the simulator, move slowly, click to interpolate, Esc stops") {
    const fs::path sim = fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(sim) || !fs::exists(YANTRA_TEST_DEMO_PROJECT)) return;
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "live_ui";
    fs::remove_all(d);
    fs::create_directories(d / "radios");
    fs::copy_file(sim, d / "radios" / "simulator" YANTRA_PLUGIN_SUFFIX);
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, d / "demo.yantra");
    AppPaths paths;
    paths.radios.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
    paths.radios.radio_folders = {d / "radios"};
    // DEMO-05: the Fraunces floors' fixed demo scenes, as the app has them.
    paths.radios.scene_folders = {fs::path(YANTRA_TEST_SOURCE_DATA_DIR) / "demo" / "scenes"};
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
    // The first floor with a scale and a fixed demo scene.
    const bm::Building b = state.project()->building();
    std::string floor;
    for (const bm::Floor& f : b.floors)
        if (b.has_scale(f) && !f.example_id.empty() && floor.empty()) floor = f.id;
    REQUIRE_FALSE(floor.empty());
    REQUIRE(state.project_window()->select(floor));
    REQUIRE(ui.frames(3));
    const bm::Floor f = *b.find_floor(floor);

    REQUIRE(ui.click("panel.walk"));
    REQUIRE(ui.frames(2));
    // UI-21: Select... opens Radio > Radios... and searches the radios.
    REQUIRE(ui.click("walk.radio_select"));
    REQUIRE(ui.frames(2));
    REQUIRE_MESSAGE(state.live()->chosen(), state.live()->radio_note());
    CHECK(state.live()->chosen()->simulated);
    state.radio_windows()->close(RadioWindow::List);
    REQUIRE(ui.frames(2));
    REQUIRE(ui.click("walk.kind.mouse_survey"));  // the kind as options (UI-21)
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
    {
        // DEMO-15: the hint is a demo explanation: green, readable text.
        namespace gd = yantra::app::gui_detail;
        gd::DemoRecord demo;
        gd::demo_record() = &demo;
        REQUIRE(ui.frame());
        gd::demo_record() = nullptr;
        bool hint = false;
        for (const gd::DemoRecord::Box& box : demo.boxes)
            if (box.what == "mouse survey hint") {
                hint = true;
                CHECK(box.bg.y > box.bg.x);
                CHECK(box.bg.y > box.bg.z);
                for (const ImVec4& t : box.texts) CHECK(gd::contrast_ratio(t, box.bg) >= 4.5);
            }
        CHECK(hint);
    }
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
    const fs::path sim = fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(sim) || !fs::exists(YANTRA_TEST_DEMO_PROJECT)) return;
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "survey_ui";
    fs::remove_all(d);
    fs::create_directories(d / "radios");
    fs::copy_file(sim, d / "radios" / "simulator" YANTRA_PLUGIN_SUFFIX);
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
    ui.set_display_size(1400.0F, 1800.0F);  // the Walk tab with recording and replay in view
    REQUIRE(ui.frames(3));
    REQUIRE(state.open_project(d / "demo.yantra").ok);
    const bm::Building b = state.project()->building();
    std::string floor;
    for (const bm::Floor& f : b.floors)
        if (b.has_scale(f) && !f.example_id.empty() && floor.empty()) floor = f.id;
    REQUIRE(state.project_window()->select(floor));
    REQUIRE(ui.frames(3));
    // UI-08: the status bar has the radio, recording, disk and power items.
    CHECK(gui.survey_overlay().status.radio == "Radio: not searched");
    CHECK(gui.survey_overlay().status.recording == "Monitoring (no file)");
    CHECK(gui.survey_overlay().status.disk.rfind("Free", 0) == 0);
    CHECK_FALSE(gui.survey_overlay().status.power.empty());

    // UI-18: the Map tab first; the Walk tab has the radio, the live values
    // and the recording.
    REQUIRE(ui.click("panel.map"));
    REQUIRE(ui.frames(2));
    CHECK_FALSE(ui.has("walk.radio_select"));
    REQUIRE(ui.click("panel.walk"));
    REQUIRE(ui.frames(2));
    REQUIRE(ui.click("walk.radio_select"));
    REQUIRE(ui.frames(2));
    state.radio_windows()->close(RadioWindow::List);
    REQUIRE(ui.frames(2));
    // UI-21: the recording controls are closed until their arrow opens them.
    CHECK_FALSE(ui.has("rec.record"));
    REQUIRE(ui.click("walk.recording"));
    REQUIRE(ui.frames(2));
    REQUIRE(ui.has("walk.radio_start"));
    REQUIRE(ui.find("rec.record")->disabled);  // no radio measuring yet
    REQUIRE(ui.click("walk.radio_start"));
    REQUIRE(ui.frames(2));
    REQUIRE_MESSAGE(state.live()->radio_running(), state.status());
    // The live values of the Walk tab: the serving cell, its level large and the compact graph.
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
    REQUIRE(until([&] { return gui.survey_overlay().live_values && !gui.survey_overlay().serving.empty(); }));
    CHECK(gui.survey_overlay().compact_graph);
    CHECK(ui.has("walk.level_plot"));
    CHECK(gui.survey_overlay().windows.empty());  // the survey windows open from the Window menu only
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
    REQUIRE(ui.find("walk.radio_stop")->disabled);
    const std::uint64_t c0 = state.live()->cycles();
    REQUIRE(until([&] { return state.live()->cycles() >= c0 + 3; }));
    REQUIRE(ui.click("rec.marker"));
    REQUIRE(ui.frames(1));
    CHECK(state.status() == "Marker 1.");
    REQUIRE(until([&] { return state.live()->cycles() >= c0 + 5; }));

    // UI-04: Window > Graphs; a click sets the cursor and the cells follow it.
    REQUIRE(ui.click("menu.window"));
    REQUIRE(ui.frames(2));
    REQUIRE(ui.click("window.graphs"));
    REQUIRE(until([&] { return gui.survey_overlay().graph_samples >= 3; }));
    CHECK(gui.survey_overlay().shown("graphs"));
    CHECK(state.action(Action::WindowGraphs).checked);
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
    REQUIRE(state.trigger(Action::WindowEvents).ok);
    REQUIRE(ui.frames(2));
    CHECK(gui.survey_overlay().event_rows >= 2);  // radio started, recording started, marker
    REQUIRE(state.trigger(Action::WindowStatistics).ok);
    REQUIRE(ui.frames(2));
    CHECK(gui.survey_overlay().stats.find("% of the measurements") != std::string::npos);
    REQUIRE(state.trigger(Action::WindowScanner).ok);
    REQUIRE(state.trigger(Action::WindowNeighbours).ok);
    REQUIRE(ui.frames(2));
    for (const char* w : {"neighbours", "graphs", "scanner", "events", "statistics"}) CHECK(gui.survey_overlay().shown(w));
    // The menu entry hides its window again.
    REQUIRE(state.trigger(Action::WindowNeighbours).ok);
    REQUIRE(ui.frames(2));
    CHECK_FALSE(gui.survey_overlay().shown("neighbours"));
    // Only the graphs stay open (the other windows would cover the plot).
    for (Action a : {Action::WindowScanner, Action::WindowEvents, Action::WindowStatistics}) REQUIRE(state.trigger(a).ok);
    REQUIRE(ui.frames(2));
    CHECK(gui.survey_overlay().windows == std::vector<std::string>{"graphs"});

    REQUIRE(ui.click("rec.stop"));
    REQUIRE(ui.frames(2));
    CHECK_FALSE(state.live()->recording());
    CHECK(state.action(Action::Exit).enabled);
    CHECK(fs::exists(d / "recordings" / "Ui_walk_0000.yrec"));

    // Replay (REC-07): choose, open (the radio stops), play to the end, cut (REC-08).
    REQUIRE(ui.click("walk.replay"));
    REQUIRE(ui.frames(2));
    REQUIRE(ui.click("replay.item.0"));
    REQUIRE(ui.frames(1));
    REQUIRE(ui.click("replay.open"));
    REQUIRE(ui.frames(2));
    REQUIRE_MESSAGE(state.replay()->is_open(), state.status());
    CHECK_FALSE(state.live()->radio_running());
    CHECK(gui.survey_overlay().status.radio == "Replay: Ui_walk");
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
    const fs::path sim = fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(sim)) return;
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "live_tour";
    fs::remove_all(d);
    fs::create_directories(d / "radios");
    fs::copy_file(sim, d / "radios" / "simulator" YANTRA_PLUGIN_SUFFIX);
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
    CHECK(gui.tour_overlay().target == "walk.start");  // the Walk tab is shown first (UI-18)
    const Result r = t.show_me();
    REQUIRE_MESSAGE(r.ok, r.message);
    REQUIRE(state.live()->running());
    REQUIRE(state.live()->chosen()->simulated);  // "Demo (simulated network)"
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
    // The simulator reports dBm (radio API 1.1 LEVEL_DBM, its default): no
    // calibration is made or needed (DEMO-14), said in the UI.
    CHECK(state.live()->view().calibration_note.find("Levels: dBm") == 0);
    CHECK((state.live()->calibration_table() == nullptr || state.live()->calibration_table()->empty()));
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
    CHECK_FALSE(fs::exists(paths.radios.calibration_table));  // the user's table is not touched

    // The points: at the simulated positions on the cursor's path, flagged
    // simulated, in dBm, with the simulator's levels.
    yantra::survey::PointQuery q;
    q.floor_id = floor;
    q.sessions = {session};
    const yantra::survey::PointList list = yantra::survey::point_list(*state.project(), q);
    REQUIRE(list.count() >= 5);
    CHECK(list.unit == "dBm");
    CHECK_FALSE(list.mixed_units);
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
    // The fixed demo scene's indoor levels (DEMO-05).
    for (const double v : levels) {
        CHECK(v > -110.0);
        CHECK(v < -20.0);
    }

    // The report shows the same points (TST-34) with no calibration recorded.
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
    CHECK(text.find("simulated (practice)") == std::string::npos);
    CHECK(text.find("none recorded (levels as the radio gives them)") != std::string::npos);
    CHECK(text.find("Some levels are uncalibrated") == std::string::npos);

    // The recording says the same, and its replay gives the same dBm cycles.
    std::vector<fs::path> files;
    for (const yantra::project::RecordingFile& rf : state.project()->recordings()) files.push_back(recordings / rf.file_name);
    REQUIRE_FALSE(files.empty());
    yantra::recording::ReplayOptions replay_options;
    replay_options.catalog = state.live()->catalog();
    std::string replay_error;
    auto replay = yantra::recording::Replay::open(files, replay_options, replay_error);
    REQUIRE_MESSAGE(replay, replay_error);
    CHECK(replay->files().front().header.info.calibration.empty());
    (void)replay->advance_to(replay->duration_ns());
    std::string mismatches;
    for (const std::string& m : replay->mismatch_messages()) mismatches += m + "\n";
    CHECK_MESSAGE(replay->mismatches() == 0, mismatches);
    const auto last = replay->last_cycle();
    REQUIRE(last);
    REQUIRE_FALSE(last->samples.empty());
    for (const auto& sample : last->samples) {
        CHECK_FALSE(sample.latest.correction.calibrated);  // dBm from the radio: no entry applied
        CHECK_FALSE(sample.latest.radio_dbfs);
    }

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
