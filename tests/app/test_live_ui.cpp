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

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <thread>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/project/project.h"

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
