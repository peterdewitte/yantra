// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step 9.9 through the UI harness: the settings of the language, theme,
// touch layout and sounds (UI-12, UI-13, UI-09/10), the in-app help with
// its buttons and F1, also over a modal dialog (DOC-07 e), and the popup
// of a level alert (UI-10). Sounds go to the FakeAudio.
#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <memory>
#include <string>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/app/audio.h"
#include "yantra/app/i18n.h"
#include "yantra/app/settings.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "help_sound" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

AppPaths paths_in(const fs::path& dir) {
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.data_folders = {YANTRA_TEST_SOURCE_DATA_DIR};
    p.radios.plugin_folders = {dir / "plugins"};
    p.radios.plugin_state = dir / "config" / "plugin_state.json";
    return p;
}

struct Shell {
    explicit Shell(AppPaths paths, AppSettings settings = {}) : state(std::move(settings), std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style(Gui::style_of(state.settings()));
        ui.set_draw([this] { gui.draw(); });
        REQUIRE(ui.frames(3));
    }
    ~Shell() { set_language(Language::En); }
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

}  // namespace

TEST_CASE("UI-12, UI-13 settings: language, high-contrast theme, touch layout and scale") {
    Shell s(paths_in(scratch("general")));
    REQUIRE(s.ui.click_menu("file", "file.settings"));
    REQUIRE(s.ui.wait_window("dlg.settings"));
    REQUIRE(s.ui.frames(3));
    // (The language combo has no label in the probe: ImGui reports none for combos.)
    for (const char* id : {"settings.theme.high_contrast", "settings.touch", "settings.ui_scale"})
        CHECK_MESSAGE(s.ui.has(id, "dlg.settings"), std::string(id) << "\n" << s.ui.dump());
    const float desktop_h = s.ui.find("dlg.ok", "dlg.settings")->y1 - s.ui.find("dlg.ok", "dlg.settings")->y0;
    REQUIRE(s.ui.click("settings.theme.high_contrast", "dlg.settings"));
    REQUIRE(s.ui.click("settings.touch", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    AppSettings form = s.state.settings_form();
    form.language = "nl";
    s.state.settings_form() = form;
    REQUIRE(s.ui.click("dlg.ok", "dlg.settings"));
    REQUIRE(s.ui.wait_window_closed("dlg.settings"));
    CHECK(s.state.settings().theme == Theme::HighContrast);
    CHECK(s.state.settings().touch);
    CHECK(language() == Language::Nl);
    REQUIRE(s.ui.frames(3));
    // Touch layout: the start screen's buttons are at least 48 px high.
    for (const ProbeItem& it : s.ui.items())
        if (it.key().rfind("start.", 0) == 0 && !it.label.empty())
            CHECK_MESSAGE(it.y1 - it.y0 >= kTouchTargetPx - 0.5F, std::string(it.key()));
    REQUIRE(s.ui.click_menu("file", "file.settings"));
    REQUIRE(s.ui.wait_window("dlg.settings"));
    REQUIRE(s.ui.frames(3));
    const float touch_h = s.ui.find("dlg.ok", "dlg.settings")->y1 - s.ui.find("dlg.ok", "dlg.settings")->y0;
    CHECK(touch_h >= kTouchTargetPx - 0.5F);
    CHECK(touch_h > desktop_h);
    // High contrast: black windows, white text.
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x < 0.05F);
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_Text].x > 0.95F);
}

TEST_CASE("UI-09 settings: the Sound tab, test sounds through the FakeAudio and level alerts") {
    Shell s(paths_in(scratch("sound")));
    auto audio = std::make_shared<FakeAudio>();
    s.state.set_audio(audio);
    s.ui.set_display_size(1600.0F, 1400.0F);  // the whole Sound tab in view, no scrolling
    REQUIRE(s.ui.click_menu("file", "file.settings"));
    REQUIRE(s.ui.wait_window("dlg.settings"));
    REQUIRE(s.ui.click("settings.tab.sound", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("settings.sound", "dlg.settings"), s.ui.dump());
    // Off: the test buttons say why they are greyed.
    CHECK(s.ui.find("settings.test.sync_lost", "dlg.settings")->disabled);
    REQUIRE(s.ui.click("settings.sound", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_FALSE(s.ui.find("settings.test.sync_lost", "dlg.settings")->disabled);
    REQUIRE(s.ui.click("settings.test.sync_lost", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    CHECK(audio->played() == 1);
    // A level alert: added, filled, applied.
    REQUIRE(s.ui.click("settings.alert.add", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.fill("settings.alert.cell.0", "Lobby", "dlg.settings"), s.ui.dump());
    REQUIRE(s.ui.click("settings.alert_popup", "dlg.settings"));  // leaves the field (and turns the popup off)
    REQUIRE(s.ui.click("settings.alert_popup", "dlg.settings"));  // and on again
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("dlg.ok", "dlg.settings"));
    REQUIRE(s.ui.wait_window_closed("dlg.settings"));
    REQUIRE(s.state.settings().sound.alerts.size() == 1);
    CHECK(s.state.settings().sound.alerts[0].cell == "Lobby");
    CHECK(s.state.settings().sound.enabled);
    CHECK(s.state.alerts().settings().alerts.size() == 1);

    // The popup of an alert (UI-10), dismissed by its button.
    SurveyFrame f;
    f.valid = true;
    CellRow r;
    r.name = "Lobby";
    r.level = -80.0;
    f.serving = r;
    audio->finish_playback();
    s.state.alerts().update({}, f, false, 0.0, *audio);
    f.serving->level = -99.0;
    s.state.alerts().update({}, f, false, 1.0, *audio);
    REQUIRE(s.ui.wait_window("win.alerts"));
    CHECK(audio->played() == 2);
    CHECK(s.ui.has("help", "win.alerts"));
    REQUIRE(s.ui.click("alerts.dismiss", "win.alerts"));
    CHECK(s.ui.wait_window_closed("win.alerts"));
}

TEST_CASE("DOC-07 e help: Help > User manual, chapters, a window's Help button, F1 and over a modal") {
    Shell s(paths_in(scratch("help")));
    REQUIRE(s.ui.click_menu("help", "help.user_manual"));
    REQUIRE(s.ui.wait_window("win.help"));
    REQUIRE_MESSAGE(s.ui.has("help.chapter.13", "win.help"), s.ui.dump());
    REQUIRE(s.ui.click("help.chapter.13", "win.help"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.help_view().number == "13");
    CHECK(s.state.help_view().text.find("13.2") != std::string::npos);
    REQUIRE(s.ui.click("help.contents", "win.help"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.help_view().number.empty());
    REQUIRE(s.ui.click("help.close", "win.help"));
    CHECK(s.ui.wait_window_closed("win.help"));

    // The start screen's Help button opens its section; F1 does the same.
    REQUIRE_MESSAGE(s.ui.click("help"), s.ui.dump());
    REQUIRE(s.ui.wait_window("win.help"));
    CHECK(s.state.help_view().number == "1.1");
    s.state.close_help();
    REQUIRE(s.ui.wait_window_closed("win.help"));

    // Over the settings dialog: the help opens on top, the dialog stays.
    REQUIRE(s.ui.click_menu("file", "file.settings"));
    REQUIRE(s.ui.wait_window("dlg.settings"));
    REQUIRE(s.ui.frames(2));
    s.ui.key(ImGuiKey_F1);
    REQUIRE(s.ui.wait_window("win.help"));
    CHECK(s.state.help_view().number == "15");  // the settings chapter
    CHECK(s.ui.window_open("dlg.settings"));
    REQUIRE(s.ui.click("help.close", "win.help"));
    REQUIRE(s.ui.wait_window_closed("win.help"));
    CHECK(s.ui.window_open("dlg.settings"));
    CHECK(s.state.dialog() == Dialog::Settings);
}
