// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI lint at run time (DOC-20, DOC-07, TST-50, UI-12, UI-13): every
// window of the app, drawn in every UI language, in the desktop layout
// and in the touch layout: each control has a tooltip, each disabled one
// its reason, nothing overlaps, is cut off or squashed, touch targets are
// at least 48 px, and every text has its translation (tests/app/ui/ui_lint.h).
// TST-50's layout check runs the same visit at the other supported window
// sizes and scales (step 9.11).
#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "gui.h"
#include "plan_fixtures.h"
#include "ui_harness.h"
#include "ui_lint.h"
#include "yantra/app/app_state.h"
#include "yantra/app/i18n.h"
#include "yantra/app/settings.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/project/project.h"

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
#ifdef YANTRA_TEST_HAVE_PROBE
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "probe.so", folder / "probe.so",
                  fs::copy_options::overwrite_existing);
#endif
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
    // The strip is the same in every dialog (the plan import window with
    // it is in test_ui_tour.cpp; decoding the large sheet here 8 times made
    // the sanitizer run too slow).
    REQUIRE(s.state.trigger(Action::AddFloor).ok);
    settle_check("tour strip in add floor");
    s.state.tour().leave();
    s.state.cancel_dialog();
    REQUIRE(s.state.close_project().ok);
    REQUIRE(s.ui.frames(2));

    // IND-11: the wall editor with proposed walls (a small plan: the demo's
    // sheet is too slow to look at in every language and layout).
    {
        namespace fx = yantra::plan::fixtures;
        namespace fs = std::filesystem;
        const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / "lint_proposals" / tag;
        fs::remove_all(dir);
        fs::create_directories(dir);
        yantra::plan::Raster r(500, 300, 255);
        for (int x = 40; x < 460; ++x)
            for (int y : {60, 61, 62, 200, 201, 202}) {
                std::uint8_t* px = r.pixel(x, y);
                px[0] = px[1] = px[2] = 0;
            }
        const fx::Bytes bytes = fx::png(r);
        std::ofstream(dir / "plan.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        {
            auto p = yantra::project::Project::create(dir / "w.yantra", yantra::project::ProjectInfo{"Walls", "", "", ""});
            yantra::building::Floor f;
            f.name = "Ground floor";
            f.plan_width = 500;
            f.plan_height = 300;
            f.source = p->add_source(dir / "plan.png").id;
            f.trim = yantra::plan::whole_page_trim({500, 300});
            f.scale.method = yantra::building::Scale::Method::Factor;
            f.scale.metres_per_unit = 0.02;
            (void)p->add_floor(f);
            p->close();
        }
        REQUIRE(s.state.open_project(dir / "w.yantra").ok);
        REQUIRE(s.ui.frames(3));
        REQUIRE_MESSAGE(s.ui.click("panel.edit"), s.ui.dump());
        settle_check("edit before proposing");
        REQUIRE(s.state.wall_proposals()->start().ok);
        settle_check("edit while proposing");
        s.state.wall_proposals()->wait();
        settle_check("edit with proposed walls");
        REQUIRE(s.state.wall_proposals()->view().proposals.size() == 2);
        REQUIRE(s.state.close_project().ok);
        REQUIRE(s.ui.frames(2));
    }

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

struct LintRun {
    const char* tag;
    bool touch = false;
    float width = 1280.0F, height = 800.0F;  // a 10" tablet, a laptop
    double scale = 1.0;
    bool all_languages = true;
};

void run_lint(const LintRun& run) {
    std::string all;
    int frames = 0;
    for (Language l : kLanguages) {
        if (!run.all_languages && l != Language::En) continue;
        AppSettings settings;
        settings.language = language_key(l);
        settings.touch = run.touch;
        settings.ui_scale = run.scale;
        Shell s(lint_paths(scratch(std::string(run.tag) + "_" + language_key(l))), settings);
        s.ui.set_display_size(run.width, run.height);
        REQUIRE(s.ui.frames(3));
        UiLint lint{s.ui, run.touch};
        visit_all(s, lint, std::string(language_key(l)) + " " + run.tag);
        frames += lint.frames_checked;
        for (const std::string& i : lint.issues) all += i + "\n";
    }
    INFO("frames checked: " << frames);
    CHECK_MESSAGE(all.empty(), all);
    CHECK(frames > 30);
}

}  // namespace

TEST_CASE("DOC-20 UI lint: every window in every language, desktop layout") { run_lint({"desktop"}); }

TEST_CASE("DOC-20 UI lint: every window in every language, touch layout (UI-13)") {
    run_lint({"touch", true});
}

// TST-50 layout check: the same windows at the other supported window
// sizes and display scales (UI-13), in English (the languages' texts
// are checked at the default size above): nothing overlaps, is cut off,
// squashed or shortened.
TEST_CASE("TST-50 layout check: a small window (1024 x 640)") {
    run_lint({"small", false, 1024.0F, 640.0F, 1.0, false});
}

TEST_CASE("TST-50 layout check: a full-HD screen at 150 % scale") {
    run_lint({"scale150", false, 1920.0F, 1080.0F, 1.5, false});
}

TEST_CASE("TST-50 layout check: a 7-inch tablet (1024 x 600), touch layout") {
    run_lint({"tablet7", true, 1024.0F, 600.0F, 1.0, false});
}
