// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app shell (step 9.1) driven through the headless UI harness
// (TST-50): the menu bar (UI-14), the start screen (UI-17), New indoor
// project then Add floor (IND-01, IND-10), About (UI-15 hook), Open demo
// (DEMO-05) and Exit, by clicking as a user does.
#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <string>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/build_info.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

/// The app's state, its ImGui layer and the harness, as main() sets them up.
struct Shell {
    explicit Shell(AppPaths paths, AppSettings settings = {}) : state(std::move(settings), std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style();
        ui.set_draw([this] { gui.draw(); });
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

}  // namespace

TEST_CASE("harness: items, windows and the main window of the start screen") {
    Shell s({});
    CHECK(s.ui.window_open("main"));
    CHECK(s.ui.window_open("status_bar"));
    CHECK(s.ui.has("menu.file"));
    CHECK(s.ui.has("menu.help"));
    for (const char* id : {"start.file.new_project", "start.file.open_project", "start.file.open_demo", "start.help.tour"})
        CHECK_MESSAGE(s.ui.has(id), id << "\n" << s.ui.dump());
    // Not available: drawn, but disabled (DOC-07 c).
    CHECK_FALSE(s.ui.find("start.help.tour")->disabled);  // step 9.10
    CHECK(s.ui.find("start.file.open_demo")->disabled);   // no data folders here
    CHECK_FALSE(s.ui.find("start.file.new_project")->disabled);
    CHECK_FALSE(s.ui.click("start.file.open_demo"));  // a disabled item is not clicked
    CHECK_FALSE(s.ui.has("no.such.item"));
    // Items lie inside the display.
    for (const ProbeItem& it : s.ui.items()) {
        if (it.label.empty()) continue;
        CAPTURE(it.label);
        CHECK(it.x0 >= 0.0F);
        CHECK(it.y0 >= 0.0F);
    }
}

TEST_CASE("menus (UI-14): every File and Help entry, disabled ones not clickable") {
    Shell s({});
    REQUIRE(s.ui.click("menu.file"));
    REQUIRE(s.ui.run_until([&] { return s.ui.has("file.exit"); }, 10));
    for (const char* id : {"file.new_project", "file.open_project", "file.recent", "file.open_demo", "file.save",
                           "file.save_as", "file.add_floor", "file.close_project", "file.export", "file.report",
                           "file.settings", "file.about", "file.exit"})
        CHECK_MESSAGE(s.ui.has(id), id << "\n" << s.ui.dump());
    for (const char* id : {"file.save", "file.add_floor", "file.export", "file.open_demo"}) CHECK(s.ui.find(id)->disabled);
    CHECK_FALSE(s.ui.find("file.about")->disabled);
    CHECK_FALSE(s.ui.find("file.settings")->disabled);
    s.ui.click_at(1200.0F, 700.0F);  // empty space closes the menu
    REQUIRE(s.ui.run_until([&] { return !s.ui.has("file.exit"); }, 10));

    REQUIRE(s.ui.click("menu.help"));
    REQUIRE(s.ui.run_until([&] { return s.ui.has("help.about"); }, 10));
    REQUIRE(s.ui.has("help.tour"));
    CHECK_FALSE(s.ui.find("help.tour")->disabled);  // step 9.10
    REQUIRE(s.ui.has("help.quick_starts"));
    CHECK_FALSE(s.ui.find("help.quick_starts")->disabled);  // step 12.4 (DOC-03)
    REQUIRE(s.ui.has("help.user_manual"));
    CHECK_FALSE(s.ui.find("help.user_manual")->disabled);  // step 9.9
    REQUIRE(s.ui.has("help.user_manual_pdf"));  // UI-14, DOC-02a: greyed with its reason without the PDF
    CHECK(s.ui.find("help.user_manual_pdf")->disabled == !s.state.action(Action::UserManualPdf).enabled);
    REQUIRE(s.ui.has("help.reset_settings"));
    CHECK_FALSE(s.ui.find("help.reset_settings")->disabled);  // step 9.7
    REQUIRE(s.ui.click("help.about"));
    CHECK(s.ui.wait_window("dlg.about"));
    CHECK(s.state.dialog() == Dialog::About);
}

TEST_CASE("About (UI-15 hook): opens from the File menu, shows the version and closes") {
    Shell s({});
    REQUIRE(s.ui.click_menu("file", "file.about"));
    REQUIRE(s.ui.wait_window("dlg.about"));
    CHECK(s.state.about().version == std::string(yantra::core::build_info().version_string));
    REQUIRE(s.ui.click("dlg.cancel", "dlg.about"));
    CHECK(s.ui.wait_window_closed("dlg.about"));
    CHECK(s.state.dialog() == Dialog::None);
}

TEST_CASE("New indoor project, then Add floor (IND-01, IND-10), through the dialogs") {
    const fs::path dir = scratch("new_project");
    AppPaths paths;
    paths.home = dir;
    Shell s(paths);
    REQUIRE(s.ui.click("start.file.new_project"));
    REQUIRE(s.ui.wait_window("dlg.new_project"));
    REQUIRE(s.ui.fill("name", "Depot", "dlg.new_project"));
    REQUIRE(s.ui.fill("file", (dir / "depot.yantra").string(), "dlg.new_project"));
    CHECK(s.state.new_project_form().name == "Depot");
    REQUIRE(s.ui.click("dlg.ok", "dlg.new_project"));
    // The project is created and the first floor is asked for.
    REQUIRE(s.ui.wait_window("dlg.add_floor"));
    CHECK(s.state.screen() == Screen::Project);
    CHECK(fs::exists(dir / "depot.yantra"));
    REQUIRE(s.ui.has("af.plan_file", "dlg.add_floor"));
    CHECK_FALSE(s.ui.find("af.plan_file", "dlg.add_floor")->disabled);  // plan import (step 9.3)
    REQUIRE(s.ui.fill("name", "Ground", "dlg.add_floor"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.add_floor"));
    REQUIRE(s.ui.wait_window_closed("dlg.add_floor"));
    const auto v = s.state.project_view();
    REQUIRE(v);
    REQUIRE(v->floors.size() == 1);
    CHECK(v->floors[0].name == "Ground");
    CHECK(v->floors[0].level == 0);
    CHECK(s.state.window_title() == "Yantra - Depot");

    // The File menu now offers the project actions.
    REQUIRE(s.ui.click("menu.file"));
    REQUIRE(s.ui.run_until([&] { return s.ui.has("file.close_project"); }, 10));
    CHECK_FALSE(s.ui.find("file.save")->disabled);
    REQUIRE(s.ui.click("file.close_project"));
    CHECK(s.ui.run_until([&] { return s.state.screen() == Screen::Start; }, 10));
    // The recent list has it.
    REQUIRE(s.ui.frames(2));
    CHECK(s.ui.has("start.recent.0"));
    REQUIRE(s.ui.click("start.recent.0"));
    CHECK(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
}

TEST_CASE("Cancel and Escape close a dialog without a change") {
    Shell s({});
    REQUIRE(s.ui.click("start.file.new_project"));
    REQUIRE(s.ui.wait_window("dlg.new_project"));
    REQUIRE(s.ui.click("dlg.cancel", "dlg.new_project"));
    CHECK(s.ui.wait_window_closed("dlg.new_project"));
    REQUIRE(s.ui.click("start.file.open_project"));
    REQUIRE(s.ui.wait_window("dlg.browse"));
    s.ui.key(ImGuiKey_Escape);
    CHECK(s.ui.wait_window_closed("dlg.browse"));
    CHECK(s.state.screen() == Screen::Start);
    CHECK(s.state.dialog() == Dialog::None);
}

TEST_CASE("small window (UI-13): the dialogs fit the display") {
    Shell s({});
    s.ui.set_display_size(800.0F, 600.0F);
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("start.file.new_project"));
    REQUIRE(s.ui.wait_window("dlg.new_project"));
    REQUIRE(s.ui.frames(3));
    const auto ok = s.ui.find("dlg.ok", "dlg.new_project");
    REQUIRE(ok);
    CHECK(ok->x1 <= 800.0F);
    CHECK(ok->y1 <= 600.0F);
}

#ifdef YANTRA_TEST_DEMO_DATA_DIR
TEST_CASE("Open demo (DEMO-05): from the start screen, a copy in the user folder") {
    const fs::path dir = scratch("open_demo");
    AppPaths paths;
    paths.data_folders = {YANTRA_TEST_DEMO_DATA_DIR};
    paths.user_data = dir / "user";
    Shell s(paths);
    REQUIRE(s.ui.has("start.file.open_demo"));
    CHECK_FALSE(s.ui.find("start.file.open_demo")->disabled);
    REQUIRE(s.ui.click("start.file.open_demo"));
    REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
    CHECK(s.state.demo());
    CHECK(s.state.project_view()->path == dir / "user" / "demo" / "fraunces" / "fraunces.yantra");
    REQUIRE(s.ui.frames(2));
    CHECK(s.ui.window_open("main"));
}
#endif

TEST_CASE("Exit (File menu) asks the app to quit") {
    Shell s({});
    REQUIRE(s.ui.click_menu("file", "file.exit"));
    CHECK(s.state.quit_requested());
}
