// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step 9.7, the model: the settings window's values (theme, coverage
// threshold, graph span, ring buffer defaults), Reset all settings with
// its backup (UI-16, TST-37) and --reset-settings, the About credits and
// logos (UI-15), the plugin manager (PLG-09) and the plugins' own menus
// and windows (PLG-06) with the example beacon plugin.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "yantra/app/app_icon.h"
#include "yantra/app/app_state.h"
#include "yantra/app/cli.h"
#include "yantra/app/config_profile.h"
#include "yantra/app/credits.h"
#include "yantra/app/plugin_ui.h"
#include "yantra/app/settings.h"
#include "yantra/app/settings_reset.h"
#include "yantra/app/survey_monitor.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/plugin/loader.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "settings_plugins" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void write(const fs::path& f, const std::string& text) {
    fs::create_directories(f.parent_path());
    std::ofstream(f, std::ios::binary) << text;
}

std::string read(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The example beacon plugin (PLG-15) copied into a plugin folder of its own.
fs::path beacon_folder(const fs::path& dir) {
    const fs::path folder = dir / "plugins";
    fs::create_directories(folder);
    fs::copy_file(fs::path(YANTRA_TEST_EXAMPLE_PLUGIN_DIR) / "example_beacon.so", folder / "example_beacon.so",
                  fs::copy_options::overwrite_existing);
    return folder;
}

AppPaths beacon_paths(const fs::path& dir) {
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.radios.plugin_folders = {beacon_folder(dir)};
    p.radios.plugin_state = dir / "config" / "plugin_state.json";
    return p;
}

constexpr const char* kBeacon = "yantra.example.beacon";

/// A copy of the row (the view is often a temporary).
std::optional<PluginRowView> row(const PluginManagerView& v, const std::string& id) {
    for (const PluginRowView& r : v.plugins)
        if (r.id == id) return r;
    return std::nullopt;
}

}  // namespace

TEST_CASE("settings (step 9.7): theme, threshold, graph span and ring buffer round trip and range checks") {
    AppSettings s;
    CHECK(s.theme == Theme::Dark);
    CHECK(s.survey.threshold_dbm == -95.0);
    CHECK(s.survey.graph_span_min == 10.0);
    CHECK_FALSE(s.survey.ring);
    CHECK(check_settings(s).empty());
    s.theme = Theme::Light;
    s.survey = SurveySettings{-88.0, 25.0, true, 15.0};
    s.tooltips = false;
    s.tooltip_delay_s = 1.5;
    std::string error;
    const AppSettings back = settings_from_json(settings_to_json(s), error);
    CHECK(error.empty());
    CHECK(back == s);
    CHECK(theme_key(Theme::Light) == std::string("light"));
    CHECK(parse_theme("dark") == Theme::Dark);
    CHECK_FALSE(parse_theme("purple").has_value());

    // Out of range values are refused by the window and ignored on load.
    for (auto bad : {+[](AppSettings& a) { a.survey.threshold_dbm = -10.0; },
                     +[](AppSettings& a) { a.survey.threshold_dbm = -151.0; },
                     +[](AppSettings& a) { a.survey.graph_span_min = 0.5; },
                     +[](AppSettings& a) { a.survey.graph_span_min = 61.0; },
                     +[](AppSettings& a) { a.survey.ring_minutes = 1.0; },
                     +[](AppSettings& a) { a.tooltip_delay_s = 6.0; },
                     +[](AppSettings& a) { a.window_system = "mir"; }}) {
        AppSettings a;
        bad(a);
        CHECK_FALSE(check_settings(a).empty());
    }
    const AppSettings loaded = settings_from_json(
        R"({"theme": "neon", "survey": {"threshold_dbm": 5, "graph_span_min": 1000, "ring": true, "ring_minutes": 30}})", error);
    CHECK(loaded.theme == Theme::Dark);
    CHECK(loaded.survey.threshold_dbm == -95.0);
    CHECK(loaded.survey.graph_span_min == 10.0);
    CHECK(loaded.survey.ring);
    CHECK(loaded.survey.ring_minutes == 30.0);
}

TEST_CASE("settings file format (NFR-05): another kind or a newer version is refused, version 1 is read") {
    const char* body = R"("theme": "light", "survey": {"ring": true, "ring_minutes": 30})";
    auto load = [&](const std::string& format, std::string& error) {
        error.clear();
        return settings_from_json("{" + format + body + "}", error);
    };
    std::string error;
    for (const char* ok : {R"("format": "yantra-app-settings/1", )", ""}) {
        const AppSettings s = load(ok, error);
        CHECK(error.empty());
        CHECK(s.theme == Theme::Light);
        CHECK(s.survey.ring);
    }
    for (const char* refused :
         {R"("format": "yantra-app-settings/2", )", R"("format": "yantra-plugin-state", )",
          R"("format": "yantra-app-settings/", )", R"("format": "yantra-app-settings/1x", )",
          R"("format": 1, )", R"("format": "yantra-app-settings/99999999999", )"}) {
        CAPTURE(refused);
        const AppSettings s = load(refused, error);
        CHECK_FALSE(error.empty());
        CHECK(s == AppSettings{});
    }
    load(R"("format": "yantra-app-settings/2", )", error);
    CHECK(error.find("newer") != std::string::npos);
}

TEST_CASE("settings window (step 9.7): apply, the coverage threshold follows and is kept") {
    const fs::path dir = scratch("apply");
    AppPaths paths;
    paths.settings_file = dir / "app.json";
    AppState st({}, paths);
    CHECK(st.trigger(Action::Settings).ok);
    CHECK(st.dialog() == Dialog::Settings);
    AppSettings& f = st.settings_form();
    f.survey.threshold_dbm = -10.0;  // out of range
    CHECK_FALSE(st.apply_settings(f).ok);
    CHECK(st.settings().survey.threshold_dbm == -95.0);
    f.survey.threshold_dbm = -90.0;
    f.survey.graph_span_min = 30.0;
    f.theme = Theme::Light;
    CHECK(st.apply_settings(f).ok);
    CHECK(st.settings().survey.threshold_dbm == -90.0);
    CHECK(st.settings().theme == Theme::Light);
    CHECK(st.monitor()->stats().threshold_dbm == -90.0);
    CHECK(st.settings_changed());
    // The survey panel's field changes the setting too (UI-11).
    CHECK(st.set_coverage_threshold(-100.0).ok);
    CHECK(st.settings().survey.threshold_dbm == -100.0);
    CHECK(st.monitor()->stats().threshold_dbm == -100.0);
    CHECK_FALSE(st.set_coverage_threshold(0.0).ok);
    // A new run starts with the stored threshold.
    std::string error;
    REQUIRE(save_settings(paths.settings_file, st.settings(), error));
    AppState again(load_settings(paths.settings_file, error), paths);
    CHECK(again.monitor()->stats().threshold_dbm == -100.0);
    CHECK(again.settings().survey.graph_span_min == 30.0);
}

TEST_CASE("Reset all settings (UI-16, TST-37): backup, defaults, projects and recordings untouched, calibration kept") {
    const fs::path dir = scratch("reset");
    const fs::path config = dir / "config";
    const SettingsFiles files{config / "app.json", config / "plugin_state.json", dir / "data" / "calibration.json"};
    AppSettings changed;
    changed.theme = Theme::Light;
    changed.survey.threshold_dbm = -80.0;
    add_recent_project(changed, dir / "projects" / "a.yantra");
    std::string error;
    REQUIRE(save_settings(files.settings_file, changed, error));
    write(files.plugin_state, R"({"plugins": {"yantra.example.beacon": {"enabled": false}}})");
    write(files.calibration_table, R"({"calibration": 1})");
    write(dir / "projects" / "a.yantra", "project");
    write(dir / "projects" / "recordings" / "walk.ysurvey", "recording");

    const ResetSummary s = reset_summary(false);
    CHECK(std::find(s.reset.begin(), s.reset.end(), "Enabled plugins") != s.reset.end());
    CHECK(std::find(s.kept.begin(), s.kept.end(), "Projects") != s.kept.end());
    CHECK(std::find(s.kept.begin(), s.kept.end(), "Recordings") != s.kept.end());
    CHECK(std::find(s.kept.begin(), s.kept.end(), "Calibration tables") != s.kept.end());
    CHECK(std::find(reset_summary(true).reset.begin(), reset_summary(true).reset.end(), "Calibration tables") !=
          reset_summary(true).reset.end());
    CHECK(backup_root(files) == config / "backups");

    const std::time_t now = 1'800'000'000;
    const ResetOutcome o = reset_settings_files(files, false, now);
    CHECK_MESSAGE(o.ok, o.message);
    CHECK(o.backup_folder.parent_path() == config / "backups");
    CHECK(o.message.find(o.backup_folder.string()) != std::string::npos);
    // The backup holds the files; the originals are gone (defaults at next start).
    CHECK(fs::exists(o.backup_folder / "app.json"));
    CHECK(fs::exists(o.backup_folder / "plugin_state.json"));
    CHECK_FALSE(fs::exists(o.backup_folder / "calibration.json"));
    CHECK_FALSE(fs::exists(files.settings_file));
    CHECK_FALSE(fs::exists(files.plugin_state));
    CHECK(load_settings(files.settings_file, error) == AppSettings{});
    CHECK(error.empty());
    std::string e2;
    CHECK(load_settings(o.backup_folder / "app.json", e2) == changed);
    // Kept: calibration (unticked), projects, recordings.
    CHECK(read(files.calibration_table) == R"({"calibration": 1})");
    CHECK(read(dir / "projects" / "a.yantra") == "project");
    CHECK(read(dir / "projects" / "recordings" / "walk.ysurvey") == "recording");

    // Again in the same second: a second backup folder, now with calibration.
    REQUIRE(save_settings(files.settings_file, changed, error));
    const ResetOutcome o2 = reset_settings_files(files, true, now);
    CHECK(o2.ok);
    CHECK(o2.backup_folder != o.backup_folder);
    CHECK(fs::exists(o2.backup_folder / "calibration.json"));
    CHECK_FALSE(fs::exists(files.calibration_table));
    // Nothing stored: nothing to back up, still fine.
    const ResetOutcome o3 = reset_settings_files(files, true, now);
    CHECK(o3.ok);
    CHECK(o3.backup_folder.empty());
}

TEST_CASE("Reset all settings from the app (UI-16): closes the project, defaults in memory, UI restarts") {
    const fs::path dir = scratch("reset_app");
    AppPaths paths = beacon_paths(dir);
    AppSettings changed;
    changed.theme = Theme::Light;
    changed.survey.graph_span_min = 5.0;
    std::string error;
    REQUIRE(save_settings(paths.settings_file, changed, error));
    {
        yantra::project::ProjectInfo info;
        info.name = "Kept";
        yantra::project::Project::create(dir / "kept.yantra", info)->close();
    }
    AppState st(changed, paths);
    REQUIRE(st.open_project(dir / "kept.yantra").ok);
    REQUIRE(st.set_plugin_enabled(false, kBeacon, false).ok);  // stored in plugin_state.json
    REQUIRE(fs::exists(paths.radios.plugin_state));
    const std::uint64_t generation = st.ui_generation();
    CHECK(st.trigger(Action::ResetSettings).ok);
    CHECK(st.dialog() == Dialog::ResetSettings);
    CHECK(st.reset_backup_root() == dir / "config" / "backups");
    const Result r = st.confirm_reset_settings();
    CHECK_MESSAGE(r.ok, r.message);
    CHECK(st.dialog() == Dialog::None);
    CHECK(st.ui_generation() == generation + 1);
    CHECK(st.settings() == AppSettings{});
    CHECK_FALSE(st.settings_changed());
    CHECK(st.project() == nullptr);
    CHECK(fs::exists(dir / "kept.yantra"));
    CHECK_FALSE(fs::exists(paths.settings_file));
    CHECK_FALSE(fs::exists(paths.radios.plugin_state));
    // The plugins load again with their defaults: the beacon is on again.
    const std::optional<PluginRowView> b = row(st.plugin_manager(), kBeacon);
    REQUIRE(b);
    CHECK(b->enabled);
}

TEST_CASE("--reset-settings and --reset-calibration (UI-16)") {
    auto parse = [](std::vector<std::string> a) { return parse_command_line(a); };
    const ParseResult a = parse({"--reset-settings"});
    REQUIRE(a.command_line);
    CHECK(a.command_line->reset_settings);
    CHECK_FALSE(a.command_line->reset_calibration);
    const ParseResult b = parse({"--reset-settings", "--reset-calibration"});
    REQUIRE(b.command_line);
    CHECK(b.command_line->reset_calibration);
    const ParseResult c = parse({"--reset-calibration"});
    CHECK_FALSE(c.command_line);
    CHECK(c.error.find("--reset-settings") != std::string::npos);
    CHECK(usage_text().find("--reset-settings") != std::string::npos);
    const auto& names = dialog_names();
    CHECK(std::find(names.begin(), names.end(), "settings") != names.end());
    CHECK(std::find(names.begin(), names.end(), "reset-settings") != names.end());
}

TEST_CASE("About (UI-15): credits from THIRD_PARTY.md, logos by theme, build date, plugins") {
    const Credits c = parse_credits(
        "# T\n\n## Libraries\n\n| Component | Version | Licence | Source |\n|---|---|---|---|\n"
        "| `Dear ImGui` | 1.92 | MIT | https://github.com/ocornut/imgui |\n\n## Data\n\n"
        "| Item | Licence | Source |\n| --- | --- | --- |\n| Language codes | public domain | Library of Congress |\n"
        "\n## Other\n\n| Component | Licence |\n|---|---|\n| x | y |\n");
    REQUIRE(c.libraries.size() == 1);
    CHECK(c.libraries[0].name == "Dear ImGui");
    CHECK(c.libraries[0].licence == "MIT");
    CHECK(c.libraries[0].source == "https://github.com/ocornut/imgui");
    REQUIRE(c.data.size() == 1);
    CHECK(c.data[0].source == "Library of Congress");

    // The sources' data folder: THIRD_PARTY.md one level up, the logos in branding/.
    const std::vector<fs::path> data{YANTRA_TEST_SOURCE_DATA_DIR};
    const Credits real = load_credits(data);
    CHECK(real.error.empty());
    CHECK(real.libraries.size() >= 5);
    auto has = [](const std::vector<CreditRow>& rows, const std::string& text) {
        return std::any_of(rows.begin(), rows.end(), [&](const CreditRow& r) {
            return (r.name + r.licence + r.source).find(text) != std::string::npos;
        });
    };
    CHECK(has(real.libraries, "imgui"));
    CHECK(has(real.data, "Library of Congress"));
    CHECK(has(real.data, "MAK"));
    CHECK(load_credits({scratch("no_credits")}).error.find("THIRD_PARTY.md") != std::string::npos);

    CHECK(curtat_logo_relative(true, 256) == fs::path("branding/curtat_logo_dark_256.png"));
    std::string error;
    const auto logo = load_data_png(data, curtat_logo_relative(false, 256), error);
    REQUIRE_MESSAGE(logo, error);
    CHECK(logo->width == 256);
    CHECK(logo->rgba[3] == 0);  // the transparency is kept (any theme)
    CHECK_FALSE(load_data_png(data, "branding/no_such.png", error));
    CHECK_FALSE(error.empty());

    const fs::path dir = scratch("about");
    AppPaths paths = beacon_paths(dir);
    paths.data_folders = data;
    AppSettings light;
    light.theme = Theme::Light;
    AppState st(light, paths);
    const AboutView a = st.about();
    CHECK(a.build_date.size() == 10);
    CHECK(a.source_note.find("AGPL") != std::string::npos);
    CHECK(a.yantra_logo.filename().string().find("yantra") != std::string::npos);
    CHECK(fs::exists(a.yantra_logo));
    // BRD-03: the light theme takes the logo for light backgrounds.
    CHECK(fs::exists(a.curtat_logo));
    CHECK(a.curtat_logo.filename().string().find("light") != std::string::npos);
    CHECK_FALSE(a.credits.libraries.empty());
    REQUIRE(a.plugins.size() == 1);
    CHECK(a.plugins[0].id == kBeacon);
    CHECK_FALSE(a.plugins[0].version.empty());
    CHECK_FALSE(a.plugins[0].licence.empty());
    CHECK(a.plugins_note.find("Radio plugins") != std::string::npos);
    AppSettings dark = light;
    dark.theme = Theme::Dark;
    CHECK(st.apply_settings(dark).ok);
    CHECK(st.about().curtat_logo.filename().string().find("dark") != std::string::npos);
}

TEST_CASE("plugin manager (PLG-09): name, version, API, author, licence, status; enable and disable") {
    const fs::path dir = scratch("manager");
    const AppPaths paths = beacon_paths(dir);
    {
        AppState st({}, paths);
        const PluginManagerView v = st.plugin_manager();
        const std::optional<PluginRowView> b = row(v, kBeacon);
        REQUIRE(b);
        CHECK_FALSE(b->radio);
        CHECK(b->name == "Beacon technology (example)");
        CHECK(b->api == "1.1");
        CHECK_FALSE(b->version.empty());
        CHECK_FALSE(b->author.empty());
        CHECK_FALSE(b->licence.empty());
        CHECK(b->status == "enabled");
        CHECK(b->enabled);
        CHECK(b->can_toggle);
        CHECK(st.plugin_ui().has_menu(YANTRA_MENU_TOOLS));
        CHECK_FALSE(st.set_plugin_enabled(false, "no.such.plugin", false).ok);
        CHECK(st.set_plugin_enabled(false, kBeacon, false).ok);
        CHECK(row(st.plugin_manager(), kBeacon)->status == "disabled");
        CHECK_FALSE(row(st.plugin_manager(), kBeacon)->enabled);
        // A disabled plugin's menus go.
        CHECK_FALSE(st.plugin_ui().has_menu(YANTRA_MENU_TOOLS));
    }
    {
        // The choice is kept (plugin_state.json).
        AppState st({}, paths);
        CHECK_FALSE(row(st.plugin_manager(), kBeacon)->enabled);
        CHECK(st.set_plugin_enabled(false, kBeacon, true).ok);
        CHECK(row(st.plugin_manager(), kBeacon)->enabled);
        CHECK(st.plugin_ui().has_menu(YANTRA_MENU_TOOLS));
    }
}

TEST_CASE("plugin UI (PLG-06): the beacon's Tools entry opens its window; disable closes it") {
    const fs::path dir = scratch("plugin_ui");
    AppState st({}, beacon_paths(dir));
    PluginUi& ui = st.plugin_ui();
    CHECK_FALSE(ui.has_menu(YANTRA_MENU_FILE));
    const std::vector<PluginMenuEntry> tools = ui.menu(YANTRA_MENU_TOOLS);
    REQUIRE(tools.size() == 1);
    CHECK(tools[0].id == "plg.yantra.example.beacon.beacon_channels_open");
    CHECK(tools[0].label == "Beacon channels");
    CHECK(tools[0].window_id == "beacon_channels");
    CHECK(tools[0].enabled);
    CHECK(std::string(plugin_menu_key(YANTRA_MENU_TOOLS)) == "tools");
    CHECK(ui.open_windows().empty());
    CHECK(ui.select(kBeacon, "beacon_channels_open").ok);
    REQUIRE(ui.open_windows().size() == 1);
    const PluginWindowView w = ui.open_windows()[0];
    CHECK(w.id == "plg.yantra.example.beacon.beacon_channels");
    CHECK(w.title == "Beacon channels");
    CHECK(w.kind == YANTRA_UI_WINDOW);
    CHECK(w.width == 360.0);
    CHECK(ui.select(kBeacon, "beacon_channels_open").ok);  // already open: still one
    CHECK(ui.open_windows().size() == 1);
    CHECK_FALSE(ui.select(kBeacon, "no_such_item").ok);
    CHECK_FALSE(ui.open(kBeacon, "no_such_window").ok);
    // Translations (UI-12): the beacon has Dutch, French and German.
    ui.set_language("nl");
    CHECK(ui.translate(kBeacon, "Beacon channels") != "Beacon channels");
    CHECK(ui.open_windows()[0].title == ui.translate(kBeacon, "Beacon channels"));
    ui.set_language("en");
    ui.close(kBeacon, "beacon_channels");
    CHECK(ui.open_windows().empty());
    CHECK(ui.open(kBeacon, "beacon_channels").ok);
    CHECK(st.set_plugin_enabled(false, kBeacon, false).ok);
    CHECK(st.plugin_ui().open_windows().empty());
    CHECK_FALSE(st.plugin_ui().select(kBeacon, "beacon_channels_open").ok);
}

TEST_CASE("Configuration profiles (NFR-06): settings, plugin state and calibration to a file and back") {
    const fs::path dir = scratch("profile");
    AppPaths paths = beacon_paths(dir);
    paths.radios.calibration_table = dir / "config" / "calibration.json";
    AppSettings mine;
    mine.theme = Theme::HighContrast;
    mine.survey.threshold_dbm = -88.0;
    mine.layers.set_visible(mine.layers.order.front(), false);
    mine.recent_projects = {dir / "a.yantra"};
    {
        AppState st(mine, paths);
        REQUIRE(st.set_plugin_enabled(false, kBeacon, false).ok);  // plugin_state.json
        yantra::calibration::Table table;
        write(paths.radios.calibration_table, table.to_json());
        const Result r = st.save_profile(dir / "site.yprofile");
        REQUIRE_MESSAGE(r.ok, r.message);
    }
    const std::string profile = read(dir / "site.yprofile");
    CHECK(profile.find("yantra-profile/1") != std::string::npos);
    CHECK(profile.find("a.yantra") == std::string::npos);  // the recent lists stay on the PC
    // Another PC: defaults, its own recent list, no plugin state, no calibration.
    const fs::path other = scratch("profile_other");
    AppPaths paths2 = beacon_paths(other);
    paths2.radios.calibration_table = other / "config" / "calibration.json";
    AppSettings theirs;
    theirs.recent_projects = {other / "b.yantra"};
    AppState st(theirs, paths2);
    REQUIRE(row(st.plugin_manager(), kBeacon)->enabled);
    const std::uint64_t generation = st.ui_generation();
    // A damaged profile changes nothing.
    write(other / "bad.yprofile",
          "{\"format\":\"yantra-profile/1\",\"settings\":{},\"calibration\":{\"format\":\"other\"}}");
    CHECK_FALSE(st.load_profile(other / "bad.yprofile").ok);
    write(other / "junk.yprofile", "not json");
    CHECK_FALSE(st.load_profile(other / "junk.yprofile").ok);
    CHECK(st.settings() == theirs);
    CHECK_FALSE(fs::exists(paths2.radios.plugin_state));
    CHECK(st.ui_generation() == generation);
    // The good one applies all three parts and keeps this PC's recent list.
    const Result r = st.load_profile(dir / "site.yprofile");
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(st.ui_generation() == generation + 1);
    CHECK(st.settings().theme == Theme::HighContrast);
    CHECK(st.settings().survey.threshold_dbm == doctest::Approx(-88.0));
    CHECK(st.settings().layers == mine.layers);
    CHECK(st.settings().recent_projects == theirs.recent_projects);
    CHECK(read(paths2.radios.calibration_table) == read(paths.radios.calibration_table));
    std::string load_error;
    CHECK(load_settings(paths2.settings_file, load_error).theme == Theme::HighContrast);
    CHECK(load_error.empty());
    REQUIRE(row(st.plugin_manager(), kBeacon));
    CHECK_FALSE(row(st.plugin_manager(), kBeacon)->enabled);  // the profile's plugin state
    // The browser purposes return to Settings / apply the file.
    CHECK(st.trigger(Action::Settings).ok);
    st.browse(BrowsePurpose::ProfileSave);
    CHECK(st.dialog() == Dialog::Browse);
    st.browser_cancel();
    CHECK(st.dialog() == Dialog::Settings);
}
