// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's non-drawing logic (TST-50): command line, window-system choice
// (NFR-10), settings, data folders, the application icon (BRD-06), menus
// and actions (UI-14), start screen (UI-17), About (UI-15 hook), project
// commands and Open demo (DEMO-05).
// Copied from yantra-v2 68ea20a: tests/app/test_app_model.cpp, adapted.
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "yantra/app/app_icon.h"
#include "yantra/app/app_state.h"
#include "yantra/app/cli.h"
#include "yantra/app/data_paths.h"
#include "yantra/app/manual.h"
#include "yantra/app/settings.h"
#include "yantra/app/window_system.h"
#include "yantra/core/build_info.h"
#include "yantra/core/project/project.h"
#include "test_platform.hpp"

namespace fs = std::filesystem;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    // Absolute with its drive on Windows (the build tree names "/home/..."),
    // as the folders the code under test gives back.
    const fs::path d = fs::absolute(fs::path(YANTRA_TEST_SCRATCH_DIR) / "model" / name);
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

std::vector<std::string> ids(const Menu& m) {
    std::vector<std::string> out;
    for (const MenuEntry& e : m.entries) {
        if (e.kind == MenuEntry::Kind::Action) out.push_back(e.item.id);
        if (e.kind == MenuEntry::Kind::Recent) out.push_back("file.recent");
    }
    return out;
}

void make_project(const fs::path& file, const std::string& name) {
    yantra::project::ProjectInfo info;
    info.name = name;
    auto p = yantra::project::Project::create(file, info);
    p->close();
}

}  // namespace

TEST_CASE("command line: defaults, options and errors") {
    auto ok = [](std::vector<std::string> a) {
        const ParseResult r = parse_command_line(a);
        REQUIRE_MESSAGE(r.command_line, r.error);
        return *r.command_line;
    };
    const CommandLine d = ok({});
    CHECK_FALSE(d.screenshot);
    CHECK(d.width == 1280);
    CHECK(d.height == 800);
    CHECK_FALSE(d.window_system);
    CHECK(d.frames == 0);
    CHECK(d.dialog.empty());

    const CommandLine c = ok({"--screenshot", "out/a.png", "--size", "1024x700", "--window-system", "X11", "--frames",
                              "3", "--dialog", "about", "house.yantra"});
    CHECK(*c.screenshot == fs::path("out/a.png"));
    CHECK(c.width == 1024);
    CHECK(c.height == 700);
    CHECK(*c.window_system == WindowSystem::X11);
    CHECK(c.frames == 3);
    CHECK(c.dialog == "about");
    CHECK(*c.project == fs::path("house.yantra"));
    CHECK(ok({"--version"}).show_version);
    CHECK(ok({"-h"}).show_help);
    for (const std::string& n : dialog_names()) CHECK(ok({"--dialog", n}).dialog == n);

    for (const std::vector<std::string>& bad :
         {std::vector<std::string>{"--bogus"}, {"--size", "100x100"}, {"--size", "800"}, {"--size", "800x600x2"},
          {"--window-system", "mir"}, {"--window-system"}, {"--screenshot"}, {"--frames", "0"}, {"--dialog", "nope"},
          {"--dialog"}, {"a.yantra", "b.yantra"}}) {
        CAPTURE(bad.front());
        const ParseResult r = parse_command_line(bad);
        CHECK_FALSE(r.command_line);
        CHECK_FALSE(r.error.empty());
    }
    CHECK(usage_text().find("--window-system") != std::string::npos);
    CHECK(usage_text().find("YANTRA_WINDOW_SYSTEM") != std::string::npos);
}

TEST_CASE("SCR-01 command line: theme, language, touch, scale, scene and input of a screenshot") {
    const ParseResult r = parse_command_line({"--screenshot", "s.png", "--theme", "high-contrast", "--language", "fr",
                                              "--touch", "--scale", "150", "--shot", "plan_import", "--input", "sheet.pdf"});
    REQUIRE_MESSAGE(r.command_line, r.error);
    const CommandLine& c = *r.command_line;
    CHECK(*c.theme == Theme::HighContrast);
    CHECK(*c.language == Language::Fr);
    CHECK(c.touch);
    CHECK(*c.scale == doctest::Approx(1.5));
    CHECK(c.shot == "plan_import");
    CHECK(*c.input == fs::path("sheet.pdf"));
    CHECK(*parse_command_line({"--theme", "light"}).command_line->theme == Theme::Light);
    CHECK(parse_command_line({"--list-shots"}).command_line->list_shots);
    for (const std::vector<std::string>& bad :
         {std::vector<std::string>{"--theme", "pink"}, {"--language", "it"}, {"--scale", "50"}, {"--scale", "300"},
          {"--shot", "survey"}, {"--screenshot", "a.png", "--shot", "survey", "--demo"},
          {"--screenshot", "a.png", "--shot", "survey", "p.yantra"}, {"--input"}, {"--shot"}}) {
        CAPTURE(bad.front());
        CHECK_FALSE(parse_command_line(bad).command_line);
    }
    CHECK(usage_text().find("--shot NAME") != std::string::npos);
}

TEST_CASE("window system (NFR-10): X11 preferred under WSL, with overrides") {
    CHECK(detect_wsl("6.18.33.2-microsoft-standard-WSL2", "", ""));
    CHECK(detect_wsl("6.8.0-generic", "Ubuntu", ""));
    CHECK(detect_wsl("6.8.0-generic", "", "/run/WSL/1_interop"));
    CHECK_FALSE(detect_wsl("6.8.0-45-generic", "", ""));

    WindowSystemInputs wsl;
    wsl.wsl = true;
    wsl.x11_display = true;
    wsl.wayland_display = true;
    CHECK(choose_window_system(wsl).system == WindowSystem::X11);
    CHECK(choose_window_system(wsl).reason.find("WSL") != std::string::npos);

    WindowSystemInputs native = wsl;
    native.wsl = false;
    CHECK(choose_window_system(native).system == WindowSystem::Auto);  // GLFW picks Wayland or X11

    WindowSystemInputs no_x = wsl;
    no_x.x11_display = false;
    CHECK(choose_window_system(no_x).system == WindowSystem::Auto);

    // Precedence: option > environment > setting > automatic.
    WindowSystemInputs in = wsl;
    in.setting = "wayland";
    CHECK(choose_window_system(in).system == WindowSystem::Wayland);
    in.environment = "x11";
    CHECK(choose_window_system(in).system == WindowSystem::X11);
    CHECK(choose_window_system(in).reason.find("YANTRA_WINDOW_SYSTEM") != std::string::npos);
    in.option = WindowSystem::Wayland;
    CHECK(choose_window_system(in).system == WindowSystem::Wayland);
    CHECK(choose_window_system(in).reason.find("--window-system") != std::string::npos);
    // An explicit "auto" option means the automatic choice (X11 under WSL).
    in.option = WindowSystem::Auto;
    CHECK(choose_window_system(in).system == WindowSystem::X11);

    WindowSystemInputs bad = native;
    bad.environment = "mir";
    const WindowSystemChoice b = choose_window_system(bad);
    CHECK(b.system == WindowSystem::Auto);
    CHECK(b.warning.find("mir") != std::string::npos);

    CHECK(parse_window_system("Wayland") == WindowSystem::Wayland);
    CHECK_FALSE(parse_window_system("x12"));
    CHECK(std::string(to_string(WindowSystem::X11)) == "x11");
}

TEST_CASE("settings: recent list, JSON round trip, damaged file, atomic save") {
    AppSettings s;
    for (int i = 0; i < 12; ++i) add_recent_project(s, "/p/" + std::to_string(i) + ".yantra");
    REQUIRE(s.recent_projects.size() == kMaxRecentProjects);
    CHECK(s.recent_projects.front() == fs::path("/p/11.yantra"));
    add_recent_project(s, "/p/5.yantra");
    CHECK(s.recent_projects.front() == fs::path("/p/5.yantra"));
    CHECK(s.recent_projects.size() == kMaxRecentProjects);
    CHECK(std::set<fs::path>(s.recent_projects.begin(), s.recent_projects.end()).size() == kMaxRecentProjects);
    remove_recent_project(s, "/p/5.yantra");
    CHECK(s.recent_projects.size() == kMaxRecentProjects - 1);

    s.window_system = "x11";
    s.tooltips = false;
    s.tooltip_delay_s = 1.25;
    std::string error;
    CHECK(settings_from_json(settings_to_json(s), error) == s);
    CHECK(error.empty());

    const AppSettings d = settings_from_json("[1, 2", error);
    CHECK_FALSE(error.empty());
    CHECK(d == AppSettings{});
    error.clear();
    const AppSettings w = settings_from_json(R"({"window_system": "mir", "tooltips": "yes", "tooltip_delay_s": 99,
                                                "recent_projects": ["/a.yantra", 3, ""], "unknown": 1})",
                                             error);
    CHECK(error.empty());
    CHECK(w.window_system == "auto");
    CHECK(w.tooltips);
    CHECK(w.tooltip_delay_s == doctest::Approx(0.5));
    CHECK(w.recent_projects == std::vector<fs::path>{"/a.yantra"});

    const fs::path dir = scratch("settings");
    const fs::path file = dir / "sub" / "app.json";
    error.clear();
    CHECK(load_settings(file, error) == AppSettings{});  // missing: defaults, no error
    CHECK(error.empty());
    REQUIRE(save_settings(file, s, error));
    CHECK_FALSE(fs::exists(file.string() + ".tmp"));
    CHECK(load_settings(file, error) == s);
    std::ofstream(file) << "not json";
    CHECK(load_settings(file, error) == AppSettings{});
    CHECK_FALSE(error.empty());

    CHECK(config_folder((yantra::test::kRoot + "/x/cfg"), (yantra::test::kRoot + "/home/u")) == fs::path((yantra::test::kRoot + "/x/cfg/yantra")));
    CHECK(config_folder("relative", (yantra::test::kRoot + "/home/u")) == fs::path((yantra::test::kRoot + "/home/u/.config/yantra")));
    CHECK(config_folder("", "").empty());
}

TEST_CASE("data folders: environment, installed share/yantra, build tree; user folders") {
    const fs::path dir = scratch("data");
    // An installed layout: <prefix>/bin and <prefix>/share/yantra.
    fs::create_directories(dir / "prefix" / "bin");
    fs::create_directories(dir / "prefix" / "share" / "yantra" / "logo");
    fs::create_directories(dir / "env");
    DataPathInputs in;
    in.executable_dir = dir / "prefix" / "bin";
    in.data_dir_env = (dir / "env").string();
    auto folders = data_folders(in);
    REQUIRE(folders.size() == 2);
    CHECK(folders[0] == fs::weakly_canonical(dir / "env"));
    CHECK(folders[1] == fs::weakly_canonical(dir / "prefix" / "share" / "yantra"));
    in.data_dir_env = "relative/path";  // ignored
    CHECK(data_folders(in).size() == 1);

    // A build tree: <build>/bin with <build>/CMakeCache.txt naming the sources.
    fs::create_directories(dir / "build" / "bin");
    fs::create_directories(dir / "src" / "data");
    std::ofstream(dir / "build" / "CMakeCache.txt") << "# comment\nYantra_SOURCE_DIR:STATIC=" << (dir / "src").string()
                                                    << "\nOTHER:STRING=x\n";
    CHECK(build_tree_source_dir(dir / "build") == dir / "src");
    CHECK(build_tree_source_dir(dir / "prefix").empty());
    // The file CMake writes for the same (copied with the tree to the
    // Windows test machine, unlike the cache).
    fs::create_directories(dir / "tree" / "bin");
    std::ofstream(dir / "tree" / "yantra-source-dir.txt") << (dir / "src").string() << "\n";
    CHECK(build_tree_source_dir(dir / "tree") == dir / "src");
    DataPathInputs b;
    b.executable_dir = dir / "build" / "bin";
    folders = data_folders(b);
    REQUIRE(folders.size() == 2);
    CHECK(folders[0] == fs::weakly_canonical(dir / "build"));
    CHECK(folders[1] == fs::weakly_canonical(dir / "src" / "data"));

    std::ofstream(dir / "src" / "data" / "x.txt") << "x";
    CHECK(find_data_file(folders, "x.txt") == fs::weakly_canonical(dir / "src" / "data") / "x.txt");
    CHECK_FALSE(find_data_file(folders, "y.txt"));

    DataPathInputs u;
    u.home = (yantra::test::kRoot + "/home/u");
    CHECK(user_data_folder(u) == fs::path((yantra::test::kRoot + "/home/u/.local/share/yantra")));
    CHECK(user_config_folder(u) == fs::path((yantra::test::kRoot + "/home/u/.config/yantra")));
    u.xdg_data_home = (yantra::test::kRoot + "/x/data");
    u.xdg_config_home = (yantra::test::kRoot + "/x/config");
    CHECK(user_data_folder(u) == fs::path((yantra::test::kRoot + "/x/data/yantra")));
    CHECK(user_config_folder(u) == fs::path((yantra::test::kRoot + "/x/config/yantra")));
    CHECK(user_data_folder(DataPathInputs{}).empty());
}

TEST_CASE("application icon (BRD-06): the Yantra logo PNGs at every icon size") {
    // The sources' data/ folder has the layout of share/yantra.
    std::string error;
    const auto icons = load_app_icons({fs::path(YANTRA_SOURCE_DIR) / "data"}, error);
    CHECK(error.empty());
    REQUIRE(icons.size() == std::size(kAppIconSizes));
    for (std::size_t i = 0; i < icons.size(); ++i) {
        CAPTURE(kAppIconSizes[i]);
        CHECK(icons[i].width == kAppIconSizes[i]);
        CHECK(icons[i].height == kAppIconSizes[i]);
        // Not empty: some opaque pixels, and transparent corners (as supplied).
        bool opaque = false;
        for (std::size_t p = 3; p < icons[i].rgba.size(); p += 4) opaque = opaque || icons[i].rgba[p] == 255;
        CHECK(opaque);
    }
    const auto none = load_app_icons({scratch("no_icons")}, error);
    CHECK(none.empty());
    CHECK(error.find("yantra_logo_16.png") != std::string::npos);
}

TEST_CASE("menus (UI-14, UI-18): File, Network, Window and Help items, tooltips, disabled reasons") {
    AppState st({}, {});
    const auto menus = st.menus();
    REQUIRE(menus.size() == 4);
    CHECK(menus[0].label == "File");
    CHECK(ids(menus[0]) == std::vector<std::string>{"file.new_project", "file.open_project", "file.recent",
                                                     "file.open_demo", "file.save", "file.save_as", "file.add_floor",
                                                     "file.close_project", "file.export", "file.report", "file.settings",
                                                     "file.about", "file.exit"});
    CHECK(menus[1].label == "Network");
    CHECK(ids(menus[1]) == std::vector<std::string>{"network.cell_db", "network.verification", "network.reselection"});
    CHECK(menus[2].label == "Window");
    CHECK(ids(menus[2]) == std::vector<std::string>{"window.neighbours", "window.graphs", "window.scanner",
                                                     "window.events", "window.statistics"});
    for (const MenuEntry& e : menus[2].entries) {
        CHECK(e.item.checkable);
        CHECK_FALSE(e.item.checked);
    }
    CHECK(menus[3].label == "Help");
    CHECK(ids(menus[3]) == std::vector<std::string>{"help.tour", "help.user_manual", "help.user_manual_pdf",
                                                     "help.quick_starts", "help.reset_settings", "help.about"});
    std::set<std::string> seen;
    for (const Menu& m : menus) {
        for (const MenuEntry& e : m.entries) {
            if (e.kind != MenuEntry::Kind::Action) continue;
            const ActionView& a = e.item;
            CAPTURE(a.id);
            CHECK(seen.insert(a.id).second);
            CHECK_FALSE(a.label.empty());
            CHECK(a.tooltip.size() > 20);  // DOC-07 b: every item explains itself
            if (!a.shortcut.empty()) CHECK(a.tooltip.find("[" + a.shortcut + "]") != std::string::npos);
            if (!a.enabled) CHECK_FALSE(a.disabled_reason.empty());  // DOC-07 c
            // Disabled either because it is not wired yet, for want of a
            // project, or (Open demo, the PDF manual) without the installed
            // file.
            const bool needs_project = a.id == "file.save" || a.id == "file.save_as" || a.id == "file.add_floor" ||
                                       a.id == "file.close_project" || a.id == "file.export" || a.id == "file.report" ||
                                       a.id.rfind("network.", 0) == 0 || a.id.rfind("window.", 0) == 0;
            if (!needs_project && a.id != "file.open_demo" && a.id != "help.user_manual_pdf")
                CHECK(a.enabled == a.planned_step.empty());
        }
    }
    // Not wired yet, each with the step that wires it.
    CHECK(st.action(Action::Tour).planned_step.empty());  // step 9.10: the guided tour
    CHECK(st.action(Action::Tour).enabled);
    CHECK(st.action(Action::UserManual).planned_step.empty());  // step 9.9: the help window
    CHECK(st.action(Action::UserManual).enabled);
    CHECK(st.action(Action::QuickStarts).planned_step.empty());  // step 12.4: the quick starts (DOC-03)
    CHECK(st.action(Action::QuickStarts).enabled);
    // Step 10.4: File > Export and File > Report, once a project is open.
    CHECK(st.action(Action::Export).planned_step.empty());
    CHECK(st.action(Action::Report).planned_step.empty());
    CHECK_FALSE(st.action(Action::Export).disabled_reason.empty());
    CHECK_FALSE(st.action(Action::Report).disabled_reason.empty());
    // Available now.
    for (Action a : {Action::NewProject, Action::OpenProject, Action::Exit, Action::About, Action::HelpAbout,
                     Action::Settings, Action::ResetSettings})
        CHECK(st.action(a).enabled);
    // Without data folders there is no demo to open.
    CHECK_FALSE(st.action(Action::OpenDemo).enabled);
    CHECK(st.action(Action::OpenDemo).disabled_reason.find("not installed") != std::string::npos);
    // Project actions need a project.
    for (Action a : {Action::Save, Action::SaveAs, Action::AddFloor, Action::CloseProject}) {
        CHECK_FALSE(st.action(a).enabled);
        CHECK(st.action(a).disabled_reason.find("No project is open") != std::string::npos);
        const Result r = st.trigger(a);
        CHECK_FALSE(r.ok);
        CHECK(st.status_is_error());
    }
    CHECK(st.trigger(Action::Tour).ok);  // step 9.10: opens the tour
    CHECK(st.tour().active());
    st.tour().leave();
    CHECK(st.trigger(Action::ResetSettings).ok);  // step 9.7
    CHECK(st.dialog() == Dialog::ResetSettings);
    st.cancel_dialog();
    CHECK(st.trigger(Action::Settings).ok);
    CHECK(st.dialog() == Dialog::Settings);
    st.cancel_dialog();
    CHECK(st.trigger(Action::NewProject).ok);
    CHECK(st.dialog() == Dialog::NewProject);
    st.cancel_dialog();
    CHECK(st.dialog() == Dialog::None);
    CHECK(st.trigger(Action::OpenProject).ok);
    CHECK(st.dialog() == Dialog::Browse);
    CHECK(st.browse_purpose() == BrowsePurpose::OpenProject);
    REQUIRE(st.browser() != nullptr);
    CHECK(st.browser()->mode() == BrowseMode::Open);
    st.cancel_dialog();
    CHECK(st.trigger(Action::HelpAbout).ok);
    CHECK(st.dialog() == Dialog::About);
    st.cancel_dialog();
    CHECK_FALSE(st.quit_requested());
    CHECK(st.trigger(Action::Exit).ok);
    CHECK(st.quit_requested());
}

TEST_CASE("About (UI-15 hook): name, version, licence, source link, homepage, statement") {
    AppState st({}, {});
    const AboutView a = st.about();
    CHECK(a.name == "Yantra");
    CHECK(a.version == std::string(yantra::core::build_info().version_string));
    CHECK(a.licence == "GPL-3.0-or-later");
    CHECK(a.source_url == "https://github.com/peterdewitte/yantra");
    CHECK(a.homepage == "https://curtat.be");
    CHECK(a.statement.find("curtat.be") != std::string::npos);
    CHECK(a.statement.find("Claude") != std::string::npos);
}

TEST_CASE("start screen (UI-17): New, Open, Open demo, Tour and the recent projects") {
    const fs::path dir = scratch("start");
    make_project(dir / "kept.yantra", "Kept");
    AppSettings s;
    add_recent_project(s, dir / "gone.yantra");
    add_recent_project(s, dir / "kept.yantra");
    AppState st(s, {});
    CHECK(st.screen() == Screen::Start);
    const StartScreenView v = st.start_screen();
    CHECK_FALSE(v.title.empty());
    CHECK_FALSE(v.intro.empty());
    std::vector<std::string> ids;
    for (const ActionView& a : v.actions) ids.push_back(a.id);
    CHECK(ids == std::vector<std::string>{"file.new_project", "file.open_project", "file.open_demo", "help.tour"});
    REQUIRE(v.recent.size() == 2);
    CHECK(v.recent[0].label.find("kept.yantra") == 0);
    CHECK(v.recent[0].exists);
    CHECK_FALSE(v.recent[1].exists);
    CHECK(st.window_title() == "Yantra");
}

TEST_CASE("project commands (IND-01, IND-10): new, add floor, save, save as, close, open, recent") {
    const fs::path dir = scratch("project");
    AppPaths paths;
    paths.settings_file = dir / "config" / "app.json";
    paths.home = dir;
    AppState st({}, paths);

    yantra::project::ProjectInfo info;
    CHECK_FALSE(st.new_project(dir / "a", info).ok);  // a name is required
    info.name = "Office A";
    info.site = "Main street";
    Result r = st.new_project(dir / "a", info);
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(st.screen() == Screen::Project);
    CHECK(fs::exists(dir / "a.yantra"));
    CHECK(st.window_title() == "Yantra - Office A");
    CHECK(st.settings().recent_projects.front() == (dir / "a.yantra").lexically_normal());
    CHECK(st.settings_changed());
    // IND-01: the first floor is asked for next.
    CHECK(st.dialog() == Dialog::AddFloor);
    CHECK(st.add_floor_form().level == 0);
    CHECK(st.add_floor_form().name == "Ground floor");
    for (Action a : {Action::Save, Action::SaveAs, Action::AddFloor, Action::CloseProject}) CHECK(st.action(a).enabled);

    st.add_floor_form().height_m = 3.2;
    REQUIRE(st.confirm_add_floor().ok);
    CHECK(st.dialog() == Dialog::None);
    CHECK(st.suggested_floor_level() == 1);
    CHECK_FALSE(st.add_floor("Again", 0, 3.0).ok);  // level taken
    CHECK(st.status().find("Ground floor") != std::string::npos);
    CHECK_FALSE(st.add_floor("", 1, 3.0).ok);
    CHECK_FALSE(st.add_floor("Tall", 1, 0.0).ok);
    CHECK_FALSE(st.add_floor("Tall", 1, 51.0).ok);
    REQUIRE(st.add_floor("Basement", -1, 2.8).ok);
    REQUIRE(st.trigger(Action::AddFloor).ok);
    CHECK(st.add_floor_form().level == 1);
    CHECK(st.add_floor_form().name == "Floor 1");
    st.cancel_dialog();
    auto view = st.project_view();
    REQUIRE(view);
    REQUIRE(view->floors.size() == 2);
    CHECK(view->floors[0].name == "Basement");  // ordered by level
    CHECK_FALSE(view->floors[0].has_plan);
    CHECK_FALSE(view->demo);
    CHECK(st.project()->meta().info.site == "Main street");
    CHECK(st.save().ok);

    REQUIRE(st.save_as(dir / "b").ok);
    CHECK(st.project_view()->path == dir / "b.yantra");
    CHECK(st.settings().recent_projects.front() == (dir / "b.yantra").lexically_normal());
    CHECK(st.close_project().ok);
    CHECK(st.screen() == Screen::Start);
    CHECK_FALSE(st.close_project().ok);

    // A second project with an existing name is refused.
    info.name = "Office A again";
    CHECK_FALSE(st.new_project(dir / "a.yantra", info).ok);
    CHECK(st.screen() == Screen::Start);

    REQUIRE(st.open_project(dir / "a.yantra").ok);
    CHECK(st.project_view()->floors.size() == 2);
    CHECK(st.open_project(dir / "a.yantra").ok);  // already open: nothing happens
    // Opening another project closes this one.
    REQUIRE(st.open_project(dir / "b.yantra").ok);
    CHECK(st.project_view()->name == "Office A");

    // A missing recent project is dropped from the list.
    fs::remove(dir / "a.yantra");
    CHECK_FALSE(st.open_project(dir / "a.yantra").ok);
    for (const fs::path& p : st.settings().recent_projects) CHECK(p != (dir / "a.yantra").lexically_normal());
    // A file that is not a project is refused with the reason.
    std::ofstream(dir / "junk.yantra") << "not a project";
    CHECK_FALSE(st.open_project(dir / "junk.yantra").ok);
    CHECK(st.project_view()->name == "Office A");  // the open project stays

    std::string error;
    REQUIRE(st.save_settings(error));
    CHECK_FALSE(st.settings_changed());
    CHECK(load_settings(paths.settings_file, error).recent_projects == st.settings().recent_projects);
}

TEST_CASE("IND-46 opening a project of Yantra 0.3 says once what its migration dropped") {
    const fs::path dir = scratch("migrated");
    const fs::path file = dir / "fraunces.yantra";
    fs::copy_file(fs::path(YANTRA_TEST_DATA_DIR) / "project" / "fraunces_0_3.yantra", file);
    AppPaths paths;
    paths.settings_file = dir / "config" / "app.json";
    paths.home = dir;
    {
        AppState st({}, paths);
        const Result r = st.open_project(file);
        REQUIRE_MESSAGE(r.ok, r.message);
        CHECK(r.message == st.status());
        CHECK_FALSE(st.status_is_error());
        CHECK(r.message.find("41 walls, 0 own wall classes, 5 antennas and 2 ray-tracing maps were dropped") !=
              std::string::npos);
        CHECK(r.message.find("(project schema 7)") != std::string::npos);
        CHECK(r.message.find(file.string() + ".schema7.bak") != std::string::npos);
        CHECK(st.project_view()->floors.size() == 2);
    }
    // The migrated file opens without a note.
    AppState st({}, paths);
    const Result r = st.open_project(file);
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(r.message.find("dropped") == std::string::npos);
}

TEST_CASE("New indoor project form: details, suggested file, replace through the browser") {
    const fs::path dir = scratch("form");
    AppPaths paths;
    paths.home = dir;
    AppState st({}, paths);
    REQUIRE(st.trigger(Action::NewProject).ok);
    NewProjectForm& f = st.new_project_form();
    CHECK(f.name == "New building");
    CHECK(f.file == dir / "New_building.yantra");
    f.name = "Depot";
    f.operator_name = "Some operator";
    f.notes = "Survey before handover";
    f.file = dir / "depot";
    REQUIRE(st.confirm_new_project().ok);
    CHECK(st.project()->meta().info.operator_name == "Some operator");
    CHECK(st.project()->meta().info.notes == "Survey before handover");
    REQUIRE(st.close_project().ok);

    // The same file again: refused unless the browser's overwrite question
    // was answered "Replace" for it.
    REQUIRE(st.trigger(Action::NewProject).ok);
    st.new_project_form().file = dir / "depot.yantra";
    CHECK_FALSE(st.confirm_new_project().ok);
    st.browse(BrowsePurpose::NewProjectFile);
    REQUIRE(st.browser() != nullptr);
    st.browser()->set_file_name("depot.yantra");
    const Result ask = st.browser_confirm();
    CHECK(ask.ok);
    REQUIRE(st.browser()->pending_overwrite());
    REQUIRE(st.browser_overwrite(true).ok);
    CHECK(st.dialog() == Dialog::NewProject);
    CHECK(st.new_project_form().file == dir / "depot.yantra");
    st.new_project_form().name = "Depot 2";
    REQUIRE(st.confirm_new_project().ok);
    CHECK(st.project()->meta().info.name == "Depot 2");
}

TEST_CASE("Open demo (DEMO-05): a copy in the user folder, kept until the installed project changes") {
    const fs::path dir = scratch("demo");
    const fs::path installed = dir / "share" / "demo" / "fraunces";
    fs::create_directories(installed / "recordings");
    make_project(installed / "fraunces.yantra", "Demo building");
    std::ofstream(installed / "recordings" / "walk.yrec") << "w";
    std::ofstream(installed / "stray.txt") << "not copied";

    AppPaths paths;
    paths.data_folders = {dir / "nothing", dir / "share"};
    paths.user_data = dir / "user";
    AppState st({}, paths);
    REQUIRE(st.action(Action::OpenDemo).enabled);
    Result r = st.trigger(Action::OpenDemo);
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(r.message.find("fresh copy") != std::string::npos);
    const fs::path copy = dir / "user" / "demo" / "fraunces";
    CHECK(st.screen() == Screen::Project);
    CHECK(st.project_view()->path == copy / "fraunces.yantra");
    CHECK(fs::exists(copy / "recordings" / "walk.yrec"));
    CHECK(fs::exists(copy / "installed.json"));
    CHECK_FALSE(fs::exists(copy / "stray.txt"));
    CHECK(st.demo());
    CHECK(st.project_view()->demo);
    CHECK(st.window_title() == "Yantra - Demo building [DEMO]");

    // The user's change stays in the copy: Open demo again (while it is
    // open) reopens the same copy.
    REQUIRE(st.add_floor("Added by the user", 5, 3.0).ok);
    r = st.open_demo();
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(r.message.find("your copy") != std::string::npos);
    CHECK(st.project_view()->floors.size() == 1);
    // The installed file is never opened or changed.
    CHECK(yantra::project::Project::open(installed / "fraunces.yantra")->floors().empty());
    REQUIRE(st.close_project().ok);

    // A changed installed project (an update) gives a fresh copy.
    fs::last_write_time(installed / "fraunces.yantra",
                        fs::last_write_time(installed / "fraunces.yantra") + std::chrono::seconds(10));
    r = st.open_demo();
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(r.message.find("fresh copy") != std::string::npos);
    CHECK(st.project_view()->floors.empty());

    // Without a user folder or an installed demo: refused with the reason.
    AppPaths no_user = paths;
    no_user.user_data.clear();
    AppState a({}, no_user);
    CHECK_FALSE(a.action(Action::OpenDemo).enabled);
    CHECK_FALSE(a.trigger(Action::OpenDemo).ok);
    bool refreshed = true;
    std::string error;
    CHECK(prepare_demo_copy(dir / "missing.yantra", dir / "x", refreshed, error).empty());
    CHECK_FALSE(refreshed);
    CHECK(error.find("not installed") != std::string::npos);
}

#ifdef YANTRA_TEST_DEMO_PROJECT
TEST_CASE("Open demo with the generated Fraunces demo project (DEMO-05)") {
    // The build tree as the app sees it: <build>/demo/fraunces/fraunces.yantra.
    const fs::path dir = scratch("fraunces");
    AppPaths paths;
    paths.data_folders = {fs::path(YANTRA_TEST_DEMO_PROJECT).parent_path().parent_path().parent_path()};
    paths.user_data = dir / "user";
    AppState st({}, paths);
    const Result r = st.trigger(Action::OpenDemo);
    REQUIRE_MESSAGE(r.ok, r.message);
    const auto v = st.project_view();
    REQUIRE(v);
    CHECK(v->name == "Fraunces Tavern (demo)");
    REQUIRE(v->floors.size() == 2);
    CHECK(v->floors[0].has_plan);
    CHECK(v->demo);
    CHECK(fs::is_directory(dir / "user" / "demo" / "fraunces" / "recordings"));
}
#endif

TEST_CASE("Help > User manual (PDF) (UI-14, DOC-02a): the shipped PDF goes to the system's viewer, greyed without it") {
    const fs::path dir = scratch("manual_pdf");
    const std::string name = manual_pdf_name(yantra::core::build_info().version_string);
    CHECK(name == "yantra-manual-" + std::string(yantra::core::build_info().version_string) + ".pdf");
    auto touch = [](const fs::path& f) {
        fs::create_directories(f.parent_path());
        std::ofstream(f, std::ios::binary) << "%PDF-1.7\n";
    };
    // Installed layout (.deb, Windows packages): share/doc/yantra next to share/yantra.
    fs::create_directories(dir / "share" / "yantra");
    AppPaths paths;
    paths.data_folders = {dir / "share" / "yantra"};
    {
        AppState st({}, paths);
        const ActionView v = st.action(Action::UserManualPdf);
        CHECK(v.label == "User manual (PDF)");
        CHECK_FALSE(v.enabled);
        CHECK(v.disabled_reason.find("not installed") != std::string::npos);  // DOC-07: says why
        bool called = false;
        st.set_document_opener([&](const fs::path&, std::string&) { return called = true; });
        CHECK_FALSE(st.trigger(Action::UserManualPdf).ok);
        CHECK_FALSE(called);
    }
    touch(dir / "share" / "doc" / "yantra" / name);
    {
        AppState st({}, paths);
        REQUIRE(st.action(Action::UserManualPdf).enabled);
        CHECK(fs::equivalent(st.manual_pdf(), dir / "share" / "doc" / "yantra" / name));
        fs::path opened;
        st.set_document_opener([&](const fs::path& f, std::string&) {
            opened = f;
            return true;
        });
        const Result r = st.trigger(Action::UserManualPdf);
        CHECK(r.ok);
        CHECK(fs::equivalent(opened, dir / "share" / "doc" / "yantra" / name));
        CHECK(st.status().find(name) != std::string::npos);
        // The viewer cannot start: the status line says why.
        st.set_document_opener([](const fs::path&, std::string& error) {
            error = "no viewer";
            return false;
        });
        const Result bad = st.trigger(Action::UserManualPdf);
        CHECK_FALSE(bad.ok);
        CHECK(bad.message.find("no viewer") != std::string::npos);
        CHECK(st.status_is_error());
    }
    // A build tree: <build>/docs/manual (ninja docs-manual) or the docs site.
    const fs::path build = dir / "build";
    CHECK_FALSE(find_manual_pdf({build}, "9.9.9"));
    touch(build / "docs" / "site" / "docs" / "manual" / "yantra-manual-9.9.9.pdf");
    REQUIRE(find_manual_pdf({build}, "9.9.9"));
    CHECK(find_manual_pdf({build}, "9.9.9")->parent_path().filename() == "manual");
    touch(build / "docs" / "manual" / "yantra-manual-9.9.9.pdf");
    CHECK(fs::equivalent(*find_manual_pdf({build}, "9.9.9"), build / "docs" / "manual" / "yantra-manual-9.9.9.pdf"));
    CHECK_FALSE(find_manual_pdf({build}, "9.9.8"));  // another version's PDF is not this program's manual
    fs::remove_all(dir);
}
