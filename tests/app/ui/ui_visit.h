// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The walk through every window, page, menu and dialog of the app in the
// headless harness, shared by the UI lint (DOC-20, test_ui_lint.cpp) and
// the GUI text-overflow test (TST-52, tests/app/overflow): visit_all()
// opens each one in turn and calls `check` once it has settled. New
// dialogs and pages go in here, so both tests see them.
// Needs YANTRA_TEST_SCRATCH_DIR, YANTRA_TEST_SOURCE_DATA_DIR,
// YANTRA_TEST_TECH_PLUGIN_DIR, YANTRA_TEST_EXAMPLE_PLUGIN_DIR (and
// YANTRA_TEST_DEMO_DATA_DIR, YANTRA_TEST_HAVE_PROBE when there).
#pragma once

#include <doctest/doctest.h>

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/app/audio.h"
#include "yantra/app/i18n.h"
#include "yantra/app/settings.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/project/project.h"

namespace yantra::test::visit {

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::test::UiHarness;

inline fs::path scratch(const std::string& name, const std::string& root = "lint") {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / root / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

inline AppPaths lint_paths(const fs::path& dir) {
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.data_folders = {YANTRA_TEST_SOURCE_DATA_DIR};
#ifdef YANTRA_TEST_DEMO_DATA_DIR
    p.data_folders.insert(p.data_folders.begin(), fs::path(YANTRA_TEST_DEMO_DATA_DIR));
#endif
    p.user_data = dir / "data";
    p.sheet_cache = dir / "cache";  // step 9.10: the tour asks before a download (none here)
    p.home = dir / "home";
    fs::create_directories(p.home);
    const fs::path folder = dir / "plugins";
    fs::create_directories(folder);
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "tetra" YANTRA_PLUGIN_SUFFIX, folder / "tetra" YANTRA_PLUGIN_SUFFIX,
                  fs::copy_options::overwrite_existing);
#ifdef YANTRA_TEST_HAVE_PROBE
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "probe" YANTRA_PLUGIN_SUFFIX, folder / "probe" YANTRA_PLUGIN_SUFFIX,
                  fs::copy_options::overwrite_existing);
#endif
    fs::copy_file(fs::path(YANTRA_TEST_EXAMPLE_PLUGIN_DIR) / "example_beacon" YANTRA_PLUGIN_SUFFIX, folder / "example_beacon" YANTRA_PLUGIN_SUFFIX,
                  fs::copy_options::overwrite_existing);
    p.radios.plugin_folders = {folder};
    p.radios.plugin_state = dir / "config" / "plugin_state.json";
    return p;
}

struct Shell {
    UiHarness ui;
    AppState state;
    Gui gui{state};
    Shell(AppPaths paths, AppSettings settings) : state(std::move(settings), std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style(Gui::style_of(state.settings()));
        ui.set_draw([this] { gui.draw(); });
        REQUIRE(ui.frames(3));
    }
    ~Shell() { set_language(Language::En); }
};

// Every window and page of the app, each checked after it has settled.
inline void visit_all(Shell& s, const std::function<void(const std::string&)>& check, const std::string& tag) {
    auto settle_check = [&](const std::string& where) {
        REQUIRE(s.ui.frames(3));
        check(tag + " " + where);
    };
    settle_check("start screen");
    // Every menu of the menu bar, open.
    for (const char* menu : {"file", "network", "help"}) {
        REQUIRE(s.ui.click(std::string("menu.") + menu));
        settle_check(std::string("menu ") + menu);
        s.ui.key(ImGuiKey_Escape);
        REQUIRE(s.ui.frames(2));
    }
    auto dialog = [&](Action a, const char* where) {
        REQUIRE(s.state.trigger(a).ok);
        settle_check(where);
        s.state.cancel_dialog();
        REQUIRE(s.ui.frames(2));
    };
    dialog(Action::NewProject, "new project");
    dialog(Action::OpenProject, "open project");
    dialog(Action::About, "about");
    dialog(Action::ResetSettings, "reset settings");
    REQUIRE(s.state.trigger(Action::Settings).ok);
    for (const char* tab : {"general", "survey", "sound", "plugins"}) {
        REQUIRE(s.ui.frames(2));
        REQUIRE_MESSAGE(s.ui.click(std::string("settings.tab.") + tab, "dlg.settings"), s.ui.dump());
        settle_check(std::string("settings ") + tab);
    }
    // The help over a modal dialog.
    REQUIRE(s.state.open_help("15.1").ok);
    settle_check("help over settings");
    s.state.close_help();
    s.state.cancel_dialog();
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.state.open_help("").ok);
    settle_check("help contents");
    REQUIRE(s.state.open_help("13.2").ok);
    settle_check("help section");
    // Help > Quick starts (DOC-03): the list and one quick start.
    REQUIRE(s.state.open_quick_start("").ok);
    settle_check("quick starts");
    REQUIRE(s.state.open_quick_start("1").ok);
    settle_check("quick start");
    s.state.close_help();

    // Step 9.10: the guided tour: its window, the download question, its
    // strip inside a dialog and over the project.
    REQUIRE(s.state.trigger(Action::Tour).ok);
    settle_check("tour choose");
    REQUIRE(s.state.tour().choose("mak_beer_house").ok);
    REQUIRE(s.state.dialog() == Dialog::TourConsent);
    settle_check("tour consent");
    s.state.cancel_dialog();  // no: the Fraunces Tavern, with the message
    settle_check("tour fallback");
    REQUIRE(s.state.tour().show_me().ok);  // the tour's project
    REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
    settle_check("tour project");
    REQUIRE(s.state.tour().next().ok);
    // The strip is the same in every dialog (the plan import window with
    // it is in test_ui_tour.cpp; decoding the large sheet here 8 times made
    // the sanitizer run too slow).
    REQUIRE(s.state.trigger(Action::AddFloor).ok);
    settle_check("tour strip in add floor");
    s.state.tour().leave();
    s.state.cancel_dialog();
    REQUIRE(s.state.close_project().ok);
    REQUIRE(s.ui.frames(2));

#ifdef YANTRA_TEST_DEMO_DATA_DIR
    REQUIRE(s.state.open_demo().ok);
    REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
    for (const char* page : {"walk", "map"}) {
        REQUIRE(s.ui.frames(2));
        REQUIRE_MESSAGE(s.ui.click(std::string("panel.") + page), s.ui.dump());
        settle_check(std::string("project ") + page);
    }
    // UI-19: the Map tab's Advanced dropdown, open.
    REQUIRE_MESSAGE(s.ui.click("map.advanced"), s.ui.dump());
    settle_check("project map advanced");
    REQUIRE(s.ui.click("map.advanced"));
    REQUIRE(s.ui.frames(2));
    // UI-18: the Walk tab's replay part, open.
    REQUIRE(s.ui.click("panel.walk"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.click("walk.replay"), s.ui.dump());
    settle_check("project walk replay");
    REQUIRE(s.ui.click("walk.replay"));
    REQUIRE(s.ui.frames(2));
    // UI-18: the survey windows of the Window menu, all open.
    for (Action a : {Action::WindowNeighbours, Action::WindowGraphs, Action::WindowScanner, Action::WindowEvents,
                     Action::WindowStatistics})
        REQUIRE(s.state.trigger(a).ok);
    settle_check("survey windows");
    s.state.survey_windows() = SurveyWindows{};
    REQUIRE(s.ui.frames(2));
    for (const char* menu : {"file", "radio", "tools", "network", "window"}) {
        if (!s.ui.has(std::string("menu.") + menu)) continue;
        REQUIRE(s.ui.click(std::string("menu.") + menu));
        settle_check(std::string("project menu ") + menu);
        s.ui.key(ImGuiKey_Escape);
        REQUIRE(s.ui.frames(2));
    }
    const std::string floor = s.state.project_window()->selected();
    auto floor_dialog = [&](const std::function<Result()>& open, const char* where) {
        REQUIRE(open().ok);
        settle_check(where);
        s.state.cancel_dialog();
        REQUIRE(s.ui.frames(3));
    };
    floor_dialog([&] { return s.state.trigger(Action::AddFloor); }, "add floor");
    floor_dialog([&] { return s.state.edit_floor(floor); }, "floor properties");
    floor_dialog([&] { return s.state.request_remove_floor(floor); }, "remove floor");
    floor_dialog([&] { return s.state.edit_floor_plan(floor); }, "plan import");
    floor_dialog([&] { return s.state.edit_interpolation_settings(); }, "interpolation settings");
    floor_dialog([&] { return s.state.trigger(Action::SaveAs); }, "save as");
    // Step 10.4: the export (each kind), report, profile and sharing dialogs.
    REQUIRE(s.state.trigger(Action::Export).ok);
    for (const ExportForm::Kind k : {ExportForm::Kind::Csv, ExportForm::Kind::Dxf, ExportForm::Kind::Image,
                                     ExportForm::Kind::Batch}) {
        s.state.output()->export_form().kind = k;
        settle_check("export " + std::to_string(static_cast<int>(k)));
    }
    s.state.cancel_dialog();
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.state.trigger(Action::Report).ok);
    s.state.output()->report_form().choose_profiles = true;
    settle_check("report");
    REQUIRE(s.state.open_profiles().ok);
    s.state.output()->add_profile();
    s.state.output()->profile_form().profiles.back().basis = yantra::survey::AcceptanceProfile::Basis::TestedArea;
    settle_check("acceptance profiles");
    s.state.cancel_dialog();
    REQUIRE(s.ui.frames(2));
    for (const SharingSettings::Method m : {SharingSettings::Method::Folder, SharingSettings::Method::Http,
                                            SharingSettings::Method::Smtp}) {
        REQUIRE(s.state.open_sharing().ok);
        s.state.sharing_form().method = m;
        settle_check("sharing " + std::to_string(static_cast<int>(m)));
    }
    s.state.cancel_dialog();
    s.state.cancel_dialog();
    REQUIRE(s.ui.frames(3));
    for (Action a : {Action::CellDatabase, Action::Verification, Action::Reselection}) REQUIRE(s.state.trigger(a).ok);
    settle_check("network windows");
    s.state.network_windows() = NetworkWindows{};
    if (RadioWindows* rw = s.state.radio_windows()) {
        REQUIRE(rw->open(RadioWindow::List).ok);
        settle_check("radio list");
        rw->close(RadioWindow::List);
    }
    // The plugins' own windows (PLG-06): the TETRA calculator, the beacon
    // and the probe's remote probes (PLG-24).
    for (const auto& [plugin, item] : {std::pair<const char*, const char*>{"yantra.tetra", "calculator"},
                                       {"yantra.example.beacon", "beacon_channels"},
                                       {"yantra.probe", "remote_probes_open"}})
        (void)s.state.plugin_ui().select(plugin, item);
    settle_check("plugin windows");
    // A level alert's popup (UI-10).
    SoundSettings snd = s.state.settings().sound;
    snd.alerts = {{"Site", -90.0}};
    s.state.alerts().set_settings(snd);
    FakeAudio audio;
    SurveyFrame f;
    f.valid = true;
    CellRow r;
    r.name = "Site";
    r.level = -80.0;
    f.serving = r;
    s.state.alerts().update({}, f, false, 0.0, audio);
    f.serving->level = -95.0;
    s.state.alerts().update({}, f, false, 1.0, audio);
    REQUIRE(!s.state.alerts().popups().empty());
    settle_check("level alert");
    s.state.alerts().dismiss_popups();
#endif
}


}  // namespace yantra::test::visit
