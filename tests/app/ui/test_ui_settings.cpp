// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step 9.7 through the UI harness, as a user does: About with the logos,
// credits and plugins (UI-15), the settings window and its tabs, the
// plugin manager's switches (PLG-09), Reset all settings (UI-16) and the
// example beacon plugin's own Tools entry and window (PLG-06).
#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/app/plugin_ui.h"
#include "yantra/app/settings.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "settings" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

AppPaths paths_with_beacon(const fs::path& dir) {
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.data_folders = {YANTRA_TEST_SOURCE_DATA_DIR};
    const fs::path folder = dir / "plugins";
    fs::create_directories(folder);
    fs::copy_file(fs::path(YANTRA_TEST_EXAMPLE_PLUGIN_DIR) / "example_beacon" YANTRA_PLUGIN_SUFFIX, folder / "example_beacon" YANTRA_PLUGIN_SUFFIX,
                  fs::copy_options::overwrite_existing);
    p.radios.plugin_folders = {folder};
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
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

constexpr const char* kWindow = "plg.yantra.example.beacon.beacon_channels";

}  // namespace

TEST_CASE("About (UI-15): logos by theme, version, build date, credits and the loaded plugins") {
    Shell s(paths_with_beacon(scratch("about")));
    REQUIRE(s.ui.click_menu("help", "help.about"));
    REQUIRE(s.ui.wait_window("dlg.about"));
    REQUIRE(s.ui.frames(2));
    const Gui::AboutOverlay& a = s.gui.about_overlay();
    CHECK(a.shown);
    CHECK(a.yantra_logo);
    CHECK(a.curtat_logo);
    CHECK(a.curtat_file == "curtat_logo_dark_256.png");  // the dark theme (default)
    CHECK(a.libraries >= 5);
    CHECK(a.data >= 2);
    CHECK(a.plugins == 1);  // the beacon; radio plugins load with a project
    REQUIRE(s.ui.click("dlg.cancel", "dlg.about"));
    CHECK(s.ui.wait_window_closed("dlg.about"));

    // The light theme takes the logo for light backgrounds (BRD-03).
    AppSettings light = s.state.settings();
    light.theme = Theme::Light;
    REQUIRE(s.state.apply_settings(light).ok);
    REQUIRE(s.ui.click_menu("file", "file.about"));
    REQUIRE(s.ui.wait_window("dlg.about"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.gui.about_overlay().curtat_file == "curtat_logo_light_256.png");
    CHECK(s.gui.about_overlay().curtat_logo);
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x > 0.5F);  // the theme switched
}

TEST_CASE("settings window: tabs, range check, Apply, OK and Cancel; Ctrl+, opens it") {
    const fs::path dir = scratch("window");
    Shell s(paths_with_beacon(dir));
    s.ui.key(ImGuiKey_Comma, ImGuiMod_Ctrl);
    REQUIRE(s.ui.wait_window("dlg.settings"));
    CHECK(s.state.dialog() == Dialog::Settings);
    for (const char* id : {"settings.tab.general", "settings.tab.survey", "settings.tab.plugins", "settings.theme.dark",
                           "settings.theme.light", "settings.tooltips", "settings.tooltip_delay"})
        CHECK_MESSAGE(s.ui.has(id, "dlg.settings"), std::string(id) << "\n" << s.ui.dump());
    // (The window system combo has no label in the probe: ImGui reports none for combos.)
    CHECK(s.ui.find("dlg.apply", "dlg.settings")->disabled);  // nothing changed yet
    REQUIRE(s.ui.click("settings.theme.light", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.ui.find("dlg.apply", "dlg.settings")->disabled);
    REQUIRE(s.ui.click("dlg.apply", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.settings().theme == Theme::Light);
    CHECK(s.ui.window_open("dlg.settings"));

    REQUIRE(s.ui.click("settings.tab.survey", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    for (const char* id : {"settings.threshold", "settings.graph_span", "settings.ring", "settings.ring_minutes"})
        CHECK_MESSAGE(s.ui.has(id, "dlg.settings"), std::string(id) << "\n" << s.ui.dump());
    // Out of range: OK is greyed and the reason shows.
    // ImGui applies a typed number when the field is left (Enter).
    REQUIRE(s.ui.fill("settings.threshold", "-5", "dlg.settings"));
    s.ui.key(ImGuiKey_Enter);
    CHECK(s.ui.window_open("dlg.settings"));  // Enter in a field does not press OK
    REQUIRE(s.ui.frames(2));
    CHECK(s.ui.find("dlg.ok", "dlg.settings")->disabled);
    CHECK(s.state.settings_form().survey.threshold_dbm == -5.0);
    REQUIRE(s.ui.fill("settings.threshold", "-87", "dlg.settings"));
    REQUIRE(s.ui.fill("settings.graph_span", "20", "dlg.settings"));
    REQUIRE(s.ui.click("settings.ring", "dlg.settings"));  // leaves the field
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.ui.find("dlg.ok", "dlg.settings")->disabled);
    REQUIRE(s.ui.click("dlg.ok", "dlg.settings"));
    CHECK(s.ui.wait_window_closed("dlg.settings"));
    CHECK(s.state.settings().survey.threshold_dbm == -87.0);
    CHECK(s.state.settings().survey.graph_span_min == 20.0);
    CHECK(s.state.settings().survey.ring);
    CHECK(s.state.monitor()->stats().threshold_dbm == -87.0);

    // Cancel drops what was not applied.
    REQUIRE(s.ui.click_menu("file", "file.settings"));
    REQUIRE(s.ui.wait_window("dlg.settings"));
    REQUIRE(s.ui.click("settings.tab.general", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("settings.theme.dark", "dlg.settings"));
    REQUIRE(s.ui.click("dlg.cancel", "dlg.settings"));
    CHECK(s.ui.wait_window_closed("dlg.settings"));
    CHECK(s.state.settings().theme == Theme::Light);
}

TEST_CASE("plugin manager (PLG-09) in the settings window: the switch turns the beacon off and its menu goes") {
    Shell s(paths_with_beacon(scratch("manager")));
    // The Tools menu is always there (Spectrum, UI-24); the beacon adds an
    // entry to it (PLG-06).
    const auto beacon_entry = [&s] {
        REQUIRE(s.ui.click("menu.tools"));
        REQUIRE(s.ui.frames(2));
        const bool has = s.ui.has("plg.yantra.example.beacon.beacon_channels_open");
        CHECK(s.ui.has("tools.spectrum"));
        s.ui.key(ImGuiKey_Escape);
        REQUIRE(s.ui.frames(2));
        return has;
    };
    CHECK(beacon_entry());
    REQUIRE(s.ui.click_menu("file", "file.settings"));
    REQUIRE(s.ui.wait_window("dlg.settings"));
    REQUIRE(s.ui.click("settings.tab.plugins", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("plugins.toggle.0", "dlg.settings"), s.ui.dump());
    CHECK(s.state.plugin_manager().plugins.at(0).name == "Beacon technology (example)");
    CHECK_FALSE(s.ui.find("plugins.toggle.0", "dlg.settings")->disabled);
    REQUIRE(s.ui.click("plugins.toggle.0", "dlg.settings"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.plugin_manager().plugins.at(0).status == "disabled");
    REQUIRE(s.ui.click("dlg.cancel", "dlg.settings"));
    REQUIRE(s.ui.wait_window_closed("dlg.settings"));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(beacon_entry());
}

TEST_CASE("Reset all settings (UI-16): what is reset and kept, also calibration, backup, the UI restarts") {
    const fs::path dir = scratch("reset");
    AppPaths paths = paths_with_beacon(dir);
    AppSettings changed;
    changed.theme = Theme::Light;
    changed.survey.threshold_dbm = -80.0;
    std::string error;
    REQUIRE(save_settings(paths.settings_file, changed, error));
    Shell s(paths, changed);
    REQUIRE(s.ui.click_menu("help", "help.reset_settings"));
    REQUIRE(s.ui.wait_window("dlg.reset_settings"));
    CHECK(s.ui.has("reset.calibration", "dlg.reset_settings"));
    REQUIRE(s.ui.click("reset.calibration", "dlg.reset_settings"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.reset_form().also_calibration);
    REQUIRE(s.ui.click("dlg.ok", "dlg.reset_settings"));
    CHECK(s.ui.wait_window_closed("dlg.reset_settings"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.state.settings() == AppSettings{});
    CHECK_FALSE(fs::exists(paths.settings_file));
    CHECK(fs::exists(dir / "config" / "backups"));
    CHECK(s.state.status().find("backups") != std::string::npos);
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_WindowBg].x < 0.5F);  // dark again
    CHECK(s.ui.window_open("main"));
    CHECK(s.ui.has("start.file.new_project"));
}

TEST_CASE("plugin UI (PLG-06): Tools > Beacon channels opens the beacon's window drawn through yantra_ui_api") {
    Shell s(paths_with_beacon(scratch("plugin_ui")));
    REQUIRE(s.ui.click_menu("tools", "plg.yantra.example.beacon.beacon_channels_open"));
    REQUIRE(s.ui.wait_window(kWindow));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.gui.plugin_frame().size() == 1);
    CHECK(s.gui.plugin_frame()[0].drawn);
    CHECK(s.gui.plugin_frame()[0].controls == 3);  // number, value, table
    // The channel (1..8): a slider and a field for the typed value.
    CHECK_MESSAGE(s.ui.has("plg.channel.slider", kWindow), s.ui.dump());
    REQUIRE(s.ui.has("plg.channel.input", kWindow));
    CHECK(s.ui.has("plg.frequency", kWindow));
    const Gui::PluginWindowState* st = s.gui.plugin_window_state(kWindow);
    REQUIRE(st != nullptr);
    CHECK(st->table_rows.at("band_plan") == 8);
    const std::string before = st->values.at("frequency");
    REQUIRE(s.ui.fill("plg.channel.input", "4", kWindow));
    s.ui.key(ImGuiKey_Enter);
    REQUIRE(s.ui.frames(3));
    st = s.gui.plugin_window_state(kWindow);
    REQUIRE(st != nullptr);
    CHECK(st->values.at("frequency") == "430.100 MHz");  // 430 MHz + 4 x 25 kHz
    CHECK(st->values.at("frequency") != before);
    // Out of range values are clamped to the plugin's limits.
    REQUIRE(s.ui.fill("plg.channel.input", "99", kWindow));
    s.ui.key(ImGuiKey_Enter);
    REQUIRE(s.ui.frames(3));
    st = s.gui.plugin_window_state(kWindow);
    REQUIRE(st != nullptr);
    CHECK(st->values.at("frequency") == "430.200 MHz");
    // The plugin switched off: its window closes.
    REQUIRE(s.state.set_plugin_enabled(false, "yantra.example.beacon", false).ok);
    REQUIRE(s.ui.frames(3));
    CHECK_FALSE(s.ui.window_open(kWindow));
    CHECK(s.gui.plugin_frame().empty());
}
