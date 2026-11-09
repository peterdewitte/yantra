// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project window's tools (step 9.4) driven through the headless UI
// harness (TST-50): drawing, selecting, deleting walls with undo / redo
// by button and by key, placing an antenna and its properties dialog, the
// wall-class table (IND-11, IND-12); the interpolation run with its
// result on the plan, the propagation settings dialog and "run again" on
// a stale map (IND-40, IND-41, IND-45); the walk-test panel without radios
// (the reasons on disabled controls, DOC-07 c).
#include <doctest/doctest.h>

#include <imgui.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gui.h"
#include "plan_fixtures.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/building/edit.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "edit" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

struct Shell {
    explicit Shell(AppPaths paths) : state(AppSettings{}, std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style();
        gui.set_wait_for_tiles(true);
        ui.set_draw([this] { gui.draw(); });
        ui.set_display_size(1400.0F, 1000.0F);
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};

    /// A click on the plan at a plan point.
    void click_plan(bm::Point2 p) {
        const ImVec2 s = gui.canvas().screen_of(p);
        ui.click_at(s.x, s.y);
        // Apart from the next click (no double click).
        ui.mouse_move(s.x + 40.0F, s.y + 40.0F);
        (void)ui.frames(20);
    }
    bm::Floor floor() { return *state.project()->floor(state.project_window()->selected()); }
};

/// A 20 x 10 m floor (0.05 m per unit) with one wall, an antenna and, if
/// asked, measured points falling off from the antenna.
void make_floor(const fs::path& file, bool points, bool antenna = true) {
    auto p = pj::Project::create(file, pj::ProjectInfo{"Office", "", "", ""});
    bm::Floor f;
    f.name = "Ground floor";
    f.plan_width = 400;
    f.plan_height = 200;
    f.scale.method = bm::Scale::Method::Factor;
    f.scale.metres_per_unit = 0.05;
    const std::string id = p->add_floor(f);
    bm::Wall w;
    w.wall_class = "brick";
    w.points = {{200, 0}, {200, 120}};
    (void)p->add_wall(id, w);
    if (antenna) {
        bm::Antenna a;
        a.name = "DAS 1";
        a.position = {80, 100};
        (void)p->add_antenna(id, a);
    }
    if (points) {
        pj::Session s;
        s.floor_id = id;
        s.kind = "manual";
        const std::int64_t sid = p->add_session(s);
        std::vector<pj::StoredPoint> pts;
        int n = 0;
        for (int y = 20; y < 200; y += 40)
            for (int x = 10; x < 390; x += 30) {
                pj::StoredPoint sp;
                sp.sequence = ++n;
                sp.time_ns = n * 1'000'000'000LL;
                sp.plan = {static_cast<double>(x), static_cast<double>(y)};
                sp.kind = "manual";
                sp.group = static_cast<std::uint32_t>(n);
                const double d = std::hypot((x - 80) * 0.05, (y - 100) * 0.05) + 0.5;
                sp.sample = R"({"carriers":[{"role":"serving","f":390000000,"level":)" +
                            std::to_string(-30.0 - 24.0 * std::log10(d) - (x > 200 ? 8.0 : 0.0)) + R"(,"unit":"dBm"}]})";
                pts.push_back(sp);
            }
        p->add_points(sid, pts);
    }
    p->close();
}

// A plan picture (800 x 400) with three long dark lines and a small mark, a
// floor of it at 0.02 m per unit; no scale or no picture when asked (IND-11).
void make_plan_floor(const fs::path& file, bool scale, bool picture = true) {
    namespace fx = yantra::plan::fixtures;
    yantra::plan::Raster r(800, 400, 255);
    const auto ink = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                std::uint8_t* px = r.pixel(x, y);
                px[0] = px[1] = px[2] = 0;
            }
    };
    ink(50, 50, 750, 54);    // the longest: 14 m
    ink(100, 100, 104, 350); // 5 m
    ink(300, 300, 700, 304); // 8 m
    ink(600, 100, 610, 110); // a mark
    const fs::path png = file.parent_path() / "walls.png";
    {
        const fx::Bytes bytes = fx::png(r);
        std::ofstream(png, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    auto p = pj::Project::create(file, pj::ProjectInfo{"Walls", "", "", ""});
    bm::Floor f;
    f.name = "Ground floor";
    f.plan_width = 800;
    f.plan_height = 400;
    if (picture) {
        f.source = p->add_source(png).id;
        f.trim = yantra::plan::whole_page_trim({800, 400});
    }
    if (scale) {
        f.scale.method = bm::Scale::Method::Factor;
        f.scale.metres_per_unit = 0.02;
    }
    (void)p->add_floor(f);
    p->close();
}

}  // namespace

TEST_CASE("edit: draw a wall by clicking, Enter ends it; undo / redo by key and button; select and delete (IND-11)") {
    const fs::path dir = scratch("walls");
    make_floor(dir / "o.yantra", false);
    Shell s({});
    REQUIRE(s.state.open_project(dir / "o.yantra").ok);
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.ui.click("panel.edit"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("edit.tool.wall"), s.ui.dump());
    CHECK(s.ui.find("edit.undo")->disabled);
    CHECK(s.ui.find("edit.tool.aim")->disabled);  // no directional antenna selected

    REQUIRE(s.ui.click("edit.tool.wall"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.floor_editor()->tool() == EditTool::Wall);
    s.click_plan({40, 160});
    s.click_plan({160, 160});
    s.click_plan({160, 190});
    CHECK(s.gui.tool_overlay().draft_corners == 3);
    s.ui.key(ImGuiKey_Enter);
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.floor().walls.size() == 2);
    CHECK(s.floor().walls[1].points.size() == 3);
    CHECK(s.floor().walls[1].points[0].x == doctest::Approx(40).epsilon(0.02));
    CHECK(s.gui.canvas().stats().walls == 2);
    CHECK(s.gui.tool_overlay().selection);  // the new wall is selected
    CHECK(s.ui.has("edit.own_value"));  // the wall's properties (the class combo has no probe label)

    // Undo by key, redo by button.
    CHECK_FALSE(s.ui.find("edit.undo")->disabled);
    s.ui.key(ImGuiKey_Z, ImGuiMod_Ctrl);
    REQUIRE(s.ui.frames(2));
    CHECK(s.floor().walls.size() == 1);
    REQUIRE(s.ui.click("edit.redo"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.floor().walls.size() == 2);

    // Select the first wall (the vertical one) and delete it by button.
    REQUIRE(s.ui.click("edit.tool.select"));
    REQUIRE(s.ui.frames(2));
    s.click_plan({200, 60});
    REQUIRE(s.state.floor_editor()->selection().kind == Selection::Kind::Wall);
    REQUIRE(s.ui.has("edit.delete"));
    REQUIRE(s.ui.click("edit.delete"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.floor().walls.size() == 1);
    CHECK(s.floor().walls[0].points.size() == 3);
    REQUIRE(s.ui.click("edit.undo"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.floor().walls.size() == 2);

    // Esc leaves the selection; P back to Pan (a left drag pans again).
    s.ui.key(ImGuiKey_Escape);
    s.ui.key(ImGuiKey_P);
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.floor_editor()->tool() == EditTool::Pan);
    CHECK(s.state.floor_editor()->selection().kind == Selection::Kind::None);
}

TEST_CASE("edit: walls proposed from the plan: accept one, reject another, nothing stored before (IND-11)") {
    const fs::path dir = scratch("proposals");
    make_plan_floor(dir / "o.yantra", true);
    Shell s({});
    REQUIRE(s.state.open_project(dir / "o.yantra").ok);
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.ui.click("panel.edit"));
    REQUIRE(s.ui.frames(2));
    WallProposals& wp = *s.state.wall_proposals();
    FloorEditor& ed = *s.state.floor_editor();
    const auto run = [&] {
        REQUIRE_MESSAGE(s.ui.click("edit.propose"), s.ui.dump());
        wp.wait();
        REQUIRE(s.ui.frames(2));
    };
    CHECK_FALSE(s.ui.find("edit.propose")->disabled);
    run();
    std::vector<WallProposal> list = wp.view().proposals;
    REQUIRE(list.size() == 3);
    CHECK(list[0].length_m == doctest::Approx(14.0).epsilon(0.02));  // longest first, in metres
    CHECK(s.gui.tool_overlay().proposals == 3);
    // Not in the project, and no undo step: only a list.
    CHECK(s.floor().walls.empty());
    CHECK_FALSE(ed.can_undo());

    // The wall class is the one chosen for new walls.
    REQUIRE(ed.set_draw_class("brick").ok);
    REQUIRE_MESSAGE(s.ui.click("edit.proposal.accept." + std::to_string(list[0].id)), s.ui.dump());
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.floor().walls.size() == 1);
    const bm::Wall accepted = s.floor().walls.front();
    CHECK(accepted.wall_class == "brick");
    REQUIRE(accepted.points.size() == 2);
    CHECK(accepted.points[0].x == doctest::Approx(50.0).epsilon(0.05));
    CHECK(accepted.points[0].y == doctest::Approx(52.0).epsilon(0.05));
    CHECK(accepted.points[1].x == doctest::Approx(750.0).epsilon(0.01));
    // An ordinary wall: selected, its form open.
    CHECK(ed.selection().kind == Selection::Kind::Wall);
    CHECK(s.ui.has("edit.delete"));

    REQUIRE(s.ui.click("edit.proposal.reject." + std::to_string(list[1].id)));
    REQUIRE(s.ui.frames(2));
    CHECK(s.floor().walls.size() == 1);
    REQUIRE(wp.view().proposals.size() == 1);
    CHECK(wp.view().proposals.front().id == list[2].id);
    CHECK(s.gui.tool_overlay().proposals == 1);

    // Accepting is one undo step of the editor.
    REQUIRE(s.ui.click("edit.undo"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.floor().walls.empty());
    REQUIRE(s.ui.click("edit.redo"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.floor().walls.size() == 1);

    // Another run does not offer the wall that is there; a rejected one comes back.
    run();
    list = wp.view().proposals;
    CHECK(list.size() == 2);
    REQUIRE(s.ui.click("edit.proposal.accept_all"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.floor().walls.size() == 3);
    CHECK(wp.view().proposals.empty());
    run();
    CHECK(wp.view().proposals.empty());
    CHECK_FALSE(wp.view().note.empty());

    // The shortest length is the user's.
    REQUIRE(ed.undo().ok);  // the two walls of accept all again
    wp.set_min_length_m(6.0);
    run();
    CHECK(wp.view().proposals.size() == 1);  // 8 m stays, 5 m does not

    // Proposals are never in the project file.
    REQUIRE(s.state.close_project().ok);
    REQUIRE(s.ui.frames(2));
    auto p = pj::Project::open(dir / "o.yantra");
    CHECK(p->walls(p->building().floors.front().id).size() == 1);
}

TEST_CASE("edit: proposing walls: the disabled reasons, Cancel, a floor change forgets the list (IND-11, DOC-07 c)") {
    const fs::path dir = scratch("proposals2");
    SUBCASE("no scale") {
        make_plan_floor(dir / "o.yantra", false);
        Shell s({});
        REQUIRE(s.state.open_project(dir / "o.yantra").ok);
        REQUIRE(s.ui.frames(3));
        REQUIRE(s.ui.click("panel.edit"));
        REQUIRE(s.ui.frames(2));
        CHECK(s.ui.find("edit.propose")->disabled);
        CHECK(s.state.wall_proposals()->start_reason().find("scale") != std::string::npos);
        CHECK_FALSE(s.state.wall_proposals()->start().ok);
    }
    SUBCASE("no picture") {
        make_plan_floor(dir / "o.yantra", true, false);
        Shell s({});
        REQUIRE(s.state.open_project(dir / "o.yantra").ok);
        REQUIRE(s.ui.frames(3));
        REQUIRE(s.ui.click("panel.edit"));
        REQUIRE(s.ui.frames(2));
        CHECK(s.ui.find("edit.propose")->disabled);
        CHECK(s.state.wall_proposals()->start_reason().find("plan picture") != std::string::npos);
    }
    SUBCASE("cancel and floor change") {
        make_plan_floor(dir / "o.yantra", true);
        Shell s({});
        REQUIRE(s.state.open_project(dir / "o.yantra").ok);
        REQUIRE(s.ui.frames(3));
        REQUIRE(s.ui.click("panel.edit"));
        REQUIRE(s.ui.frames(2));
        WallProposals& wp = *s.state.wall_proposals();
        REQUIRE(wp.start().ok);
        CHECK_FALSE(wp.start().ok);  // one at a time
        wp.cancel();
        wp.wait();
        REQUIRE(s.ui.frames(2));
        CHECK_FALSE(wp.view().running);
        // Either stopped in time (nothing proposed) or it had just finished.
        const WallProposalsView v = wp.view();
        CHECK((v.proposals.empty() || v.proposals.size() == 3));
        CHECK_FALSE(v.note.empty());

        REQUIRE(wp.start().ok);
        wp.wait();
        REQUIRE(s.ui.frames(2));
        REQUIRE(wp.view().proposals.size() == 3);
        bm::Floor g;
        g.name = "First floor";
        g.level = 1;
        const std::string other = s.state.project()->add_floor(g);
        s.state.project_window()->select(other);
        REQUIRE(s.ui.frames(3));
        CHECK(wp.view().proposals.empty());
    }
}

#ifdef YANTRA_TEST_DEMO_DATA_DIR
TEST_CASE("edit: the Fraunces Tavern's sheet gives proposals on the floor; the project does not change (IND-11)") {
    const fs::path dir = scratch("proposals_demo");
    AppPaths paths;
    paths.data_folders = {YANTRA_TEST_DEMO_DATA_DIR};
    paths.user_data = dir / "user";
    Shell s(paths);
    REQUIRE(s.state.open_demo().ok);
    REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
    REQUIRE(s.ui.frames(2));
    const std::size_t walls = s.floor().walls.size();
    WallProposals& wp = *s.state.wall_proposals();
    REQUIRE(wp.start().ok);
    wp.wait();
    REQUIRE(s.ui.frames(2));
    const WallProposalsView v = wp.view();
    MESSAGE("Fraunces: " << v.proposals.size() << " proposals, " << walls << " walls");
    CHECK(v.proposals.size() >= 10);
    CHECK(v.proposals.size() <= 400);
    const bm::Floor f = s.floor();
    for (const WallProposal& p : v.proposals) {
        CHECK(p.length_m >= 1.5);
        for (const bm::Point2 q : {p.a, p.b}) {
            CHECK(q.x >= -1.0);
            CHECK(q.y >= -1.0);
            CHECK(q.x <= f.plan_width + 1.0);
            CHECK(q.y <= f.plan_height + 1.0);
        }
    }
    CHECK(s.floor().walls.size() == walls);
}
#endif

TEST_CASE("edit: place an antenna, its properties dialog with a new cell; the wall-class table (IND-11, IND-12)") {
    const fs::path dir = scratch("antenna");
    make_floor(dir / "o.yantra", false);
    Shell s({});
    REQUIRE(s.state.open_project(dir / "o.yantra").ok);
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.ui.click("panel.edit"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("edit.tool.antenna"));
    REQUIRE(s.ui.frames(2));
    s.click_plan({300, 150});
    REQUIRE(s.floor().antennas.size() == 2);
    CHECK(s.state.floor_editor()->tool() == EditTool::Select);
    REQUIRE_MESSAGE(s.ui.has("edit.antenna_props"), s.ui.dump());
    REQUIRE(s.ui.click("edit.antenna_props"));
    REQUIRE(s.ui.wait_window("dlg.antenna"));
    REQUIRE(s.ui.fill("name", "Stairs", "dlg.antenna"));
    REQUIRE(s.ui.fill("height", "3.1", "dlg.antenna"));
    REQUIRE(s.ui.fill("new_cell_name", "Indoor DAS", "dlg.antenna"));
    REQUIRE(s.ui.fill("new_cell_mhz", "390.0125", "dlg.antenna"));
    REQUIRE(s.ui.click("add_cell", "dlg.antenna"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.state.project()->cells().size() == 1);
    REQUIRE(s.ui.click("dlg.ok", "dlg.antenna"));
    REQUIRE(s.ui.wait_window_closed("dlg.antenna"));
    const bm::Antenna a = s.floor().antennas[1];
    CHECK(a.name == "Stairs");
    CHECK(a.height_m == doctest::Approx(3.1));
    CHECK(a.cell == s.state.project()->cells()[0].id);

    // A height out of range keeps the dialog open with the reason.
    REQUIRE(s.ui.click("edit.antenna_props"));
    REQUIRE(s.ui.wait_window("dlg.antenna"));
    REQUIRE(s.ui.fill("height", "500", "dlg.antenna"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.antenna"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.ui.window_open("dlg.antenna"));
    CHECK(s.state.status_is_error());
    REQUIRE(s.ui.click("dlg.cancel", "dlg.antenna"));
    REQUIRE(s.ui.wait_window_closed("dlg.antenna"));
    CHECK(s.floor().antennas[1].height_m == doctest::Approx(3.1));

    // The wall-class table: a new class; brick in use cannot be removed.
    REQUIRE(s.ui.click("edit.materials"));
    REQUIRE(s.ui.wait_window("dlg.materials"));
    CHECK(s.ui.find("mat.remove.brick", "dlg.materials")->disabled);
    REQUIRE(s.ui.click("mat.new", "dlg.materials"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.fill("mat.id", "lead_glass", "dlg.materials"));
    REQUIRE(s.ui.fill("mat.name", "Lead glass", "dlg.materials"));
    REQUIRE(s.ui.fill("mat.band.0.db", "25", "dlg.materials"));
    REQUIRE(s.ui.click("mat.apply", "dlg.materials"));
    REQUIRE(s.ui.frames(2));
    bool found = false;
    for (const bm::WallClass& c : s.state.project()->wall_classes())
        if (c.id == "lead_glass") found = c.name == "Lead glass" && c.bands.front().attenuation_db == doctest::Approx(25.0);
    CHECK(found);
    CHECK(s.ui.has("mat.edit.lead_glass", "dlg.materials"));
    REQUIRE(s.ui.click("dlg.cancel", "dlg.materials"));
    REQUIRE(s.ui.wait_window_closed("dlg.materials"));
}

TEST_CASE("interpolation: run with the map on the plan, settings dialog, stale badge, run again (IND-40, IND-41, IND-45)") {
    const fs::path dir = scratch("interp");
    make_floor(dir / "o.yantra", true);
    Shell s({});
    REQUIRE(s.state.open_project(dir / "o.yantra").ok);
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.ui.click("panel.map"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("interp.run"), s.ui.dump());
    CHECK_FALSE(s.ui.find("interp.run")->disabled);
    CHECK_FALSE(s.ui.has("interp.rerun"));  // no map yet
    REQUIRE(s.ui.click("interp.run"));
    s.state.interpolation()->wait();
    REQUIRE(s.ui.run_until([&] { return s.gui.canvas().stats().interpolation; }, 30));
    CHECK(s.state.status().rfind("Interpolated Ground floor", 0) == 0);
    CHECK_FALSE(s.gui.canvas().stats().stale_badge);
    REQUIRE(s.ui.has("interp.rerun"));
    CHECK(s.ui.find("interp.rerun")->label.find("stale") == std::string::npos);
    const auto maps = s.state.project()->interpolations(s.state.project_window()->selected());
    REQUIRE(maps.size() == 1);

    // The settings dialog: a bad grid keeps it open; a good one stores it.
    REQUIRE(s.ui.click("interp.settings"));
    REQUIRE(s.ui.wait_window("dlg.propagation"));
    REQUIRE(s.ui.fill("grid", "50", "dlg.propagation"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.propagation"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.ui.window_open("dlg.propagation"));
    CHECK(s.state.status().rfind("Not stored", 0) == 0);
    REQUIRE(s.ui.fill("grid", "0.5", "dlg.propagation"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.propagation"));
    REQUIRE(s.ui.wait_window_closed("dlg.propagation"));
    CHECK(s.state.project()->propagation().grid_m == doctest::Approx(0.5));

    // The map is stale now: the badge, "Run again (stale)"; run it again.
    REQUIRE(s.ui.run_until([&] { return s.gui.canvas().stats().stale_badge; }, 30));
    REQUIRE(s.ui.has("interp.rerun"));
    CHECK(s.ui.find("interp.rerun")->label.find("stale") != std::string::npos);
    REQUIRE(s.ui.click("interp.rerun"));
    s.state.interpolation()->wait();
    REQUIRE(s.ui.run_until([&] { return !s.gui.canvas().stats().stale_badge && s.gui.canvas().stats().interpolation; }, 30));
    // The same request replaces the stale map (one map per key).
    const auto now = s.state.project()->interpolations(s.state.project_window()->selected());
    REQUIRE(now.size() == 1);
    CHECK(now[0].id != maps[0].id);
}

TEST_CASE("interpolation: no antenna on the floor, the estimated sources are drawn and named (IND-42)") {
    const fs::path dir = scratch("estimated");
    make_floor(dir / "o.yantra", true, false);
    Shell s({});
    REQUIRE(s.state.open_project(dir / "o.yantra").ok);
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.ui.click("panel.map"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("interp.run"));
    s.state.interpolation()->wait();
    REQUIRE(s.ui.run_until([&] { return s.gui.canvas().stats().interpolation; }, 30));
    const FloorContent* c = s.state.project_window()->content(s.state.settings().layers.bin_size_m);
    REQUIRE(c);
    REQUIRE(c->interpolation);
    CHECK(c->interpolation->estimated_sources);
    REQUIRE_FALSE(c->interpolation->sources.empty());
    CHECK(c->interpolation->sources[0].label.rfind("estimated", 0) == 0);
    CHECK(s.gui.canvas().stats().estimated_sources == c->interpolation->sources.size());
}

TEST_CASE("walk test panel without radios: the reason on Start, the note after a search (DOC-07 c)") {
    const fs::path dir = scratch("walk");
    make_floor(dir / "o.yantra", false);
    AppPaths paths;
    paths.radios.radio_folders = {dir / "no_radios"};
    Shell s(paths);
    REQUIRE(s.state.open_project(dir / "o.yantra").ok);
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.ui.click("panel.walk"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("walk.start"), s.ui.dump());
    CHECK(s.ui.find("walk.start")->disabled);
    REQUIRE(s.ui.click("walk.find_radios"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.state.live()->radios().empty());
    CHECK(s.state.live()->radio_note().find("No radio found") == 0);
    CHECK(s.ui.find("walk.start")->disabled);
    CHECK(s.ui.has("walk.name"));
    CHECK(s.ui.find("walk.start")->disabled);
}
