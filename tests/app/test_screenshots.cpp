// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SCR-01..03: every scene of the screenshot set (app/gui/screenshot.h)
// reaches its capture in the headless harness, with the demo project and
// the simulated radios, as `yantra --shot NAME --screenshot FILE` does
// on a display (tools/screenshots; the launch tests in CMakeLists.txt
// render some of them with OpenGL). What each scene shows is checked
// through the app's state and the controls drawn.
#include <doctest/doctest.h>

#include <imgui.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <unistd.h>

#include "gui.h"
#include "plan_fixtures.h"
#include "screenshot.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/app/i18n.h"
#include "test_platform.hpp"

namespace fs = std::filesystem;
namespace fx = yantra::plan::fixtures;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

bool available() {
    return fs::exists(YANTRA_TEST_DEMO_PROJECT) && fs::exists(fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX);
}

/// Plays one scene to its capture; returns the harness's items of the
/// captured frame through `check`.
void play(const std::string& name, const std::function<void(UiHarness&, AppState&, Gui&)>& check,
          const fs::path& radios_extra = {}, Language language = Language::En, bool sheet_folder = false) {
    const ShotScene* scene = find_shot_scene(name);
    REQUIRE_MESSAGE(scene != nullptr, name);
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "screenshots" /
                       (name + "_" + language_key(language) + (sheet_folder ? "_folder" : ""));
    fs::remove_all(d);
    fs::create_directories(d / "radios");
    fs::copy_file(fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX, d / "radios" / "simulator" YANTRA_PLUGIN_SUFFIX);
    if (!radios_extra.empty()) fs::copy_file(radios_extra, d / "radios" / radios_extra.filename());
    AppPaths paths;
    // The demo project of the build tree (<build>/demo/fraunces), the
    // sources' data for the rest.
    paths.data_folders = {fs::path(YANTRA_TEST_DEMO_PROJECT).parent_path().parent_path().parent_path(),
                          YANTRA_TEST_SOURCE_DATA_DIR};
    paths.user_data = d / "data";
    paths.home = d / "home";
    fs::create_directories(paths.home);
    paths.radios.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
    paths.radios.radio_folders = {d / "radios"};
    paths.radios.plugin_state = d / "config" / "plugin_state.json";
    paths.radios.calibration_table = d / "config" / "calibration.json";
    paths.interpolation_threads = 2;
    AppSettings settings;
    settings.language = language_key(language);
    settings = shot_settings(*scene, settings);

    fs::path input;
    if (scene->needs_input) {
        // A small sheet with two floors side by side (the script uses the
        // two Fraunces sheets; decoding those is too slow for a test).
        // With sheet_folder: a folder with two small sheets under the
        // Fraunces names, which the scene combines (the script's way).
        const auto write = [](const fs::path& f, const fx::Bytes& png) {
            std::ofstream(f, std::ios::binary).write(reinterpret_cast<const char*>(png.data()),
                                                     static_cast<std::streamsize>(png.size()));
        };
        if (sheet_folder) {
            input = d / "sheets";
            fs::create_directories(input);
            write(input / "fraunces_tavern_first_floor.png", fx::png(fx::quadrants(800, 600)));
            write(input / "fraunces_tavern_second_floor.png", fx::png(fx::quadrants(780, 600)));
        } else {
            input = d / "two_floors.png";
            write(input, fx::png(fx::quadrants(800, 300)));
        }
    }
    {
        UiHarness ui;
        AppState state(settings, paths);
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style(Gui::style_of(settings));
        Gui gui(state);
        gui.set_wait_for_tiles(true);
        gui.set_screenshot_mode(true);
        ShotRunner runner(*scene, state, input);
        runner.set_wait_limit(ShotRunner::kWaitLimit * YANTRA_TEST_TIME_SCALE);
        ui.set_draw([&] { gui.draw(); });
        REQUIRE(ui.frames(1));
        int frames = 0;
        while (!runner.ready() && !runner.failed()) {
            runner.before_frame(ui.items());
            if (runner.failed()) break;
            if (runner.waiting()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            REQUIRE_MESSAGE(ui.frame(), ui.last_error());
            REQUIRE(++frames < 200000);
        }
        REQUIRE_MESSAGE(!runner.failed(), name << ": " << runner.error() << "\n" << ui.dump());
        check(ui, state, gui);
    }
    set_language(Language::En);
}

}  // namespace

TEST_CASE("SCR-03: the screenshot scenes without a radio reach their capture") {
    if (!available()) return;
    CHECK(shot_scenes().size() == 29);  // step 11.2: remote_probes; 15.3: about_building; 15.4: plan_note
    play("start_menu", [](UiHarness& ui, AppState&, Gui&) { CHECK(ui.has("file.new_project")); });
    play("about_building", [](UiHarness& ui, AppState& s, Gui& g) {
        CHECK(s.about_tab());
        CHECK(ui.has("about.address"));
        CHECK(g.building_overlay().attribution);
        REQUIRE(s.about_building());
        CHECK(s.about_building()->stored().surveyor == "Demo surveyor");
    });
    play("plan_note", [](UiHarness& ui, AppState& s, Gui& g) {
        CHECK(s.dialog() == Dialog::Note);
        CHECK(ui.has("note.text"));
        CHECK(g.notes_overlay().window);
        CHECK(g.notes_overlay().pins >= 1);
    });
    play("about", [](UiHarness&, AppState& s, Gui& g) {
        CHECK(s.dialog() == Dialog::About);
        CHECK(g.about_overlay().curtat_logo);
    });
    play("reset_settings", [](UiHarness&, AppState& s, Gui&) { CHECK(s.dialog() == Dialog::ResetSettings); });
    play("settings_survey", [](UiHarness& ui, AppState&, Gui&) { CHECK(ui.has("settings.threshold", "dlg.settings")); });
    play("plan_import", [](UiHarness&, AppState& s, Gui&) {
        REQUIRE(s.plan_import() != nullptr);
        CHECK(s.plan_import()->placed().size() == 2);  // two floors from one sheet
    });
    // The folder of the two sheets: combined into one sheet (a PGM) first.
    play(
        "plan_import",
        [](UiHarness&, AppState& s, Gui&) {
            REQUIRE(s.plan_import() != nullptr);
            CHECK(s.plan_import()->placed().size() == 2);
            // Side by side at a quarter: 400 x 150 grey pixels and the header.
            CHECK(fs::file_size(s.paths().user_data / "screenshots" / "fraunces_both_floors.pgm") > 400u * 150u);
        },
        {}, Language::En, true);
    for (const char* name : {"survey", "survey_graphs", "survey_events"})
        play(name, [name](UiHarness&, AppState& s, Gui& g) {
            CHECK(s.replay()->view().open);
            CHECK(g.survey_overlay().live_values);  // the Walk tab (UI-18)
            CHECK_FALSE(g.survey_overlay().serving.empty());
            if (std::string(name) == "survey") CHECK(g.survey_overlay().shown("neighbours"));
            if (std::string(name) == "survey_graphs") CHECK(g.survey_overlay().shown("graphs"));
            if (std::string(name) == "survey_events") CHECK(g.survey_overlay().shown("events"));
        });
    // The translated survey view of the manuals (SCR-03).
    play("survey", [](UiHarness& ui, AppState&, Gui&) { CHECK(ui.has("panel.walk")); }, {}, Language::De);
    play("walk_test", [](UiHarness& ui, AppState& s, Gui&) {
        CHECK_FALSE(s.settings().layers.visible(Layer::Interpolation));
        CHECK(ui.has("panel.walk"));
    });
    play("touch", [](UiHarness& ui, AppState& s, Gui&) {
        CHECK(s.settings().touch);
        const auto b = ui.find("panel.walk");
        REQUIRE(b);
        CHECK(b->y1 - b->y0 >= static_cast<float>(kTouchTargetPx) - 0.5F);
    });
    play("interpolation", [](UiHarness& ui, AppState& s, Gui& g) {
        CHECK(s.screen() == Screen::Project);
        CHECK(ui.has("map.opacity"));  // the Map tab (UI-19)
        CHECK(g.survey_overlay().advanced_open);
    });
    play("cell_database", [](UiHarness&, AppState& s, Gui&) { CHECK(s.network_windows().cell_db); });
    play("export", [](UiHarness& ui, AppState& s, Gui&) {
        CHECK(s.dialog() == Dialog::Export);
        CHECK(ui.has("ex.col.time", "dlg.export"));
    });
    play("report", [](UiHarness& ui, AppState& s, Gui&) {
        CHECK(s.dialog() == Dialog::Report);
        CHECK(ui.has("rp.surveyor", "dlg.report"));
        CHECK(ui.has("rp.profile.0", "dlg.report"));
    });
    play("verification", [](UiHarness&, AppState& s, Gui&) { CHECK(s.network_windows().verification); });
    // The probe plugin's window (absent in a build without it, PLG-01 check).
    if (fs::exists(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "probe" YANTRA_PLUGIN_SUFFIX))
        play("remote_probes", [](UiHarness&, AppState& s, Gui&) {
            CHECK(s.plugin_ui().is_open("yantra.probe", "remote_probes"));
        });
    // The demo cells carry access parameters: the simulator decides (DB-08).
    play("reselection", [](UiHarness&, AppState& s, Gui&) {
        CHECK(s.network_windows().reselection);
        REQUIRE(s.network_checks() != nullptr);
        const ReselectionView v = s.network_checks()->reselection();
        CHECK_MESSAGE(v.ok, v.error);
        CHECK(v.rows.size() == 2);
    });
}

TEST_CASE("SCR-03: the simulator's scenes: scanner, radio list and generated radio windows (PLG-16)") {
    if (!available()) return;
    play("survey_scanner", [](UiHarness&, AppState&, Gui& g) {
        CHECK(g.survey_overlay().shown("scanner"));
        CHECK(g.survey_overlay().scan_rows > 0);
    });
    play("radio_list", [](UiHarness&, AppState& s, Gui&) { CHECK(s.radio_windows()->is_open(RadioWindow::List)); });
    const std::pair<const char*, RadioWindow> windows[] = {{"settings", RadioWindow::Settings},
                                                           {"debug", RadioWindow::Debug},
                                                           {"calibration", RadioWindow::Calibration},
                                                           {"info", RadioWindow::Info}};
    for (const auto& [name, w] : windows)
        play(std::string("radio_simulator_") + name,
             [w = w](UiHarness&, AppState& s, Gui&) { CHECK(s.radio_windows()->is_open(w)); });
}

TEST_CASE("SCR-03: the HackRF plugin's generated radio windows on the fake SoapySDR device (PLG-16)") {
    const fs::path fake = fs::path(YANTRA_TEST_RADIO_TEST_PLUGIN_DIR) / "hackrf_fake" YANTRA_PLUGIN_SUFFIX;
    if (!available() || !fs::exists(fake) || std::string(YANTRA_TEST_FAKE_SDR_SCENARIO).empty()) return;
    const fs::path control = fs::path(YANTRA_TEST_FAKE_SDR_CONTROL_DIR) / (std::to_string(getpid()) + "_shots");
    fs::remove_all(control);
    fs::create_directories(control);
    // Unpaced: the device delivers the signal as fast as the receiver takes
    // it, so the debug window gets records under a sanitizer too (the
    // script uses the paced device on a normal build).
    const std::string config = R"({"control":")" + control.generic_string() +
                               R"(","devices":[{"serial":"0000000000000000a1b2c3d4e5f60009","scenario":")" +
                               YANTRA_TEST_FAKE_SDR_SCENARIO + R"(","paced":false}]})";
    REQUIRE(yantra::test::set_env("YANTRA_FAKE_SOAPY", config.c_str()) == 0);
    for (const char* name : {"settings", "debug", "calibration", "info"})
        play(std::string("radio_hackrf_") + name,
             [](UiHarness&, AppState& s, Gui& g) {
                 const InfoView v = s.radio_windows()->info_view();
                 CHECK(v.plugin.id.find("hackrf") != std::string::npos);
                 // No folder of this machine in the pictures (NFR-09).
                 CHECK(g.screenshot_mode());
                 CHECK(Gui::shown_plugin_file(v.plugin.path, true) == std::string("<plugins>/hackrf_fake") + YANTRA_PLUGIN_SUFFIX);
                 CHECK(Gui::shown_plugin_file(v.plugin.path, false) == v.plugin.path.string());
             },
             fake);
    yantra::test::unset_env("YANTRA_FAKE_SOAPY");
    fs::remove_all(control);
}
