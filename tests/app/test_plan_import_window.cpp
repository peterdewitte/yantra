// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plan import window's editing state (TST-50; IND-02, IND-03,
// IND-05..IND-10): opening files and sources, trim tools (rectangle,
// polygon, corner drags), rotation and deskew, the live preview, Add as
// floor, editing and removing trims, scale entry (two points, scale bar,
// drawing scale) and sharing, the reference point, licence-restricted
// sources, and the AppState hand-off from Add floor. Fixtures are written
// by code (tests/plan/fixtures); no third-party plans.
// Copied from yantra-v2 68ea20a: tests/app/test_plan_import_window.cpp,
// adapted (string ids, building::Scale, drawing scale, preview,
// restricted sources).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "plan_fixtures.h"
#include "yantra/app/app_state.h"
#include "yantra/app/plan_import_window.h"
#include "yantra/core/plan/source.h"
#include "yantra/core/project/project.h"
#include "yantra/core/util/sha256.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace pl = yantra::plan;
namespace bm = yantra::building;
namespace fx = yantra::plan::fixtures;
using namespace yantra::app;
using yantra::building::Point2;

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

/// A 400 x 300 px sheet (page units = pixels).
fs::path sheet(const fs::path& d, const std::string& name = "sheet.png") {
    write(d / name, fx::png(fx::pattern(400, 300)));
    return d / name;
}

std::unique_ptr<pj::Project> project_in(const fs::path& d) {
    pj::ProjectInfo info;
    info.name = "Plans";
    return pj::Project::create(d / "plans.yantra", info);
}

std::unique_ptr<PlanImportWindow> open_sheet(pj::Project& p, const fs::path& d) {
    std::string error;
    auto w = PlanImportWindow::open_file(p, sheet(d), {}, error);
    REQUIRE_MESSAGE(w, error);
    return w;
}

void drag(PlanImportWindow& w, Point2 a, Point2 b) {
    (void)w.pointer_down(a);
    w.pointer_move(Point2{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0});
}

Result drag_up(PlanImportWindow& w, Point2 a, Point2 b) {
    drag(w, a, b);
    return w.pointer_up(b);
}

Result click(PlanImportWindow& w, Point2 p) {
    (void)w.pointer_down(p);
    return w.pointer_up(p);
}

std::string add_floor(PlanImportWindow& w, const std::string& name, int level) {
    w.begin_add_floor();
    ImportFloorForm* form = w.add_form();
    if (form == nullptr) throw std::logic_error("begin_add_floor opened no form");
    form->name = name;
    form->level = level;
    const Result r = w.confirm_add_floor();
    REQUIRE_MESSAGE(r.ok, r.message);
    return w.selected();  // the floor just added
}

std::string sha_hex(const std::string& bytes) {
    yantra::util::Sha256 h;
    h.update(bytes);
    return h.finish_hex();
}

double mpu(const pj::Project& p, const std::string& floor_id) {
    const auto f = p.floor(floor_id);
    REQUIRE(f);
    const auto m = p.building().metres_per_unit(*f);
    return m.value_or(0.0);
}

}  // namespace

TEST_CASE("parse_distance_m and parse_ratio accept values as typed and say what is wrong") {
    std::string e;
    CHECK(parse_distance_m("3", e) == doctest::Approx(3.0));
    CHECK(parse_distance_m(" 2,5 ", e) == doctest::Approx(2.5));
    CHECK(parse_distance_m("4.25 m", e) == doctest::Approx(4.25));
    CHECK_FALSE(parse_distance_m("", e));
    CHECK(e.find("metres") != std::string::npos);
    CHECK_FALSE(parse_distance_m("abc", e));
    CHECK(e.find("not a number") != std::string::npos);
    CHECK_FALSE(parse_distance_m("0", e));
    CHECK_FALSE(parse_distance_m("-2", e));
    CHECK_FALSE(parse_distance_m("inf", e));
    CHECK_FALSE(parse_distance_m("3 mm", e));

    CHECK(parse_ratio("100", e) == doctest::Approx(100.0));
    CHECK(parse_ratio("1:50", e) == doctest::Approx(50.0));
    CHECK(parse_ratio(" 1 : 200 ", e) == doctest::Approx(200.0));
    CHECK_FALSE(parse_ratio("2:100", e));
    CHECK(e.find("1:N") != std::string::npos);
    CHECK_FALSE(parse_ratio("", e));
    CHECK_FALSE(parse_ratio("0.5", e));
    CHECK_FALSE(parse_ratio("x", e));
}

TEST_CASE("Opening (IND-02, IND-03): unreadable files give the reason and are not kept") {
    const fs::path d = scratch("open");
    auto p = project_in(d);
    std::ofstream(d / "broken.png") << "not an image";
    std::string error;
    CHECK_FALSE(PlanImportWindow::open_file(*p, d / "broken.png", {}, error));
    CHECK(error.find("broken.png") != std::string::npos);
    CHECK(error.find("Cannot read") != std::string::npos);
    CHECK(p->sources().empty());
    CHECK_FALSE(PlanImportWindow::open_file(*p, d / "missing.png", {}, error));
    CHECK(error.find("does not exist") != std::string::npos);
    CHECK_FALSE(PlanImportWindow::open_source(*p, "source-99", {}, error));
    CHECK_FALSE(error.empty());
    if (!pl::pdf_supported()) {
        write(d / "plan.pdf", fx::pdf({fx::PdfPage{}}));
        CHECK_FALSE(PlanImportWindow::open_file(*p, d / "plan.pdf", {}, error));
        CHECK(error.find("PDF") != std::string::npos);
    }

    // A copy is kept in the project (embedded) and opens whole, fitted.
    auto w = open_sheet(*p, d);
    REQUIRE(p->sources().size() == 1);
    CHECK(p->sources()[0].plan.embedded);
    CHECK_FALSE(w->referenced());
    CHECK_FALSE(w->restricted());
    CHECK(w->title() == "sheet.png");
    CHECK(w->description().find("400 x 300") != std::string::npos);
    CHECK_FALSE(w->large_sheet());
    CHECK(w->page_count() == 1);
    CHECK(w->page_label(0) == "Page 1");
    CHECK(w->import().trim() == pl::whole_page_trim(pl::PageSize{400, 300}));
}

TEST_CASE("Large sheets (IND-02) are flagged for tiled rendering") {
    const fs::path d = scratch("large");
    auto p = project_in(d);
    write(d / "wide.png", fx::png(fx::pattern(15010, 4), 3));
    std::string error;
    auto w = PlanImportWindow::open_file(*p, d / "wide.png", {}, error);
    REQUIRE_MESSAGE(w, error);
    CHECK(w->large_sheet());
}

TEST_CASE("Rectangle tool (IND-05): drag, too small, corner drag, live preview") {
    const fs::path d = scratch("rect");
    auto p = project_in(d);
    auto w = open_sheet(*p, d);
    const unsigned g0 = w->trim_generation();
    REQUIRE(w->set_tool(ImportTool::Rectangle).ok);
    CHECK(w->pointer_down(Point2{20, 30}));
    w->pointer_move(Point2{100, 90});
    CHECK(w->drafting());
    CHECK(w->draft().trim.size() == 4);
    REQUIRE(w->pointer_up(Point2{220, 230}).ok);
    CHECK_FALSE(w->drafting());
    CHECK(w->trim_generation() != g0);
    CHECK(w->import().trim().shape == pl::Trim::Shape::Rectangle);
    CHECK(w->import().trim_size() == pl::PageSize{200, 200});
    CHECK(w->trim_text().find("200 x 200") != std::string::npos);
    CHECK(w->draft().handles.size() == 4);
    // The live preview is the trimmed part, fitted.
    const pl::Raster prev = w->import().preview(100);
    CHECK(prev.width == 100);
    CHECK(prev.height == 100);

    // A click is not a rectangle: the trim stays.
    CHECK_FALSE(click(*w, Point2{300, 100}).ok);
    CHECK(w->import().trim_size() == pl::PageSize{200, 200});

    // Dragging the corner (220, 230) keeps the opposite corner (20, 30).
    REQUIRE(drag_up(*w, Point2{218, 228}, Point2{120, 130}).ok);
    CHECK(w->import().trim_size() == pl::PageSize{100, 100});
    CHECK(w->import().trim().outline[0] == Point2{20, 30});

    REQUIRE(w->reset_trim().ok);
    CHECK(w->import().trim() == pl::whole_page_trim(pl::PageSize{400, 300}));
}

TEST_CASE("Polygon tool (IND-05): clicks, undo, close, invalid outline, corner drag") {
    const fs::path d = scratch("poly");
    auto p = project_in(d);
    auto w = open_sheet(*p, d);
    REQUIRE(w->set_tool(ImportTool::Polygon).ok);
    CHECK(click(*w, Point2{10, 10}).ok);
    CHECK(click(*w, Point2{200, 10}).ok);
    CHECK_FALSE(w->finish_polygon().ok);  // 2 points
    CHECK(click(*w, Point2{300, 300}).ok);
    w->undo_point();
    CHECK(w->draft().polygon.size() == 2);
    CHECK(click(*w, Point2{200, 200}).ok);
    CHECK(click(*w, Point2{10, 200}).ok);
    CHECK(w->hint().find("4 points") != std::string::npos);
    // Clicking the first point closes the outline.
    REQUIRE(click(*w, Point2{12, 11}).ok);
    CHECK_FALSE(w->drafting());
    CHECK(w->import().trim().shape == pl::Trim::Shape::Polygon);
    CHECK(w->import().trim().outline.size() == 4);
    CHECK(w->trim_text().find("Polygon (4 corners)") == 0);

    // A crossing outline is refused with the reason; the points stay.
    for (Point2 q : {Point2{30, 30}, Point2{180, 180}, Point2{180, 30}, Point2{30, 180}}) (void)click(*w, q);
    const Result bad = w->finish_polygon();
    CHECK_FALSE(bad.ok);
    CHECK(bad.message.find("cannot be used") != std::string::npos);
    CHECK(w->draft().polygon.size() == 4);
    w->cancel_draft();
    CHECK(w->draft().polygon.empty());

    // Dragging a corner of the current polygon adjusts it.
    CHECK(w->draft().handles.size() == 4);
    REQUIRE(drag_up(*w, Point2{200, 200}, Point2{250, 250}).ok);
    CHECK(w->import().trim().outline[2] == Point2{250, 250});
    // Dragging it across the outline is refused; the trim stays.
    CHECK_FALSE(drag_up(*w, Point2{250, 250}, Point2{-50, 100}).ok);
    CHECK(w->import().trim().outline[2] == Point2{250, 250});
    // Switching tools drops an unfinished outline.
    (void)click(*w, Point2{5, 5});
    REQUIRE(w->set_tool(ImportTool::Pan).ok);
    CHECK_FALSE(w->drafting());
}

TEST_CASE("Rotation (IND-05): quarter turns, deskew value and deskew line") {
    const fs::path d = scratch("rotate");
    auto p = project_in(d);
    auto w = open_sheet(*p, d);
    REQUIRE(w->rotate(1).ok);
    CHECK(w->quarter_turns() == 1);
    CHECK(w->import().trim_size() == pl::PageSize{300, 400});
    REQUIRE(w->rotate(-2).ok);
    CHECK(w->quarter_turns() == 3);
    CHECK_FALSE(w->set_deskew(50.0).ok);
    REQUIRE(w->set_deskew(2.5).ok);
    CHECK(w->deskew() == doctest::Approx(2.5));
    CHECK(w->trim_text().find("272.50") != std::string::npos);

    REQUIRE(w->set_tool(ImportTool::Deskew).ok);
    CHECK_FALSE(click(*w, Point2{10, 10}).ok);
    drag(*w, Point2{0, 0}, Point2{100, 0});
    REQUIRE(w->draft().line);
    // A line falling 1 in 100 to the right: turned back by atan(0.01).
    REQUIRE(w->pointer_up(Point2{100, 1}).ok);
    CHECK(std::fabs(w->deskew()) == doctest::Approx(std::atan(0.01) * 180.0 / std::numbers::pi).epsilon(1e-6));
}

TEST_CASE("Add as floor (IND-06): form, checks, the window stays for the next trim") {
    const fs::path d = scratch("add");
    auto p = project_in(d);
    auto w = open_sheet(*p, d);
    REQUIRE(w->set_tool(ImportTool::Rectangle).ok);
    REQUIRE(drag_up(*w, Point2{0, 0}, Point2{190, 290}).ok);
    w->begin_add_floor();
    REQUIRE(w->add_form());
    CHECK(w->add_form()->name == "Ground floor");
    CHECK(w->add_form()->level == 0);
    w->add_form()->name = "  ";
    CHECK_FALSE(w->confirm_add_floor().ok);
    w->add_form()->name = "Ground floor";
    w->add_form()->height_m = 0.0;
    CHECK_FALSE(w->confirm_add_floor().ok);
    w->add_form()->height_m = 3.2;
    REQUIRE(w->confirm_add_floor().ok);
    CHECK_FALSE(w->add_form());
    REQUIRE(w->added().size() == 1);
    const std::string ground = w->added()[0];
    CHECK(w->selected() == ground);
    // The current trim is the whole page again for the next floor.
    CHECK(w->import().trim() == pl::whole_page_trim(pl::PageSize{400, 300}));

    // The next floor: suggested level 1; a used level is refused.
    REQUIRE(drag_up(*w, Point2{210, 0}, Point2{400, 290}).ok);
    w->begin_add_floor();
    CHECK(w->add_form()->level == 1);
    CHECK(w->add_form()->name == "Floor 1");
    w->add_form()->level = 0;
    const Result used = w->confirm_add_floor();
    CHECK_FALSE(used.ok);
    CHECK(used.message.find("Ground floor") != std::string::npos);
    w->add_form()->level = 1;
    REQUIRE(w->confirm_add_floor().ok);

    const auto floors = p->floors();
    REQUIRE(floors.size() == 2);
    CHECK(floors[0].height_m == doctest::Approx(3.2));
    CHECK(floors[0].plan_width == doctest::Approx(190));
    CHECK(floors[1].plan_width == doctest::Approx(190));
    // Non-destructive (IND-07): the floors store source, page and trim only.
    CHECK(floors[0].source == w->import().source_id());
    CHECK(p->sources().size() == 1);

    // Both trims are outlined and labelled on the source.
    const auto placed = w->placed();
    REQUIRE(placed.size() == 2);
    CHECK(placed[0].label == "Ground floor (level 0)");
    CHECK(placed[0].on_page);
    CHECK(placed[0].outline.size() == 4);
    CHECK(placed[0].label_at.x == doctest::Approx(95));
    CHECK(placed[0].scale_text == "not to scale");
    CHECK(placed[1].selected);
    CHECK(w->page_label(0) == "Page 1 (2 floors)");

    // Pan/select: a click on a trim selects its floor.
    REQUIRE(w->set_tool(ImportTool::Pan).ok);
    CHECK_FALSE(w->pointer_down(Point2{50, 50}));  // the GUI pans
    REQUIRE(w->pointer_up(Point2{50, 50}).ok);
    CHECK(w->selected() == ground);
    CHECK(w->floor_at(Point2{300, 100}) == w->added()[1]);
    CHECK_FALSE(w->floor_at(Point2{200, 295}));
}

TEST_CASE("Editing and removing trims (IND-06, IND-07) keeps the scale and reference on the sheet") {
    const fs::path d = scratch("edit");
    auto p = project_in(d);
    auto w = open_sheet(*p, d);
    REQUIRE(w->set_tool(ImportTool::Rectangle).ok);
    REQUIRE(drag_up(*w, Point2{100, 50}, Point2{300, 250}).ok);
    const std::string id = add_floor(*w, "Hall", 0);

    // Scale line and reference point on the page.
    REQUIRE(w->set_tool(ImportTool::ScaleLine).ok);
    REQUIRE(drag_up(*w, Point2{120, 100}, Point2{220, 100}).ok);
    w->scale_form().distance_m = "5";
    REQUIRE(w->apply_scale().ok);
    CHECK(p->floor(id)->scale.method == bm::Scale::Method::TwoPoints);
    CHECK(p->floor(id)->scale.a.x == doctest::Approx(20));  // floor units
    REQUIRE(w->set_tool(ImportTool::Reference).ok);
    REQUIRE(click(*w, Point2{150, 150}).ok);

    // Edit: load the trim, turn it, widen it, apply.
    REQUIRE(w->edit_floor(id).ok);
    REQUIRE(w->editing());
    CHECK(w->tool() == ImportTool::Rectangle);
    CHECK(w->import().trim() == p->floor(id)->trim);
    CHECK_FALSE(w->set_page(0).ok);  // apply or cancel first
    CHECK_FALSE(w->request_remove(id).ok);
    w->begin_add_floor();
    CHECK_FALSE(w->confirm_add_floor().ok);
    w->cancel_add_floor();
    REQUIRE(w->rotate(1).ok);
    REQUIRE(drag_up(*w, Point2{50, 20}, Point2{350, 280}).ok);
    REQUIRE(w->apply_edit().ok);
    CHECK_FALSE(w->editing());
    const bm::Floor f = *p->floor(id);
    CHECK(f.trim.quarter_turns == 1);
    CHECK(f.plan_width == doctest::Approx(260));
    // The same page points as before, in the new floor units.
    const auto view = w->placed()[0];
    REQUIRE(view.scale_line);
    CHECK(view.scale_line->first.x == doctest::Approx(120));
    CHECK(view.scale_line->second.x == doctest::Approx(220));
    REQUIRE(view.reference);
    CHECK(view.reference->x == doctest::Approx(150));
    CHECK(view.reference->y == doctest::Approx(150));
    CHECK(mpu(*p, id) == doctest::Approx(0.05));

    // Cancel changes nothing in the project.
    REQUIRE(w->edit_floor(id).ok);
    REQUIRE(drag_up(*w, Point2{0, 0}, Point2{50, 50}).ok);
    REQUIRE(w->cancel_edit().ok);
    CHECK(p->floor(id)->trim == f.trim);

    // Remove asks first.
    REQUIRE(w->request_remove(id).ok);
    CHECK(w->remove_question().find("Hall") != std::string::npos);
    w->cancel_remove();
    CHECK(p->floor(id));
    REQUIRE(w->request_remove(id).ok);
    REQUIRE(w->confirm_remove().ok);
    CHECK_FALSE(p->floor(id));
    CHECK(w->placed().empty());
    CHECK(w->selected().empty());
    CHECK(w->added().empty());
    CHECK(p->sources().size() == 1);  // the plan file stays
}

TEST_CASE("Scale (IND-08): two points, scale bar, sharing, not to scale; reference point (IND-09)") {
    const fs::path d = scratch("scale");
    auto p = project_in(d);
    auto w = open_sheet(*p, d);

    // Without a selected floor the scale and reference tools say why.
    for (const ToolView& t : w->tools()) {
        if (t.tool == ImportTool::ScaleLine || t.tool == ImportTool::ScaleBar || t.tool == ImportTool::Reference) {
            CHECK_FALSE(t.enabled);
            CHECK(t.disabled_reason.find("Select a floor") != std::string::npos);
        }
        CHECK_FALSE(t.tooltip.empty());
    }
    CHECK_FALSE(w->set_tool(ImportTool::ScaleBar).ok);
    CHECK_FALSE(w->apply_scale().ok);

    REQUIRE(w->set_tool(ImportTool::Rectangle).ok);
    REQUIRE(drag_up(*w, Point2{0, 0}, Point2{190, 290}).ok);
    const std::string a = add_floor(*w, "Ground floor", 0);
    REQUIRE(drag_up(*w, Point2{210, 0}, Point2{400, 290}).ok);
    const std::string b = add_floor(*w, "First floor", 1);
    REQUIRE(w->select_floor(a).ok);

    // A plain PNG has no paper size: no drawing scale.
    CHECK_FALSE(w->drawing_scale_available());
    w->scale_form().kind = ScaleForm::Kind::DrawingScale;
    w->scale_form().ratio = "100";
    CHECK_FALSE(w->apply_scale().ok);

    REQUIRE(w->set_tool(ImportTool::ScaleBar).ok);
    CHECK_FALSE(w->apply_scale().ok);  // no line yet
    CHECK_FALSE(click(*w, Point2{20, 20}).ok);
    REQUIRE(drag_up(*w, Point2{20, 250}, Point2{120, 250}).ok);
    REQUIRE(w->scale_form().a);
    CHECK(w->scale_form().kind == ScaleForm::Kind::ScaleBar);
    w->scale_form().distance_m = "abc";
    CHECK_FALSE(w->apply_scale().ok);
    w->scale_form().distance_m = "0";
    CHECK_FALSE(w->apply_scale().ok);
    w->scale_form().distance_m = "2,5";
    REQUIRE(w->apply_scale().ok);
    CHECK(mpu(*p, a) == doctest::Approx(0.025));
    CHECK(p->floor(a)->scale.method == bm::Scale::Method::ScaleBar);
    CHECK(w->placed()[0].scale_text.find("scale bar 2.5 m") != std::string::npos);
    CHECK(w->placed()[1].scale_text == "not to scale");

    // Sharing: the other floor of the sheet takes the scale.
    const auto choices = w->share_choices();
    REQUIRE(choices.size() == 1);
    CHECK(choices[0].floor_id == b);
    CHECK_FALSE(choices[0].shared);
    REQUIRE(w->share_scale(b, true).ok);
    CHECK(w->share_choices()[0].shared);
    CHECK(mpu(*p, b) == doctest::Approx(0.025));
    CHECK(w->placed()[0].shared_with == "First floor");
    CHECK(w->placed()[1].scale_text.find("shared with Ground floor") != std::string::npos);
    // The scale others share from cannot be cleared.
    CHECK_FALSE(w->clear_scale().ok);
    REQUIRE(w->share_scale(b, false).ok);
    CHECK_FALSE(w->share_choices()[0].shared);
    CHECK(mpu(*p, b) == doctest::Approx(0.025));  // keeps the value

    // Selecting a floor loads its stored measurement into the form.
    REQUIRE(w->select_floor(b).ok);
    CHECK_FALSE(w->scale_form().a);
    CHECK(w->share_choices().size() == 1);
    REQUIRE(w->select_floor(a).ok);
    REQUIRE(w->scale_form().a);
    CHECK(w->scale_form().a->x == doctest::Approx(20));
    CHECK(w->scale_form().distance_m == "2.5");

    // Not to scale is allowed and marked.
    REQUIRE(w->clear_scale().ok);
    CHECK_FALSE(p->building().has_scale(*p->floor(a)));
    CHECK(w->placed()[0].scale_text == "not to scale");
    // Sharing needs a scale to share.
    CHECK_FALSE(w->share_scale(b, true).ok);

    // Reference point: inside the trim only.
    REQUIRE(w->set_tool(ImportTool::Reference).ok);
    CHECK_FALSE(click(*w, Point2{300, 100}).ok);
    REQUIRE(click(*w, Point2{40, 60}).ok);
    REQUIRE(p->floor(a)->reference_point);
    CHECK(p->floor(a)->reference_point->x == doctest::Approx(40));
    CHECK(w->placed()[0].aligned);
    REQUIRE(w->clear_reference().ok);
    CHECK_FALSE(p->floor(a)->reference_point);
}

TEST_CASE("Drawing scale (IND-08): 1:N from the page's paper size") {
    const fs::path d = scratch("ratio");
    auto p = project_in(d);
    // 100 dpi: one pixel is 0.254 mm on paper.
    write(d / "dpi.png", fx::png_with_dpi(fx::pattern(400, 300), 100.0));
    std::string error;
    auto w = PlanImportWindow::open_file(*p, d / "dpi.png", {}, error);
    REQUIRE_MESSAGE(w, error);
    const std::string id = add_floor(*w, "Ground floor", 0);
    REQUIRE(w->drawing_scale_available());
    w->scale_form().kind = ScaleForm::Kind::DrawingScale;
    w->scale_form().ratio = "1:x";
    CHECK_FALSE(w->apply_scale().ok);
    w->scale_form().ratio = "1:100";
    REQUIRE(w->apply_scale().ok);
    CHECK(mpu(*p, id) == doctest::Approx(0.0254));
    CHECK(p->floor(id)->scale.method == bm::Scale::Method::Factor);
    // The form shows it again after another selection.
    REQUIRE(w->select_floor(id).ok);
    CHECK(w->scale_form().ratio == "100");
}

TEST_CASE("PDF sources (IND-02): page list, page switch, thumbnails, drawing scale") {
    if (!pl::pdf_supported()) return;
    const fs::path d = scratch("pdf");
    auto p = project_in(d);
    write(d / "plans.pdf", fx::pdf({fx::PdfPage{}, fx::PdfPage{842.0, 595.0, 0, {}}}));
    std::string error;
    auto w = PlanImportWindow::open_file(*p, d / "plans.pdf", {}, error);
    REQUIRE_MESSAGE(w, error);
    CHECK(w->page_count() == 2);
    CHECK(w->description().find("300 dpi") != std::string::npos);
    CHECK(w->description().find("210 x 297 mm") != std::string::npos);
    const pl::Raster t = w->import().thumbnail(1, 96);
    CHECK(t.width == 96);
    CHECK_FALSE(w->set_page(2).ok);
    REQUIRE(w->set_page(1).ok);
    CHECK(w->page() == 1);
    const std::string id = add_floor(*w, "Ground floor", 0);
    CHECK(w->page_label(1) == "Page 2 (1 floor)");
    CHECK(w->page_label(0) == "Page 1");
    REQUIRE(w->drawing_scale_available());
    w->scale_form().kind = ScaleForm::Kind::DrawingScale;
    w->scale_form().ratio = "1:100";
    REQUIRE(w->apply_scale().ok);
    CHECK(mpu(*p, id) == doctest::Approx(100.0 * 0.0254 / 72.0));
    REQUIRE(w->set_page(0).ok);
    CHECK(w->selected().empty());  // the floor is on page 2
    CHECK_FALSE(w->placed()[0].on_page);
    REQUIRE(w->select_floor(w->added()[0]).ok);
    CHECK(w->page() == 1);
}

TEST_CASE("Licence-restricted sources (IND-03, DEMO-12): referenced, credited, never copied") {
    const fs::path d = scratch("restricted");
    // A bundled example description whose restricted sheet is this file.
    const fs::path cache = d / "cache";
    fs::create_directories(cache);
    const fs::path file = cache / "example_sheet.png";
    write(file, fx::png(fx::pattern(1680, 1565)));  // noisy: far larger than an empty project
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

    const auto rv = find_restricted_sheet({data}, sha_hex(bytes));
    REQUIRE(rv);
    CHECK(rv->licence == "CC BY-NC-ND 4.0");
    CHECK(rv->credit.find("MAK - Museum of Applied Arts, Vienna") == 0);
    CHECK(rv->credit.find("KI 23126-36") != std::string::npos);
    CHECK(rv->scale_approximate);
    CHECK_FALSE(find_restricted_sheet({data}, std::string(64, '0')));

    auto p = project_in(d);
    std::string error;
    auto w = PlanImportWindow::open_file(*p, file, {data}, error);
    REQUIRE_MESSAGE(w, error);
    REQUIRE(p->sources().size() == 1);
    const pj::SourceFile s = p->sources()[0];
    CHECK_FALSE(s.plan.embedded);  // a reference, not a copy
    CHECK(s.plan.sha256 == sha_hex(bytes));
    CHECK(w->referenced());
    REQUIRE(w->restricted());
    CHECK(w->restricted()->note.find("never a copy") != std::string::npos);
    CHECK(w->description().find("not copied") != std::string::npos);
    // The project file holds no copy of the image.
    p->save();
    CHECK(fs::file_size(p->path()) < bytes.size());
    // Re-opening the source shows the credit again.
    auto again = PlanImportWindow::open_source(*p, s.plan.id, {data}, error);
    REQUIRE_MESSAGE(again, error);
    CHECK(again->restricted());
    // The same file without the description is an ordinary copy.
    auto p2 = pj::Project::create(d / "other.yantra", pj::ProjectInfo{"Other", "", "", ""});
    auto copy = PlanImportWindow::open_file(*p2, file, {}, error);
    REQUIRE(copy);
    CHECK(p2->sources()[0].plan.embedded);
    CHECK_FALSE(copy->restricted());
}

TEST_CASE("AppState: Add floor -> plan import window -> project window (IND-01, IND-10)") {
    const fs::path d = scratch("app");
    AppPaths paths;
    paths.home = d;
    AppState st({}, paths);
    REQUIRE(st.new_project(d / "p", pj::ProjectInfo{"P", "", "", ""}).ok);
    REQUIRE(st.dialog() == Dialog::AddFloor);  // IND-01: the first floor next
    CHECK(st.plan_sources().empty());

    // The browser for plan files.
    st.browse(BrowsePurpose::PlanFile);
    REQUIRE(st.dialog() == Dialog::Browse);
    REQUIRE(st.browser() != nullptr);
    CHECK(st.browser()->filters().front().matches("plan.png"));
    st.browser_cancel();
    CHECK(st.dialog() == Dialog::AddFloor);

    // Unreadable: back in Add floor with the reason.
    std::ofstream(d / "notes.png") << "x";
    const Result bad = st.import_plan_file(d / "notes.png");
    CHECK_FALSE(bad.ok);
    CHECK(bad.message.find("notes.png") != std::string::npos);
    CHECK(st.status_is_error());
    CHECK(st.dialog() == Dialog::AddFloor);
    CHECK(st.plan_import() == nullptr);
    CHECK(st.plan_sources().empty());

    REQUIRE(st.import_plan_file(sheet(d)).ok);
    CHECK(st.dialog() == Dialog::PlanImport);
    PlanImportWindow* w = st.plan_import();
    REQUIRE(w != nullptr);
    REQUIRE(w->set_tool(ImportTool::Rectangle).ok);
    REQUIRE(drag_up(*w, Point2{0, 0}, Point2{190, 290}).ok);
    (void)add_floor(*w, "Ground floor", 0);
    REQUIRE(drag_up(*w, Point2{210, 0}, Point2{400, 290}).ok);
    const std::string up = add_floor(*w, "Floor 1", 1);
    CHECK(st.dialog() == Dialog::PlanImport);  // stays open (IND-06)
    st.cancel_dialog();                         // Close
    CHECK(st.dialog() == Dialog::None);
    CHECK(st.plan_import() == nullptr);
    REQUIRE(st.project_window()->tabs().size() == 2);
    CHECK(st.project_window()->selected() == up);
    REQUIRE(st.project_window()->content(2.0));
    CHECK(st.project_window()->content(2.0)->has_plan);

    // Add floor with the source already used; and re-opening a floor's plan.
    REQUIRE(st.trigger(Action::AddFloor).ok);
    const auto sources = st.plan_sources();
    REQUIRE(sources.size() == 1);
    CHECK(sources[0].detail.find("2 floors") != std::string::npos);
    CHECK(sources[0].available);
    REQUIRE(st.import_plan_source(sources[0].id).ok);
    REQUIRE(st.plan_import() != nullptr);
    CHECK(st.plan_import()->placed().size() == 2);
    st.cancel_dialog();
    CHECK(st.edit_plan_reason(up).empty());
    REQUIRE(st.edit_floor_plan(up).ok);
    CHECK(st.plan_import()->selected() == up);
    st.cancel_dialog();
    REQUIRE(st.add_floor("Roof", 5, 3.0).ok);
    const std::string roof = st.project_window()->tabs().back().id;
    CHECK(st.edit_plan_reason(roof).find("without a plan") != std::string::npos);
    CHECK_FALSE(st.edit_floor_plan(roof).ok);
    // Closing the project closes the window too.
    REQUIRE(st.edit_floor_plan(up).ok);
    REQUIRE(st.close_project().ok);
    CHECK(st.plan_import() == nullptr);
    CHECK(st.dialog() == Dialog::None);
}

TEST_CASE("AppState: a referenced plan whose cached file is gone is listed but not usable") {
    const fs::path d = scratch("app_missing");
    AppPaths paths;
    paths.home = d;
    AppState st({}, paths);
    REQUIRE(st.new_project(d / "p", pj::ProjectInfo{"P", "", "", ""}).ok);
    const fs::path cached = d / "cache.png";
    write(cached, fx::png(fx::pattern(64, 48)));
    (void)st.project()->add_source_reference(cached);
    fs::remove(cached);
    const auto sources = st.plan_sources();
    REQUIRE(sources.size() == 1);
    CHECK_FALSE(sources[0].available);
    CHECK(sources[0].reason.find("not there") != std::string::npos);
    CHECK_FALSE(st.import_plan_source(sources[0].id).ok);
    CHECK(st.plan_import() == nullptr);
}
