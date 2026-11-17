// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plan import model (IND-05, IND-06, IND-07, IND-10): add a plan file
// to a project, view it whole with zoom and pan, trim several floors from
// one sheet (rectangle, polygon, quarter turns, deskew), list, edit and
// remove them, several sources per project, refused files, and floor
// tiles of a trimmed floor.
//
// Copied from yantra-v2 68ea20a: tests/plan/test_plan_import.cpp, adapted (project API of schema 2).

#include "plan_fixtures.h"
#include "yantra/core/building/edit.h"
#include "yantra/core/plan/import.h"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace pl = yantra::plan;
namespace pj = yantra::project;
namespace fx = yantra::plan::fixtures;
using pl::Point2;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

fs::path write(const fs::path& p, const fx::Bytes& b) {
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return p;
}

bool near(Point2 a, Point2 b, double tol = 1e-6) { return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol; }

// A sheet with two "floors" side by side: left half red, right half blue.
pl::Raster sheet() {
    pl::Raster r(400, 200);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 400; ++x) {
            std::uint8_t* p = r.pixel(x, y);
            p[0] = x < 200 ? 220 : 10;
            p[1] = static_cast<std::uint8_t>(y);
            p[2] = x < 200 ? 10 : 220;
            p[3] = 255;
        }
    return r;
}

}  // namespace

TEST_CASE("plan import: view shows the whole page, zoom and pan (IND-05)") {
    const fs::path dir = scratch("import-view");
    auto project = pj::Project::create(dir / "p.yantra", pj::ProjectInfo{"View", "", "", ""});
    auto imp = pl::PlanImport::add_file(*project, write(dir / "sheet.png", fx::png(sheet())));
    pl::PlanView& v = imp->view();
    v.set_viewport(1000, 800);
    v.fit(imp->source().page_size(0), 0, 0.0);
    CHECK(v.zoom() == doctest::Approx(2.5));  // 400 x 200 into 1000 x 800
    CHECK(near(v.page_to_screen({0, 0}), {0, 150}));
    CHECK(near(v.page_to_screen({400, 200}), {1000, 650}));
    // Zoom about a point keeps that point in place.
    const Point2 anchor{300, 400};
    const Point2 under = v.screen_to_page(anchor);
    v.zoom_at(3.0, anchor);
    CHECK(v.zoom() == doctest::Approx(7.5));
    CHECK(near(v.page_to_screen(under), anchor));
    v.pan_by({-50, 20});
    CHECK(near(v.page_to_screen(under), {250, 420}));
    // Turned views: screen <-> page round trips, the centre stays.
    const Point2 centre = v.screen_to_page({500, 400});
    imp->rotate_quarter(1);
    CHECK(imp->trim().quarter_turns == 1);
    CHECK(near(v.page_to_screen(centre), {500, 400}));
    imp->set_deskew(-3.5);
    CHECK(near(v.page_to_screen(centre), {500, 400}));
    for (const Point2 s : {Point2{0, 0}, Point2{999, 3}, Point2{123.5, 777.25}})
        CHECK(near(v.page_to_screen(v.screen_to_page(s)), s));
    // A whole-page trim follows the rotation; fitting shows all of it.
    CHECK(imp->trim() == pl::whole_page_trim({400, 200}, 1, -3.5));
    v.fit(imp->source().page_size(0), 1, -3.5);
    const auto [lo, hi] = v.visible_page_bounds();
    CHECK(lo.x <= 0.0);
    CHECK(lo.y <= 0.0);
    CHECK(hi.x >= 400.0);
    CHECK(hi.y >= 200.0);
    imp->rotate_quarter(-5);  // back to 0 (wraps)
    CHECK(imp->trim().quarter_turns == 0);
}

TEST_CASE("plan import: several floors from one sheet, edited and removed (IND-06, IND-07)") {
    const fs::path dir = scratch("import-floors");
    const fs::path file = dir / "p.yantra";
    std::string ground, first;
    {
        auto project = pj::Project::create(file, pj::ProjectInfo{"Floors", "", "", ""});
        auto imp = pl::PlanImport::add_file(*project, write(dir / "sheet.png", fx::png(sheet())));
        CHECK(imp->page_count() == 1);
        CHECK(imp->trim() == pl::whole_page_trim({400, 200}));  // opens on the whole page
        // Ground floor: the left half, a rectangle dragged right to left.
        imp->set_rectangle({195, 190}, {5, 10});
        CHECK(imp->trim_size() == pl::PageSize{190, 180});
        ground = imp->add_as_floor("Ground floor", 0);
        // First floor: the right half as a polygon, turned a quarter.
        imp->set_polygon({{205, 10}, {395, 10}, {395, 190}, {300, 150}, {205, 190}});
        imp->rotate_quarter(1);
        CHECK(imp->trim_size() == pl::PageSize{180, 190});
        first = imp->add_as_floor("First floor", 1, 3.5);
        // The window stays on the source; the trims are listed with labels.
        const auto placed = imp->placed();
        REQUIRE(placed.size() == 2);
        CHECK(placed[0].floor_id == ground);
        CHECK(placed[0].name == "Ground floor");
        CHECK(near(placed[0].label, {100, 100}));
        CHECK(placed[1].level == 1);
        CHECK(placed[1].label.x > 205);
        // A used level or no name: refused by the project.
        CHECK_THROWS_AS(imp->add_as_floor("Again", 0), pj::ProjectError);
        CHECK_THROWS_AS(imp->add_as_floor("", 5), pj::ProjectError);
        // Bad outlines never become the trim.
        const pl::Trim before = imp->trim();
        CHECK_THROWS_AS(imp->set_polygon({{0, 0}, {10, 10}, {10, 0}, {0, 10}}), pl::PlanError);
        CHECK_THROWS_AS(imp->set_rectangle({5, 5}, {5, 50}), pl::PlanError);
        CHECK(imp->trim() == before);
        // The preview is the trimmed floor: the first floor is blue.
        const pl::Raster pv = imp->preview(64);
        CHECK(std::max(pv.width, pv.height) == 64);
        CHECK(pv.pixel(pv.width / 2, 5)[2] == 220);
    }
    // IND-07: re-opened, the trims are there and can be adjusted.
    auto project = pj::Project::open(file);
    const auto floors = project->floors();
    REQUIRE(floors.size() == 2);
    CHECK(floors[0].plan_width == 190.0);
    CHECK(floors[1].plan_height == 190.0);
    CHECK(floors[1].height_m == 3.5);
    auto imp = pl::PlanImport::open(*project, floors[0].source);
    CHECK(imp->placed().size() == 2);
    imp->edit_floor(first);
    CHECK(imp->editing() == first);
    CHECK(imp->trim() == floors[1].trim);
    imp->rotate_quarter(1);
    imp->apply_to_floor();
    CHECK(project->floor(first)->trim.quarter_turns == 2);
    CHECK(project->floor(first)->plan_width == 190.0);
    // A deskewed rectangle keeps its centre and size.
    imp->edit_floor(ground);
    const pl::TrimTransform old(imp->trim());
    const Point2 c0 = old.floor_to_page({95, 90});
    imp->set_deskew(4.0);
    const pl::TrimTransform now(imp->trim());
    CHECK(now.floor_size().width == doctest::Approx(190.0));
    CHECK(now.floor_size().height == doctest::Approx(180.0));
    CHECK(near(now.floor_to_page({95, 90}), c0, 1e-9));
    imp->deskew_from_line({0, 0}, {100, 0});
    CHECK(imp->trim().deskew_deg == 0.0);
    imp->apply_to_floor();
    // Removing a floor of this source.
    imp->remove_floor(ground);
    CHECK_FALSE(imp->editing());
    CHECK(imp->placed().size() == 1);
    CHECK_THROWS_AS(imp->apply_to_floor(), pl::PlanError);
    CHECK_THROWS_AS(imp->edit_floor("floor-99"), pj::ProjectError);
}

TEST_CASE("plan import: an edited trim keeps the scale and the reference point on the sheet (IND-07, IND-08, IND-09)") {
    const fs::path dir = scratch("import-reanchor");
    auto project = pj::Project::create(dir / "r.yantra", pj::ProjectInfo{"Reanchor", "", "", ""});
    auto imp = pl::PlanImport::add_file(*project, write(dir / "sheet.png", fx::png(sheet())));
    imp->set_rectangle({5, 10}, {195, 190});
    const std::string id = imp->add_as_floor("Ground floor", 0);
    // Drawn on the floor (plan units of the first trim).
    project->set_scale(id, *yantra::building::two_point_scale({10, 20}, {110, 20}, 5.0));
    project->set_reference_point(id, Point2{30, 40});
    const auto before = *project->floor(id);
    const pl::TrimTransform old(before.trim);
    auto on_page = [](const pl::TrimTransform& t, const std::vector<Point2>& pts) {
        std::vector<Point2> out;
        for (const Point2& p : pts) out.push_back(t.floor_to_page(p));
        return out;
    };
    const std::vector<Point2> drawn{before.scale.a, before.scale.b, *before.reference_point};
    const auto page_points = on_page(old, drawn);
    // The trim is made larger and turned a quarter: everything stays on
    // the same page point.
    imp->edit_floor(id);
    imp->set_rectangle({2, 2}, {198, 198});
    imp->rotate_quarter(1);
    imp->apply_to_floor();
    const auto after = *project->floor(id);
    const pl::TrimTransform now(after.trim);
    const std::vector<Point2> moved{after.scale.a, after.scale.b, *after.reference_point};
    const auto page_after = on_page(now, moved);
    REQUIRE(page_after.size() == page_points.size());
    for (std::size_t i = 0; i < page_points.size(); ++i) {
        CAPTURE(i);
        CHECK(near(page_after[i], page_points[i], 1e-9));
    }
    CHECK(after.plan_width == doctest::Approx(196.0));
    // A trim only turns: the metres per unit stay.
    const auto b = project->building();
    CHECK(*b.metres_per_unit(*b.find_floor(id)) == doctest::Approx(0.05).epsilon(1e-12));
    // Editing without a change moves nothing.
    imp->edit_floor(id);
    imp->apply_to_floor();
    CHECK(project->floor(id)->reference_point == after.reference_point);
    CHECK(project->floor(id)->scale == after.scale);
    // reanchor_floor alone: from no trim (the whole page) to a trim.
    yantra::building::Floor f;
    f.reference_point = Point2{20, 30};
    pl::reanchor_floor(f, pl::Trim{}, pl::rectangle_trim({10, 10}, {100, 100}));
    CHECK(near(*f.reference_point, {10, 20}));
    CHECK(pl::trim_page_to_floor(pl::Trim{}, {3, 4}) == Point2{3, 4});
    CHECK(near(pl::trim_floor_to_page(pl::rectangle_trim({10, 10}, {100, 100}), {10, 20}), {20, 30}));
}

TEST_CASE("plan import: several sources, PDF pages, refused files (IND-02, IND-10)") {
    const fs::path dir = scratch("import-sources");
    auto project = pj::Project::create(dir / "p.yantra", pj::ProjectInfo{"Sources", "", "", ""});
    // A file that is not a plan: refused with the reason and not kept.
    const std::string note = "meeting notes, not a plan";
    std::ofstream(dir / "notes.txt") << note;
    try {
        (void)pl::PlanImport::add_file(*project, dir / "notes.txt");
        FAIL("opened");
    } catch (const pl::PlanError& e) {
        CHECK(e.code() == pl::PlanError::Code::Unsupported);
        CHECK(std::string(e.what()).find("notes.txt") != std::string::npos);
    }
    CHECK(project->sources().empty());
    // Image + (when built with PDFium) a PDF: two sources in one project.
    auto a = pl::PlanImport::add_file(*project, write(dir / "a.png", fx::png(sheet())));
    a->add_as_floor("Ground floor", 0);
    // The same file again is the same source.
    CHECK(pl::PlanImport::add_file(*project, dir / "a.png")->source_id() == a->source_id());
    REQUIRE(pl::pdf_supported() == bool(YANTRA_TEST_PDFIUM));
    if (!pl::pdf_supported()) return;
    fx::PdfPage p1, p2;
    p2.width = 842;
    p2.height = 595;
    p2.rects = {{0, 0, 421, 595, 0, 0, 255}};
    auto b = pl::PlanImport::add_file(*project, write(dir / "b.pdf", fx::pdf({p1, p2})));
    CHECK(b->source_id() != a->source_id());
    CHECK(b->page_count() == 2);
    // The page sizes are recorded in the project (IND-02 page list).
    CHECK(project->source_pages(b->source_id()) ==
          std::vector<pj::SourcePage>{{595, 842, 72}, {842, 595, 72}});
    CHECK(project->source_pages(a->source_id()) == std::vector<pj::SourcePage>{{400, 200, 0}});
    CHECK(b->thumbnail(1, 100).width == 100);
    b->set_page(1);
    CHECK(b->trim() == pl::whole_page_trim({842, 595}));
    b->set_rectangle({0, 0}, {421, 595});
    const std::string f = b->add_as_floor("First floor", 1);
    CHECK(project->floor(f)->page == 1);
    CHECK(b->placed()[0].page == 1);
    CHECK_THROWS_AS(b->set_page(2), pl::PlanError);
    CHECK(project->sources().size() == 2);
    // The project window renders the floor in tiles from its source.
    const auto floor = project->floor(f);
    auto tiles = pl::floor_tiles(pl::open_project_source(*project, b->source_id()), *floor, 0.5, 8u << 20);
    tiles->request(tiles->layout().tiles_for(0, 0, 0, 0, 421, 595));
    tiles->wait_idle();
    const auto t = tiles->find({0, 0, 0, 0});
    REQUIRE(t);
    CHECK(t->pixel(10, 10)[2] > 250);
    CHECK(t->pixel(10, 10)[0] < 5);
    auto pages = b->page_tiles(8u << 20);
    CHECK(pages->layout().base_scale() == doctest::Approx(300.0 / 72.0));
    yantra::building::Floor plain;
    plain.id = "x";
    CHECK_THROWS_AS(pl::floor_tiles(b->shared_source(), plain, 1.0, 1u << 20), pl::PlanError);
}
