// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI lint at run time (DOC-20, DOC-07, TST-50, UI-12, UI-13): every
// window of the app, drawn in every UI language, in the desktop layout
// and in the touch layout: each control has a tooltip, each disabled one
// its reason, nothing overlaps, is cut off or squashed, touch targets are
// at least 48 px, and every text has its translation (tests/app/ui/ui_lint.h).
#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <string>

#include "gui.h"
#include "ui_harness.h"
#include "ui_lint.h"
#include "yantra/app/app_state.h"
#include "yantra/app/i18n.h"
#include "yantra/app/settings.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::test::UiHarness;
using yantra::test::UiLint;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "lint" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

AppPaths lint_paths(const fs::path& dir) {
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
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "tetra.so", folder / "tetra.so",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(fs::path(YANTRA_TEST_EXAMPLE_PLUGIN_DIR) / "example_beacon.so", folder / "example_beacon.so",
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
void visit_all(Shell& s, UiLint& lint, const std::string& tag) {
    auto settle_check = [&](const std::string& where) {
        REQUIRE(s.ui.frames(3));
        lint.check(tag + " " + where);
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
    REQUIRE(s.state.trigger(Action::AddFloor).ok);
    settle_check("tour strip in add floor");
    REQUIRE(s.state.tour().show_me().ok);  // the plan import window on the sheet
    settle_check("tour strip in plan import");
    s.state.tour().leave();
    s.state.cancel_dialog();
    REQUIRE(s.state.close_project().ok);
    REQUIRE(s.ui.frames(2));

#ifdef YANTRA_TEST_DEMO_DATA_DIR
    REQUIRE(s.state.open_demo().ok);
    REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
    for (const char* page : {"layers", "edit", "walk", "map", "survey", "post"}) {
        REQUIRE(s.ui.frames(2));
        REQUIRE_MESSAGE(s.ui.click(std::string("panel.") + page), s.ui.dump());
        settle_check(std::string("project ") + page);
    }
    for (const char* menu : {"file", "radio", "tools", "network"}) {
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
    floor_dialog([&] { return s.state.edit_materials(); }, "wall classes");
    floor_dialog([&] { return s.state.edit_propagation(); }, "propagation");
    floor_dialog([&] { return s.state.trigger(Action::SaveAs); }, "save as");
    for (Action a : {Action::CellDatabase, Action::Verification, Action::Reselection}) REQUIRE(s.state.trigger(a).ok);
    settle_check("network windows");
    s.state.network_windows() = NetworkWindows{};
    if (RadioWindows* rw = s.state.radio_windows()) {
        REQUIRE(rw->open(RadioWindow::List).ok);
        settle_check("radio list");
        rw->close(RadioWindow::List);
    }
    // The plugins' own windows (PLG-06): the TETRA calculator and the beacon.
    for (const auto& [plugin, item] : {std::pair<const char*, const char*>{"yantra.tetra", "calculator"},
                                       {"yantra.example.beacon", "beacon_channels"}})
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

void run_lint(bool touch, const char* tag) {
    std::string all;
    int frames = 0;
    for (Language l : kLanguages) {
        AppSettings settings;
        settings.language = language_key(l);
        settings.touch = touch;
        Shell s(lint_paths(scratch(std::string(tag) + "_" + language_key(l))), settings);
        if (touch) s.ui.set_display_size(1280.0F, 800.0F);  // a 10" tablet
        UiLint lint{s.ui, touch};
        visit_all(s, lint, std::string(language_key(l)) + (touch ? " touch" : ""));
        frames += lint.frames_checked;
        for (const std::string& i : lint.issues) all += i + "\n";
    }
    INFO("frames checked: " << frames);
    CHECK_MESSAGE(all.empty(), all);
    CHECK(frames > 30);
}

}  // namespace

TEST_CASE("DOC-20 UI lint: every window in every language, desktop layout") { run_lint(false, "desktop"); }

TEST_CASE("DOC-20 UI lint: every window in every language, touch layout (UI-13)") { run_lint(true, "touch"); }
