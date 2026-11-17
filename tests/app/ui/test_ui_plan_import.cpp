// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plan import window (step 9.3) driven through the headless UI harness
// (TST-50): Add floor -> choose a plan file in the file browser -> the
// plan import window; a rectangle dragged on the sheet, the live preview,
// Add as floor, a scale from two points, Close back to the project window
// and "Plan and scale..." to come back (IND-01..IND-10); PDF page
// thumbnails and page choice; an unreadable file's reason; the credit of a
// licence-restricted plan (IND-03). Fixtures are written by code.
#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#include "gui.h"
#include "plan_fixtures.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/plan/source.h"
#include "yantra/core/project/project.h"
#include "yantra/core/util/sha256.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace pl = yantra::plan;
namespace fx = yantra::plan::fixtures;
using namespace yantra::app;
using yantra::building::Point2;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "plan_import" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void write(const fs::path& file, const fx::Bytes& bytes) {
    std::ofstream(file, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

struct Shell {
    explicit Shell(AppPaths paths) : state({}, std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style();
        gui.set_wait_for_tiles(true);
        ui.set_draw([this] { gui.draw(); });
        // Tall enough for the whole side panel (it scrolls on smaller
        // windows; clipped items cannot be clicked).
        ui.set_display_size(1600.0F, 1500.0F);
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};

    /// A new project; the Add floor dialog is open (IND-01).
    void new_project(const fs::path& dir) {
        REQUIRE(state.new_project(dir / "p", pj::ProjectInfo{"P", "", "", ""}).ok);
        REQUIRE(ui.wait_window("dlg.add_floor"));
    }
    /// Add floor > Choose a plan file > type its path > Open.
    void choose(const fs::path& file) {
        REQUIRE(ui.click("af.plan_file", "dlg.add_floor"));
        REQUIRE(ui.wait_window("dlg.browse"));
        REQUIRE(ui.fill("fb.name", file.string(), "dlg.browse"));
        REQUIRE(ui.click("dlg.ok", "dlg.browse"));
        REQUIRE(ui.wait_window_closed("dlg.browse"));
        REQUIRE(ui.frames(2));
    }
    /// The screen position of a page point on the import canvas.
    ImVec2 at(Point2 page) {
        PlanImportWindow* w = state.plan_import();
        REQUIRE(w != nullptr);
        const Point2 s = w->view().page_to_screen(page);
        const ImVec2 o = gui.import_view().stats().canvas_origin;
        return ImVec2(o.x + static_cast<float>(s.x), o.y + static_cast<float>(s.y));
    }
    void drag(Point2 a, Point2 b) {
        const ImVec2 sa = at(a), sb = at(b);
        ui.drag(sa.x, sa.y, sb.x, sb.y);
        REQUIRE(ui.frames(2));
    }
};

std::string sha_hex(const std::string& bytes) {
    yantra::util::Sha256 h;
    h.update(bytes);
    return h.finish_hex();
}

}  // namespace

TEST_CASE("plan import: choose a file, trim, preview, add as floor, scale, close and come back") {
    const fs::path d = scratch("flow");
    write(d / "sheet.png", fx::png(fx::pattern(400, 300)));
    AppPaths paths;
    paths.home = d;
    Shell s(paths);
    s.new_project(d);
    REQUIRE(s.ui.has("af.plan_file", "dlg.add_floor"));
    CHECK_FALSE(s.ui.find("af.plan_file", "dlg.add_floor")->disabled);
    s.choose(d / "sheet.png");
    REQUIRE_MESSAGE(s.ui.wait_window("dlg.plan_import"), s.ui.dump());
    REQUIRE(s.ui.frames(3));
    PlanImportWindow* w = s.state.plan_import();
    REQUIRE(w != nullptr);
    const ImportViewStats& st = s.gui.import_view().stats();
    CHECK(st.tiles > 0);
    CHECK(st.preview);  // the whole page, fitted
    CHECK(st.preview_width == 240);
    CHECK_FALSE(st.credit);
    // UI-20: the file's details behind Info, Scale and Alignment closed.
    CHECK(s.ui.has("pi.info", "dlg.plan_import"));
    CHECK_FALSE(s.ui.has("pi.tool.scale_line", "dlg.plan_import"));
    CHECK_FALSE(s.ui.has("pi.tool.reference", "dlg.plan_import"));
    REQUIRE(s.ui.click("pi.scale", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.ui.find("pi.tool.scale_line", "dlg.plan_import")->disabled);  // no floor yet
    REQUIRE(s.ui.click("pi.align", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.ui.find("pi.tool.reference", "dlg.plan_import")->disabled);  // no floor yet
    REQUIRE(s.ui.click("pi.align", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));

    // Rectangle: drag it on the sheet; the preview follows.
    REQUIRE(s.ui.click("pi.tool.rectangle", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK(w->tool() == ImportTool::Rectangle);
    s.drag(Point2{20, 30}, Point2{220, 230});
    CHECK(w->import().trim().shape == pl::Trim::Shape::Rectangle);
    CHECK(w->import().trim_size().width == doctest::Approx(200).epsilon(0.02));
    CHECK(w->import().trim_size().height == doctest::Approx(200).epsilon(0.02));
    REQUIRE(s.ui.frames(2));
    CHECK(st.preview);
    CHECK(st.preview_height == doctest::Approx(240).epsilon(0.03));  // square now

    // Turn right and back: the preview and trim follow.
    REQUIRE(s.ui.click("pi.rot_right", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK(w->quarter_turns() == 1);
    REQUIRE(s.ui.click("pi.rot_left", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK(w->quarter_turns() == 0);

    // Add as floor: a popup asks for the name and level (UI-20); Add; the
    // outline is drawn on the sheet. Cancel first leaves the project as it is.
    REQUIRE(s.ui.click("pi.add", "dlg.plan_import"));
    REQUIRE(s.ui.wait_window("pi.add_popup"));
    CHECK(s.ui.has("pi.add_level", "pi.add_popup"));
    CHECK(s.ui.has("pi.add_height", "pi.add_popup"));
    REQUIRE(s.ui.click("pi.add_cancel", "pi.add_popup"));
    REQUIRE(s.ui.wait_window_closed("pi.add_popup"));
    CHECK(w->added().empty());
    CHECK(w->add_form() == nullptr);
    REQUIRE(s.ui.click("pi.add", "dlg.plan_import"));
    REQUIRE(s.ui.wait_window("pi.add_popup"));
    REQUIRE(s.ui.fill("pi.add_name", "Hall", "pi.add_popup"));
    REQUIRE(s.ui.click("pi.add_ok", "pi.add_popup"));
    REQUIRE(s.ui.wait_window_closed("pi.add_popup"));
    REQUIRE(s.ui.frames(1));
    REQUIRE(w->added().size() == 1);
    const std::string hall = w->added()[0];
    CHECK(s.state.project()->floor(hall)->name == "Hall");
    CHECK(st.placed == 1);
    CHECK(s.ui.has("pi.floor." + hall, "dlg.plan_import"));

    // Scale from two points on the floor.
    REQUIRE(s.ui.click("pi.floor." + hall, "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.ui.find("pi.tool.scale_line", "dlg.plan_import")->disabled);
    CHECK(s.ui.find("pi.ratio", "dlg.plan_import")->disabled);  // a PNG without resolution
    REQUIRE(s.ui.click("pi.tool.scale_line", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    s.drag(Point2{40, 100}, Point2{140, 100});
    REQUIRE(w->scale_form().a);
    REQUIRE(s.ui.fill("pi.distance", "5", "dlg.plan_import"));
    REQUIRE(s.ui.click("pi.scale_apply", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    const auto f = s.state.project()->floor(hall);
    REQUIRE(f);
    const auto mpu = s.state.project()->building().metres_per_unit(*f);
    REQUIRE(mpu);
    CHECK(*mpu == doctest::Approx(0.05).epsilon(0.03));
    CHECK_FALSE(s.gui.import_view().message_is_error());

    // Close: the project window shows the floor; Plan and properties
    // reopens it (UI-23: the one floor button; no Properties button).
    REQUIRE(s.ui.click("dlg.cancel", "dlg.plan_import"));
    REQUIRE(s.ui.wait_window_closed("dlg.plan_import"));
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.state.project_window());
    CHECK(s.state.project_window()->selected() == hall);
    CHECK(s.ui.has("plan.canvas"));
    REQUIRE(s.ui.has("floor.plan"));
    CHECK_FALSE(s.ui.find("floor.plan")->disabled);
    REQUIRE(s.ui.click("floor.plan"));
    REQUIRE(s.ui.wait_window("dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.state.plan_import());
    CHECK(s.state.plan_import()->selected() == hall);
    CHECK(s.gui.import_view().stats().placed == 1);
    // UI-23: the floor's properties are in the window; Apply stores them
    // and the window stays open.
    REQUIRE_MESSAGE(s.ui.has("pi.prop.name", "dlg.plan_import"), s.ui.dump());
    CHECK(s.state.floor_properties_form().floor_id == hall);
    const std::string hall_name = s.state.project()->floor(hall)->name;
    CHECK(s.state.floor_properties_form().name == hall_name);
    REQUIRE(s.ui.fill("pi.prop.name", "Main hall", "dlg.plan_import"));
    REQUIRE(s.ui.fill("pi.prop.height", "4.5", "dlg.plan_import"));
    REQUIRE(s.ui.click("pi.prop.apply", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.project()->floor(hall)->name == "Main hall");
    CHECK(s.state.project()->floor(hall)->height_m == doctest::Approx(4.5));
    CHECK(s.ui.has("dlg.plan_import"));
    CHECK(s.state.plan_import()->selected() == hall);
    s.ui.key(ImGuiKey_Escape);  // nothing drafted: Escape is not a close
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("dlg.cancel", "dlg.plan_import"));
    REQUIRE(s.ui.wait_window_closed("dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.ui.has("floor.properties"));
    CHECK(s.ui.find("floor.plan")->label.find("Plan and properties") == 0);

    // Add floor lists the plan already in the project.
    REQUIRE(s.state.trigger(Action::AddFloor).ok);
    REQUIRE(s.ui.wait_window("dlg.add_floor"));
    const auto sources = s.state.plan_sources();
    REQUIRE(sources.size() == 1);
    REQUIRE(s.ui.click("af.src." + sources[0].id, "dlg.add_floor"));
    REQUIRE(s.ui.wait_window("dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.plan_import()->placed().size() == 1);
}

TEST_CASE("plan import: an unreadable file gives its reason in Add floor") {
    const fs::path d = scratch("broken");
    std::ofstream(d / "broken.png") << "not an image";
    AppPaths paths;
    paths.home = d;
    Shell s(paths);
    s.new_project(d);
    s.choose(d / "broken.png");
    REQUIRE(s.ui.wait_window("dlg.add_floor"));
    CHECK_FALSE(s.ui.window_open("dlg.plan_import"));
    CHECK(s.state.status_is_error());
    CHECK(s.state.status().find("broken.png") != std::string::npos);
    CHECK(s.state.project()->sources().empty());
}

TEST_CASE("plan import: PDF pages as thumbnails, page choice (IND-02)") {
    if (!pl::pdf_supported()) return;
    const fs::path d = scratch("pdf");
    write(d / "plans.pdf", fx::pdf({fx::PdfPage{}, fx::PdfPage{842.0, 595.0, 0, {}}}));
    AppPaths paths;
    paths.home = d;
    Shell s(paths);
    s.new_project(d);
    s.choose(d / "plans.pdf");
    REQUIRE(s.ui.wait_window("dlg.plan_import"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.gui.import_view().stats().thumbnails == 2);
    REQUIRE(s.ui.has("pi.page.1", "dlg.plan_import"));
    REQUIRE(s.ui.click("pi.scale", "dlg.plan_import"));  // closed by default (UI-20)
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("pi.ratio", "dlg.plan_import"), s.ui.dump());
    CHECK(s.ui.find("pi.ratio", "dlg.plan_import")->disabled);  // no floor selected yet
    REQUIRE(s.ui.click("pi.page.1", "dlg.plan_import"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.state.plan_import()->page() == 1);
    CHECK(s.gui.import_view().stats().tiles > 0);
    CHECK(s.gui.import_view().stats().preview_width == 240);  // landscape: full width
    CHECK(s.gui.import_view().stats().preview_height < 240);
}

TEST_CASE("plan import: a licence-restricted plan shows its credit and licence behind Info (IND-03, UI-20)") {
    const fs::path d = scratch("restricted");
    const fs::path file = d / "cache" / "example_sheet.png";
    fs::create_directories(file.parent_path());
    write(file, fx::png(pl::Raster(1680, 1565, 255)));  // uniform: cheap to encode and draw
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const fs::path data = d / "data";
    fs::create_directories(data / "demo" / "mak");
    nlohmann::json j;
    std::ifstream(fs::path(YANTRA_TEST_SOURCE_DATA_DIR) / "demo" / "mak" / "mak_beer_house.json") >> j;
    j["sheets"][0]["file"] = "example_sheet.png";
    j["sheets"][0]["sha256"] = sha_hex(bytes);
    j["sheets"][0]["size_bytes"] = bytes.size();
    j["sheets"][0]["media_type"] = "image/png";
    std::ofstream(data / "demo" / "mak" / "example.json") << j.dump(2);
    fs::copy_file(fs::path(YANTRA_TEST_SOURCE_DATA_DIR) / "demo" / "mak" / "cells.csv",
                  data / "demo" / "mak" / "cells.csv");

    AppPaths paths;
    paths.home = d;
    paths.data_folders = {data};
    Shell s(paths);
    s.new_project(d);
    s.choose(file);
    REQUIRE(s.ui.wait_window("dlg.plan_import"));
    REQUIRE(s.ui.frames(3));
    // UI-20: the credit, licence and notes are behind Info, closed by default.
    CHECK_FALSE(s.gui.import_view().stats().credit);
    REQUIRE(s.ui.click("pi.info", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.gui.import_view().stats().credit);
    REQUIRE(s.ui.click("pi.info", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.gui.import_view().stats().credit);
    REQUIRE(s.state.plan_import());
    CHECK(s.state.plan_import()->referenced());
    CHECK_FALSE(s.state.project()->sources()[0].plan.embedded);
}

TEST_CASE("plan import: fits a small window (UI-13); the panel scrolls") {
    const fs::path d = scratch("small");
    write(d / "sheet.png", fx::png(fx::pattern(400, 300)));
    AppPaths paths;
    paths.home = d;
    Shell s(paths);
    s.ui.set_display_size(800.0F, 600.0F);
    REQUIRE(s.ui.frames(2));
    s.new_project(d);
    s.choose(d / "sheet.png");
    REQUIRE(s.ui.wait_window("dlg.plan_import"));
    REQUIRE(s.ui.frames(3));
    const auto close = s.ui.find("dlg.cancel", "dlg.plan_import");
    REQUIRE(close);
    CHECK(close->x1 <= 800.0F);
    CHECK(close->y1 <= 600.0F);
    const auto canvas = s.ui.find("pi.canvas", "dlg.plan_import");
    REQUIRE(canvas);
    CHECK(canvas->x1 <= 800.0F);
    CHECK(canvas->y1 - canvas->y0 > 200.0F);
    CHECK(s.gui.import_view().stats().tiles > 0);
}
