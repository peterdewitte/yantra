// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File > Export, File > Report, the acceptance profile editor and the
// sharing settings (step 10.4; EXP-01, EXP-03..08) driven through the
// headless UI harness (TST-50) on the demo project, as a user clicks.
#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/project/project.h"
#include "yantra/core/survey/acceptance.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::test::UiHarness;

#ifdef YANTRA_TEST_DEMO_DATA_DIR
namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "report" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

AppPaths demo_paths(const fs::path& dir) {
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.data_folders = {YANTRA_TEST_DEMO_DATA_DIR, YANTRA_TEST_SOURCE_DATA_DIR};
    p.user_data = dir / "data";
    p.home = dir / "home";
    fs::create_directories(p.home);
    const fs::path folder = dir / "plugins";
    fs::create_directories(folder);
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "tetra" YANTRA_PLUGIN_SUFFIX, folder / "tetra" YANTRA_PLUGIN_SUFFIX,
                  fs::copy_options::overwrite_existing);
    p.radios.plugin_folders = {folder};
    p.radios.plugin_state = dir / "config" / "plugin_state.json";
    return p;
}

struct Shell {
    explicit Shell(AppPaths paths) : state({}, std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        ui.set_display_size(1400.0F, 1400.0F);
        Gui::apply_style(Gui::style_of(state.settings()));
        ui.set_draw([this] { gui.draw(); });
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

void open_demo(Shell& s) {
    REQUIRE(s.ui.click("start.file.open_demo"));
    REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
    REQUIRE(s.ui.frames(3));
}

// Types into a field and leaves it (Tab), so Enter does not confirm.
void enter(Shell& s, const char* id, const std::string& text, const char* window) {
    REQUIRE_MESSAGE(s.ui.fill(id, text, window), s.ui.dump());
    s.ui.key(ImGuiKey_Tab);
    REQUIRE(s.ui.frames(2));
}

std::string read(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The background job's end, as the frames see it.
bool job_done(Shell& s) {
    if (s.state.output() == nullptr) return false;
    s.state.output()->wait();
    return s.ui.frames(3) && !s.state.output()->busy();
}

}  // namespace

TEST_CASE("File > Export by clicking: DXF and CSV with a chosen separator, sent to a shared folder (EXP-01/03/07)") {
    const fs::path dir = scratch("export");
    Shell s(demo_paths(dir));
    open_demo(s);
    REQUIRE_MESSAGE(s.ui.click_menu("file", "file.export"), s.ui.dump());
    REQUIRE(s.ui.wait_window("dlg.export"));
    REQUIRE(s.ui.has("ex.col.time", "dlg.export"));
    REQUIRE(s.ui.click("ex.dxf", "dlg.export"));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.ui.has("ex.col.time", "dlg.export"));  // no columns for a drawing
    const fs::path dxf = s.state.output()->export_form().file;
    CHECK(dxf.extension() == ".dxf");
    REQUIRE(s.ui.click("dlg.ok", "dlg.export"));
    REQUIRE(s.ui.wait_window_closed("dlg.export"));
    INFO(s.state.status());
    CHECK_FALSE(s.state.status_is_error());
    CHECK(fs::exists(dxf));

    // CSV without the level column, semicolons, then sent to a folder.
    fs::create_directories(dir / "shared");
    REQUIRE(s.ui.click_menu("file", "file.export"));
    REQUIRE(s.ui.wait_window("dlg.export"));
    REQUIRE(s.ui.click("ex.csv", "dlg.export"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("ex.col.level", "dlg.export"));
    REQUIRE(s.ui.click("out.share", "dlg.export"));
    REQUIRE(s.ui.click("out.sharing", "dlg.export"));
    REQUIRE(s.ui.wait_window("dlg.sharing"));
    REQUIRE(s.ui.click("sh.folder", "dlg.sharing"));
    REQUIRE(s.ui.frames(2));
    enter(s, "sh.folder_path", (dir / "shared").string(), "dlg.sharing");
    REQUIRE(s.ui.click("dlg.ok", "dlg.sharing"));
    REQUIRE(s.ui.wait_window("dlg.export"));
    CHECK(s.state.settings().sharing.folder == dir / "shared");
    s.state.output()->export_form().separator = 1;  // the combo's second item
    const fs::path csv = s.state.output()->export_form().file;
    CHECK(csv.extension() == ".csv");
    REQUIRE(s.ui.click("dlg.ok", "dlg.export"));
    REQUIRE(s.ui.wait_window_closed("dlg.export"));
    REQUIRE(job_done(s));
    INFO(s.state.status());
    CHECK_FALSE(s.state.status_is_error());
    const std::string text = read(csv);
    const std::string head = text.substr(0, text.find('\n'));
    CHECK(head.find(';') != std::string::npos);
    CHECK((";" + head + ";").find(";rssi;") == std::string::npos);
    CHECK((";" + head + ";").find(";time_utc;") != std::string::npos);
    CHECK(read(dir / "shared" / csv.filename()) == text);
}

TEST_CASE("File > Report by clicking: surveyor, a new acceptance profile, one floor, PDF and HTML (EXP-05/06)") {
    const fs::path dir = scratch("report");
    Shell s(demo_paths(dir));
    open_demo(s);
    REQUIRE_MESSAGE(s.ui.click_menu("file", "file.report"), s.ui.dump());
    REQUIRE(s.ui.wait_window("dlg.report"));
    enter(s, "rp.surveyor", "A. Surveyor", "dlg.report");
    CHECK(s.state.output()->report_form().surveyor == "A. Surveyor");

    // The profile editor: add one, rename it, save.
    REQUIRE(s.ui.click("rp.edit_profiles", "dlg.report"));
    REQUIRE(s.ui.wait_window("dlg.profiles"));
    const std::size_t before = s.state.output()->profile_form().profiles.size();
    REQUIRE(s.ui.click("ap.add", "dlg.profiles"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.state.output()->profile_form().profiles.size() == before + 1);
    enter(s, "ap.name", "Lobby", "dlg.profiles");
    REQUIRE(s.ui.click("ap.crit_add", "dlg.profiles"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("dlg.ok", "dlg.profiles"));
    REQUIRE(s.ui.wait_window("dlg.report"));
    INFO(s.state.status());
    REQUIRE(yantra::survey::acceptance_profiles(*s.state.project()).size() == before + 1);
    CHECK(yantra::survey::acceptance_profiles(*s.state.project()).back().name == "Lobby");

    // One floor only (the report of every floor is the sample's ctest).
    const auto floors = s.state.output()->floor_names();
    REQUIRE(floors.size() >= 2);
    for (std::size_t i = 1; i < floors.size(); ++i)
        REQUIRE(s.ui.click("rp.floor." + floors[i].first, "dlg.report"));
    REQUIRE(s.ui.frames(2));
    const fs::path pdf = s.state.output()->report_form().file;
    REQUIRE(s.ui.click("dlg.ok", "dlg.report"));
    REQUIRE(s.ui.wait_window_closed("dlg.report"));
    REQUIRE(job_done(s));
    INFO(s.state.status());
    CHECK_FALSE(s.state.status_is_error());
    REQUIRE(fs::exists(pdf));
    fs::path html = pdf;
    html.replace_extension(".html");
    const std::string page = read(html);
    CHECK(page.find("A. Surveyor") != std::string::npos);
    CHECK(page.find("Lobby") != std::string::npos);
    CHECK(page.find(floors[0].second) != std::string::npos);

    // UI-22: the Report ready popup lists both files; Open opens the PDF
    // in the system viewer and closes the popup.
    REQUIRE_MESSAGE(s.ui.wait_window("dlg.report_ready"), s.ui.dump());
    CHECK(s.state.dialog() == Dialog::ReportReady);
    REQUIRE(s.state.report_ready() == std::vector<fs::path>{pdf, html});
    std::vector<fs::path> opened;
    s.state.set_document_opener([&](const fs::path& f, std::string&) {
        opened.push_back(f);
        return true;
    });
    REQUIRE(s.ui.click("rr.open", "dlg.report_ready"));
    REQUIRE(s.ui.wait_window_closed("dlg.report_ready"));
    CHECK(opened == std::vector<fs::path>{pdf});
    CHECK(s.state.dialog() == Dialog::None);
    CHECK(s.state.report_ready().empty());
}

namespace {

// Makes a report with only PDF or only HTML ticked and waits for the
// Report ready popup (UI-22).
fs::path report_one_kind(Shell& s, bool pdf) {
    REQUIRE_MESSAGE(s.ui.click_menu("file", "file.report"), s.ui.dump());
    REQUIRE(s.ui.wait_window("dlg.report"));
    ReportForm& f = s.state.output()->report_form();
    if (f.pdf != pdf) REQUIRE(s.ui.click("rp.pdf", "dlg.report"));
    if (f.html == pdf) REQUIRE(s.ui.click("rp.html", "dlg.report"));
    // One floor keeps the test short.
    const auto floors = s.state.output()->floor_names();
    for (std::size_t i = 1; i < floors.size(); ++i)
        if (f.floors.size() > i && f.floors[i]) REQUIRE(s.ui.click("rp.floor." + floors[i].first, "dlg.report"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(f.pdf == pdf);
    REQUIRE(f.html == !pdf);
    fs::path file = f.file;
    file.replace_extension(pdf ? ".pdf" : ".html");
    REQUIRE(s.ui.click("dlg.ok", "dlg.report"));
    REQUIRE(s.ui.wait_window_closed("dlg.report"));
    REQUIRE(job_done(s));
    REQUIRE_MESSAGE(s.ui.wait_window("dlg.report_ready"), s.ui.dump());
    return file;
}

}  // namespace

TEST_CASE("UI-22 Report ready: an HTML report with Show in folder, a PDF report with Close") {
    const fs::path dir = scratch("report_ready");
    Shell s(demo_paths(dir));
    open_demo(s);
    std::vector<fs::path> opened;
    s.state.set_document_opener([&](const fs::path& f, std::string&) {
        opened.push_back(f);
        return true;
    });

    // HTML only: the popup lists it; Show in folder opens its folder.
    const fs::path html = report_one_kind(s, false);
    REQUIRE(s.state.report_ready() == std::vector<fs::path>{html});
    CHECK(fs::exists(html));
    REQUIRE(s.ui.click("rr.folder", "dlg.report_ready"));
    REQUIRE(s.ui.wait_window_closed("dlg.report_ready"));
    CHECK(opened == std::vector<fs::path>{html.parent_path()});
    CHECK_FALSE(s.state.status_is_error());

    // PDF only: Close just closes the popup, nothing is opened.
    opened.clear();
    const fs::path pdf = report_one_kind(s, true);
    REQUIRE(s.state.report_ready() == std::vector<fs::path>{pdf});
    CHECK(fs::exists(pdf));
    REQUIRE(s.ui.click("dlg.cancel", "dlg.report_ready"));
    REQUIRE(s.ui.wait_window_closed("dlg.report_ready"));
    CHECK(opened.empty());
    CHECK(s.state.report_ready().empty());
    CHECK(s.state.dialog() == Dialog::None);

    // A viewer that fails keeps the popup with the error.
    s.state.set_document_opener([](const fs::path&, std::string& error) {
        error = "no viewer";
        return false;
    });
    (void)report_one_kind(s, true);
    REQUIRE(s.ui.click("rr.open", "dlg.report_ready"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.state.dialog() == Dialog::ReportReady);
    CHECK(s.state.status_is_error());
    CHECK(s.state.status().find("no viewer") != std::string::npos);
}
#endif
