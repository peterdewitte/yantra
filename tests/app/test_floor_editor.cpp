// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The wall and antenna editing of the project window (TST-50, step 9.4):
// draw, select, move, split and delete walls and corners, place, move, aim
// and edit antennas, wall classes and cells (IND-11, IND-12), and the
// snapshot undo / redo through the project (one step per edit).
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

#include "yantra/app/floor_editor.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "editor" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

struct Rig {
    std::unique_ptr<pj::Project> project;
    std::string floor;
    std::unique_ptr<FloorEditor> ed;
    explicit Rig(const std::string& name) {
        project = pj::Project::create(scratch(name) / "e.yantra", pj::ProjectInfo{"Edit", "", "", ""});
        bm::Floor f;
        f.name = "Ground floor";
        f.plan_width = 400;
        f.plan_height = 200;
        floor = project->add_floor(f);
        ed = std::make_unique<FloorEditor>(*project);
        ed->set_floor(floor);
    }
    bm::Floor data() const { return *project->floor(floor); }
};

constexpr double kTol = 4.0;

void draw_wall(FloorEditor& ed, std::initializer_list<bm::Point2> corners) {
    REQUIRE(ed.set_tool(EditTool::Wall).ok);
    for (const bm::Point2& p : corners) {
        ed.pointer_down(p, kTol);
        ed.pointer_up(p, kTol);
    }
    REQUIRE(ed.finish_wall().ok);
}

void click(FloorEditor& ed, bm::Point2 p) {
    ed.pointer_down(p, kTol);
    ed.pointer_up(p, kTol);
}

}  // namespace

TEST_CASE("IND-11: the tools need a floor; Aim needs a directional antenna or a donor") {
    Rig r("tools");
    FloorEditor none(*r.project);
    for (const EditToolView& t : none.tools()) {
        CHECK(t.id == std::string("edit.tool.") + edit_tool_key(t.tool));
        CHECK(t.enabled == (t.tool == EditTool::Pan));
        if (!t.enabled) CHECK(t.reason == "Add a floor first.");
    }
    CHECK_FALSE(none.set_tool(EditTool::Wall).ok);
    CHECK_FALSE(none.wants_left_button());
    CHECK(r.ed->set_tool(EditTool::Select).ok);
    CHECK(r.ed->wants_left_button());
    const Result aim = r.ed->set_tool(EditTool::Aim);
    CHECK_FALSE(aim.ok);
    CHECK(aim.message.find("directional") != std::string::npos);
}

TEST_CASE("IND-11: draw a wall corner by corner with the chosen class; snap; Backspace and Esc") {
    Rig r("draw");
    REQUIRE(r.ed->set_draw_class("concrete").ok);
    CHECK_FALSE(r.ed->set_draw_class("no_such_class").ok);
    draw_wall(*r.ed, {{10, 10}, {200, 10}, {200, 150}});
    bm::Floor f = r.data();
    REQUIRE(f.walls.size() == 1);
    CHECK(f.walls[0].wall_class == "concrete");
    CHECK(f.walls[0].points.size() == 3);
    CHECK(r.ed->selection().kind == Selection::Kind::Wall);
    CHECK(r.ed->draft().empty());
    // A second wall starting near the first one's corner snaps onto it.
    REQUIRE(r.ed->set_tool(EditTool::Wall).ok);
    click(*r.ed, {202, 152});
    CHECK(r.ed->draft().back() == bm::Point2{200, 150});
    click(*r.ed, {300, 150});
    click(*r.ed, {300, 190});
    CHECK(r.ed->draft().size() == 3);
    CHECK(r.ed->remove_last_corner());
    CHECK(r.ed->draft().size() == 2);
    r.ed->hover(bm::Point2{350, 150});
    REQUIRE(r.ed->rubber_band());
    r.ed->double_click({350, 150}, kTol);  // ends the wall
    f = r.data();
    REQUIRE(f.walls.size() == 2);
    CHECK(f.walls[1].points.front() == bm::Point2{200, 150});
    // Esc drops a draft; one corner is not a wall.
    click(*r.ed, {10, 100});
    r.ed->cancel();
    CHECK(r.ed->draft().empty());
    click(*r.ed, {10, 100});
    CHECK_FALSE(r.ed->finish_wall().ok);
    CHECK(r.data().walls.size() == 2);
}

TEST_CASE("IND-11: select, move a wall and a corner by dragging, split at a corner, delete; undo and redo") {
    Rig r("walls");
    draw_wall(*r.ed, {{10, 10}, {200, 10}, {200, 150}});
    const std::string id = r.data().walls[0].id;
    REQUIRE(r.ed->set_tool(EditTool::Select).ok);
    r.ed->cancel();  // nothing selected
    CHECK(r.ed->selection().kind == Selection::Kind::None);
    click(*r.ed, {100, 11});
    REQUIRE(r.ed->selection().kind == Selection::Kind::Wall);
    CHECK(r.ed->selection().id == id);
    CHECK_FALSE(r.ed->selection_text().empty());

    // Drag the wall by 10 units down: a preview, then one stored change.
    r.ed->pointer_down({100, 10}, kTol);
    r.ed->pointer_move({100, 20}, kTol);
    REQUIRE(r.ed->drag_wall());
    CHECK(r.data().walls[0].points[0] == bm::Point2{10, 10});
    r.ed->pointer_up({100, 20}, kTol);
    CHECK_FALSE(r.ed->drag_wall());
    CHECK(r.data().walls[0].points[0] == bm::Point2{10, 20});
    CHECK(r.ed->undo_label() == "Undo: move wall");

    // Drag the middle corner (selected wall's corners come first).
    r.ed->pointer_down({200, 20}, kTol);
    REQUIRE(r.ed->selection().vertex);
    CHECK(*r.ed->selection().vertex == 1);
    r.ed->pointer_move({220, 30}, kTol);
    r.ed->pointer_up({220, 30}, kTol);
    CHECK(r.data().walls[0].points[1] == bm::Point2{220, 30});
    CHECK(r.ed->undo_label() == "Undo: move wall corner");

    // Split at the selected middle corner: two walls of two corners each.
    CHECK(r.ed->split_reason().empty());
    REQUIRE(r.ed->split_selected().ok);
    bm::Floor f = r.data();
    REQUIRE(f.walls.size() == 2);
    CHECK(f.walls[0].points.size() == 2);
    CHECK(f.walls[1].points.size() == 2);
    CHECK(f.walls[0].points.back() == f.walls[1].points.front());

    // Undo three times: split, corner move, wall move; then redo all.
    REQUIRE(r.ed->undo().ok);
    CHECK(r.data().walls.size() == 1);
    REQUIRE(r.ed->undo().ok);
    CHECK(r.data().walls[0].points[1] == bm::Point2{200, 20});
    REQUIRE(r.ed->undo().ok);
    CHECK(r.data().walls[0].points[0] == bm::Point2{10, 10});
    CHECK(r.data().walls[0].id == id);
    REQUIRE(r.ed->undo().ok);  // the drawing of the wall
    CHECK(r.data().walls.empty());
    CHECK_FALSE(r.ed->can_undo());
    CHECK_FALSE(r.ed->undo().ok);
    for (int i = 0; i < 4; ++i) REQUIRE(r.ed->redo().ok);
    CHECK_FALSE(r.ed->can_redo());
    CHECK(r.data().walls.size() == 2);

    // Deleting a corner of a two-corner wall deletes the wall (a wall
    // keeps two corners); undo brings it back. Then delete a wall.
    const bm::Point2 end = r.data().walls[1].points[1];
    click(*r.ed, end);
    click(*r.ed, end);
    REQUIRE(r.ed->selection().vertex);
    REQUIRE(r.ed->delete_selected().ok);
    CHECK(r.data().walls.size() == 1);
    CHECK(r.ed->undo_label() == "Undo: delete wall");
    REQUIRE(r.ed->undo().ok);
    REQUIRE(r.data().walls.size() == 2);
    r.ed->cancel();
    const bm::Wall second = r.data().walls[1];
    const bm::Point2 mid{(second.points[0].x + second.points[1].x) / 2, (second.points[0].y + second.points[1].y) / 2};
    click(*r.ed, mid);
    REQUIRE(r.ed->selection().kind == Selection::Kind::Wall);
    REQUIRE(r.ed->delete_selected().ok);
    CHECK(r.data().walls.size() == 1);
    CHECK(r.ed->selection().kind == Selection::Kind::None);
    REQUIRE(r.ed->undo().ok);
    CHECK(r.data().walls.size() == 2);
}

TEST_CASE("IND-11: a double click on the selected wall adds a corner; a wall's own attenuation") {
    Rig r("corner");
    draw_wall(*r.ed, {{10, 10}, {210, 10}});
    REQUIRE(r.ed->set_tool(EditTool::Select).ok);
    click(*r.ed, {50, 10});
    CHECK_FALSE(r.ed->split_reason().empty());  // no inner corner selected
    r.ed->double_click({110, 10}, kTol);
    REQUIRE(r.data().walls[0].points.size() == 3);
    CHECK(r.data().walls[0].points[1].x == doctest::Approx(110));
    CHECK(r.ed->split_reason().empty());

    WallForm& w = r.ed->wall_form();
    CHECK(w.wall_class == "brick");
    w.own_value = true;
    w.attenuation_db = 150.0;
    CHECK_FALSE(r.ed->apply_wall_form().ok);
    w.attenuation_db = 7.5;
    REQUIRE(r.ed->apply_wall_form().ok);
    REQUIRE(r.data().walls[0].attenuation_db);
    CHECK(*r.data().walls[0].attenuation_db == doctest::Approx(7.5));
    r.ed->wall_form().wall_class = "glass";
    r.ed->wall_form().own_value = false;
    REQUIRE(r.ed->apply_wall_form().ok);
    CHECK(r.data().walls[0].wall_class == "glass");
    CHECK_FALSE(r.data().walls[0].attenuation_db);
}

TEST_CASE("IND-12: place, move and edit an antenna; cells; aim a directional antenna") {
    Rig r("antenna");
    REQUIRE(r.ed->set_tool(EditTool::Antenna).ok);
    click(*r.ed, {100, 100});
    bm::Floor f = r.data();
    REQUIRE(f.antennas.size() == 1);
    CHECK(f.antennas[0].name == "Antenna 1");
    CHECK(r.ed->tool() == EditTool::Select);
    REQUIRE(r.ed->selection().kind == Selection::Kind::Antenna);
    const std::string id = f.antennas[0].id;

    // Drag it: a preview, then a move.
    r.ed->pointer_down({100, 100}, kTol);
    r.ed->pointer_move({150, 120}, kTol);
    REQUIRE(r.ed->drag_antenna());
    r.ed->pointer_up({150, 120}, kTol);
    CHECK(r.data().antennas[0].position == bm::Point2{150, 120});
    CHECK(r.ed->undo_label() == "Undo: move antenna");

    // Properties: validation, a new cell, directional with azimuth.
    AntennaForm& a = r.ed->antenna_form();
    CHECK(a.antenna_id == id);
    a.height_m = 500.0;
    CHECK_FALSE(r.ed->apply_antenna_form().ok);
    a.height_m = 2.7;
    a.name = "Corridor";
    a.eirp_known = true;
    a.eirp_dbm = 20.0;
    a.directional = true;
    a.azimuth_deg = 90.0;
    a.beamwidth_deg = 65.0;
    REQUIRE(r.ed->add_cell("Indoor DAS", 390.0125).ok);
    CHECK_FALSE(r.ed->add_cell("", 0.0).ok);
    REQUIRE(r.ed->cells().size() == 1);
    CHECK(r.ed->antenna_form().cell == r.ed->cells()[0].id);
    REQUIRE(r.ed->apply_antenna_form().ok);
    bm::Antenna got = r.data().antennas[0];
    CHECK(got.name == "Corridor");
    CHECK(got.height_m == doctest::Approx(2.7));
    REQUIRE(got.eirp_dbm);
    CHECK(*got.eirp_dbm == doctest::Approx(20.0));
    CHECK(got.cell == r.ed->cells()[0].id);
    CHECK(got.pattern.type != bm::AntennaPattern::Type::Omni);

    // Aim: click south of the antenna (degrees clockwise from the top).
    REQUIRE(r.ed->set_tool(EditTool::Aim).ok);
    click(*r.ed, {150, 190});
    CHECK(r.ed->tool() == EditTool::Select);
    const double az = r.data().antennas[0].pattern.azimuth_deg;
    CHECK(az == doctest::Approx(180.0));

    // A second antenna gets the only cell by itself; undo the placement.
    REQUIRE(r.ed->set_tool(EditTool::Antenna).ok);
    click(*r.ed, {300, 50});
    REQUIRE(r.data().antennas.size() == 2);
    CHECK(r.data().antennas[1].cell == r.ed->cells()[0].id);
    REQUIRE(r.ed->undo().ok);
    CHECK(r.data().antennas.size() == 1);
    // Undo the aim and the properties, then the new cell (its own step).
    REQUIRE(r.ed->undo().ok);  // aim
    CHECK(r.ed->undo_label() == "Undo: antenna properties");
    REQUIRE(r.ed->undo().ok);
    CHECK(r.data().antennas[0].name == "Antenna 1");
    CHECK(r.project->cells().size() == 1);
    CHECK(r.ed->undo_label() == "Undo: add cell");
    REQUIRE(r.ed->undo().ok);
    CHECK(r.project->cells().empty());
    REQUIRE(r.ed->redo().ok);
    CHECK(r.project->cells().size() == 1);
}

TEST_CASE("IND-11: the wall-class table: new, edit, remove (not while used), reset; undo") {
    Rig r("materials");
    const auto rows = r.ed->materials();
    REQUIRE_FALSE(rows.empty());
    CHECK(std::any_of(rows.begin(), rows.end(), [](const MaterialRow& m) { return m.id == "brick" && m.default_class; }));
    REQUIRE(r.ed->edit_material("").ok);
    MaterialForm& m = r.ed->material_form();
    CHECK(m.is_new);
    m.id = "lead_glass";
    m.name = "Lead glass";
    m.bands = {{400.0, 25.0}};
    REQUIRE(r.ed->apply_material().ok);
    REQUIRE(r.ed->set_draw_class("lead_glass").ok);
    draw_wall(*r.ed, {{10, 10}, {100, 10}});
    const auto used = r.ed->materials();
    const auto lg = std::find_if(used.begin(), used.end(), [](const MaterialRow& x) { return x.id == "lead_glass"; });
    REQUIRE(lg != used.end());
    CHECK(lg->walls == 1);
    CHECK(lg->bands.find("25") != std::string::npos);
    CHECK_FALSE(r.ed->remove_material("lead_glass").ok);
    REQUIRE(r.ed->undo().ok);  // the wall
    REQUIRE(r.ed->remove_material("lead_glass").ok);
    REQUIRE(r.ed->undo().ok);  // back
    CHECK(std::any_of(r.project->wall_classes().begin(), r.project->wall_classes().end(),
                      [](const bm::WallClass& c) { return c.id == "lead_glass"; }));
    // Edit a default class, then reset the defaults.
    REQUIRE(r.ed->edit_material("brick").ok);
    CHECK_FALSE(r.ed->material_form().is_new);
    r.ed->material_form().bands = {{400.0, 33.0}};
    REQUIRE(r.ed->apply_material().ok);
    REQUIRE(r.ed->reset_materials().ok);
    for (const bm::WallClass& c : r.project->wall_classes())
        if (c.id == "brick") CHECK(c.bands.front().attenuation_db != doctest::Approx(33.0));
}

TEST_CASE("Undo is dropped with a note when the floor was changed elsewhere") {
    Rig r("foreign");
    draw_wall(*r.ed, {{10, 10}, {100, 10}});
    REQUIRE(r.ed->can_undo());
    bm::Wall w;
    w.wall_class = "glass";
    w.points = {{0, 50}, {50, 50}};
    (void)r.project->add_wall(r.floor, w);  // not through the editor
    CHECK_FALSE(r.ed->undo().ok);
    CHECK_FALSE(r.ed->can_undo());
    CHECK_FALSE(r.ed->undo_note().empty());
    CHECK(r.data().walls.size() == 2);
}
