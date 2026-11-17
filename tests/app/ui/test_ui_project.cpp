// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project window (step 9.2) driven through the headless UI harness
// (TST-50): floor tabs with "+" (IND-04, IND-10), floor properties,
// reorder and remove with confirmation, the layer panel (visibility and
// order), the plan view (tiles, zoom, pan, scale bar; IND-07, IND-08), the
// interpolation layer with its stale badge (IND-43, IND-45), the DEMO
// banner (DEMO-07) and small windows (UI-13).
#include <doctest/doctest.h>

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

#include "gui.h"
#include "ui_harness.h"
#include "user_texture.h"
#include "yantra/app/app_state.h"
#include "yantra/core/building/edit.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "project" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

struct Shell {
    explicit Shell(AppPaths paths, AppSettings settings = {}) : state(std::move(settings), std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style();
        gui.set_wait_for_tiles(true);
        ui.set_draw([this] { gui.draw(); });
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

void make_depot(const fs::path& file) {
    auto p = pj::Project::create(file, pj::ProjectInfo{"Depot", "", "", ""});
    for (int level : {0, 1}) {
        bm::Floor f;
        f.name = level == 0 ? "Ground floor" : "First floor";
        f.level = level;
        f.plan_width = 400;
        f.plan_height = 200;
        const std::string id = p->add_floor(f);
        // Three manual points (no scale: the plan units are the map).
        pj::Session s;
        s.floor_id = id;
        s.kind = "manual";
        const std::int64_t sid = p->add_session(s);
        std::vector<pj::StoredPoint> pts;
        for (int n = 1; n <= 3; ++n) {
            pj::StoredPoint sp;
            sp.sequence = n;
            sp.time_ns = n * 1'000'000'000LL;
            sp.plan = {100.0 * n, 100.0};
            sp.kind = "manual";
            sp.group = static_cast<std::uint32_t>(n);
            sp.sample = R"({"carriers":[{"role":"serving","f":390000000,"level":-70,"unit":"dBm"}]})";
            pts.push_back(sp);
        }
        p->add_points(sid, pts);
    }
    p->close();
}

std::string tab_key(const std::string& floor_id) { return "tab." + floor_id; }

// A right click on a floor tab opens its menu (IND-04: move, remove).
bool tab_menu(Shell& s, const std::string& floor_id) {
    const auto t = s.ui.find(tab_key(floor_id));
    if (!t) return false;
    s.ui.drag(t->cx(), t->cy(), t->cx(), t->cy(), 1, 1);
    return s.ui.frames(2);
}

}  // namespace

TEST_CASE("project window: floor tabs, \"+\", properties, reorder by dragging a tab and remove from its menu (IND-04, IND-10)") {
    const fs::path dir = scratch("tabs");
    make_depot(dir / "depot.yantra");
    Shell s({});
    REQUIRE(s.state.open_project(dir / "depot.yantra").ok);
    REQUIRE(s.ui.frames(3));
    ProjectWindow* w = s.state.project_window();
    REQUIRE(w);
    const std::string g = w->tabs()[0].id;
    const std::string up = w->tabs()[1].id;
    REQUIRE_MESSAGE(s.ui.has(tab_key(g)), s.ui.dump());
    REQUIRE(s.ui.has(tab_key(up)));
    CHECK(s.ui.find(tab_key(g))->x0 < s.ui.find(tab_key(up))->x0);  // by level
    CHECK(s.ui.has("plan.canvas"));
    // No Move or Remove buttons in the main window: the tab's menu has them.
    CHECK_FALSE(s.ui.has("floor.down"));
    CHECK_FALSE(s.ui.has("floor.up"));
    CHECK_FALSE(s.ui.has("floor.remove"));
    REQUIRE(tab_menu(s, g));
    REQUIRE_MESSAGE(s.ui.has("floor.down"), s.ui.dump());
    CHECK(s.ui.find("floor.down")->disabled);  // the lowest floor
    CHECK_FALSE(s.ui.find("floor.up")->disabled);
    CHECK(s.ui.has("floor.remove"));
    s.ui.click_at(s.ui.find(tab_key(g))->cx(), s.ui.find(tab_key(g))->cy());  // outside the menu: it closes
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.ui.has("floor.remove"));

    // A click on a tab selects its floor.
    REQUIRE(s.ui.click(tab_key(up)));
    REQUIRE(s.ui.frames(2));
    CHECK(w->selected() == up);
    REQUIRE(tab_menu(s, up));
    CHECK(s.ui.find("floor.up")->disabled);  // the highest floor
    s.ui.click_at(s.ui.find(tab_key(up))->cx(), s.ui.find(tab_key(up))->cy());
    REQUIRE(s.ui.frames(2));

    // "+" opens Add floor; the new floor becomes the selected tab.
    REQUIRE(s.ui.click("floors.add"));
    REQUIRE(s.ui.wait_window("dlg.add_floor"));
    REQUIRE(s.ui.fill("name", "Roof", "dlg.add_floor"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.add_floor"));
    REQUIRE(s.ui.wait_window_closed("dlg.add_floor"));
    REQUIRE(s.ui.frames(3));
    REQUIRE(w->tabs().size() == 3);
    const std::string roof = w->tabs().back().id;
    CHECK(w->tabs().back().name == "Roof");
    CHECK(w->selected() == roof);
    CHECK(s.ui.has(tab_key(roof)));

    // Properties: rename it.
    REQUIRE(s.ui.click("floor.properties"));
    REQUIRE(s.ui.wait_window("dlg.floor_properties"));
    REQUIRE(s.ui.fill("name", "Plant room", "dlg.floor_properties"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.floor_properties"));
    REQUIRE(s.ui.wait_window_closed("dlg.floor_properties"));
    CHECK(s.state.project()->floor(roof)->name == "Plant room");
    REQUIRE(s.ui.frames(2));
    CHECK(s.ui.find(tab_key(roof))->label.find("Plant room") == 0);

    // Drag the roof's tab onto the ground floor's: it becomes the lowest
    // floor, stored in the project (one move, as the menu does it).
    {
        const auto from = s.ui.find(tab_key(roof));
        const auto to = s.ui.find(tab_key(g));
        REQUIRE(from);
        REQUIRE(to);
        s.ui.drag(from->cx(), from->cy(), to->x0 + 2.0F, to->cy(), 0, 12);
        REQUIRE(s.ui.frames(4));
    }
    REQUIRE(w->tabs().size() == 3);
    CHECK(w->tabs()[0].id == roof);
    CHECK(s.state.project()->floors()[0].id == roof);
    CHECK(s.state.project()->floors()[0].level < s.state.project()->floors()[1].level);
    CHECK(s.ui.find(tab_key(roof))->x0 < s.ui.find(tab_key(g))->x0);
    CHECK(w->selected() == roof);

    // The tab's menu: Move up, it swaps places with the next floor.
    REQUIRE(tab_menu(s, roof));
    REQUIRE(s.ui.click("floor.up"));
    REQUIRE(s.ui.frames(3));
    CHECK(w->tabs()[1].id == roof);
    CHECK(s.state.project()->floors()[1].id == roof);
    CHECK(s.ui.find(tab_key(roof))->x0 < s.ui.find(tab_key(up))->x0);
    CHECK(s.ui.find(tab_key(g))->x0 < s.ui.find(tab_key(roof))->x0);

    // Remove (the tab's menu) asks first; "Keep it" keeps it, "Remove" removes it.
    REQUIRE(tab_menu(s, roof));
    REQUIRE(s.ui.click("floor.remove"));
    REQUIRE(s.ui.wait_window("dlg.remove_floor"));
    REQUIRE(s.ui.click("dlg.cancel", "dlg.remove_floor"));
    REQUIRE(s.ui.wait_window_closed("dlg.remove_floor"));
    CHECK(s.state.project()->floors().size() == 3);
    REQUIRE(tab_menu(s, roof));
    REQUIRE(s.ui.click("floor.remove"));
    REQUIRE(s.ui.wait_window("dlg.remove_floor"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.remove_floor"));
    REQUIRE(s.ui.wait_window_closed("dlg.remove_floor"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.state.project()->floors().size() == 2);
    CHECK_FALSE(s.ui.has(tab_key(roof)));
    CHECK(s.ui.has(tab_key(w->selected())));

    // Closing the project goes back to the start screen.
    REQUIRE(s.state.close_project().ok);
    REQUIRE(s.ui.frames(3));
    CHECK_FALSE(s.ui.has("plan.canvas"));
    CHECK(s.ui.has("start.file.new_project"));
}

TEST_CASE("project window: the Map tab's Show row and map opacity, zoom and pan, scale bar, small windows (UI-13, UI-19)") {
    const fs::path dir = scratch("layers");
    make_depot(dir / "depot.yantra");
    Shell s({});
    REQUIRE(s.state.open_project(dir / "depot.yantra").ok);
    REQUIRE(s.ui.frames(3));
    const PlanCanvas& cv = s.gui.canvas();
    CHECK(cv.stats().points == 3);
    CHECK(cv.stats().scale.label == "not to scale");
    CHECK(cv.stats().scale.pixels == 0.0);

    // UI-18: the project window opens on the Walk tab; the Show row is the Map tab's.
    CHECK(s.ui.has("panel.walk"));
    CHECK_FALSE(s.ui.has("layer.points"));
    REQUIRE(s.ui.click("panel.map"));
    REQUIRE(s.ui.frames(2));
    // Toggle the points off and on.
    REQUIRE_MESSAGE(s.ui.has("layer.points"), s.ui.dump());
    REQUIRE(s.ui.click("layer.points"));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.state.settings().layers.visible(Layer::Points));
    CHECK(cv.stats().points == 0);
    CHECK(std::find(cv.stats().drawn.begin(), cv.stats().drawn.end(), Layer::Points) == cv.stats().drawn.end());
    REQUIRE(s.ui.click("layer.points"));
    REQUIRE(s.ui.frames(2));
    CHECK(cv.stats().points == 3);

    // UI-19: the map's opacity, kept in the settings (clamped).
    REQUIRE(s.ui.has("map.opacity"));
    CHECK(s.state.settings().layers.map_opacity == doctest::Approx(0.85));
    s.state.set_map_opacity(0.5);
    CHECK(s.state.settings().layers.map_opacity == doctest::Approx(0.5));
    s.state.set_map_opacity(0.0);
    CHECK(s.state.settings().layers.map_opacity == doctest::Approx(kMinMapOpacity));
    CHECK_FALSE(s.ui.has("layer.points.up"));  // no layer order in the UI any more (no Layers tab)

    // Zoom buttons and the wheel change the zoom; Fit restores it; a drag pans.
    const double fit = cv.view().zoom();
    REQUIRE(s.ui.click("plan.zoom_in"));
    REQUIRE(s.ui.frames(1));
    CHECK(cv.view().zoom() == doctest::Approx(fit * 1.25));
    REQUIRE(s.ui.click("plan.zoom_out"));
    REQUIRE(s.ui.click("plan.zoom_out"));
    REQUIRE(s.ui.frames(1));
    CHECK(cv.view().zoom() < fit);
    REQUIRE(s.ui.click("plan.fit"));
    REQUIRE(s.ui.frames(2));
    CHECK(cv.view().zoom() == doctest::Approx(fit));
    const auto canvas = *s.ui.find("plan.canvas");
    s.ui.wheel(canvas.cx(), canvas.cy(), 1.0F);
    REQUIRE(s.ui.frames(1));
    CHECK(cv.view().zoom() > fit);
    const bm::Point2 mid{canvas.cx() - canvas.x0, canvas.cy() - canvas.y0};  // canvas-local
    const auto at = cv.view().screen_to_page(mid);
    s.ui.drag(canvas.cx(), canvas.cy(), canvas.cx() + 60.0F, canvas.cy() + 30.0F, 0);
    REQUIRE(s.ui.frames(1));
    const auto moved = cv.view().screen_to_page(mid);
    CHECK(moved.x < at.x);
    CHECK(moved.y < at.y);

    // A scale shows the bar.
    const std::string g = s.state.project_window()->selected();
    s.state.project()->set_scale(g, *bm::factor_scale(0.05));
    REQUIRE(s.ui.frames(3));
    CHECK(cv.stats().scale.pixels > 0.0);
    CHECK(cv.stats().scale.label.find(" m") != std::string::npos);

    // A small window: everything still drawn inside it.
    s.ui.set_display_size(800.0F, 600.0F);
    REQUIRE(s.ui.frames(4));
    for (const char* id : {"floor.properties", "plan.fit", "layer.points", "plan.canvas"}) {
        const auto it = s.ui.find(id);
        REQUIRE_MESSAGE(it, id);
        CHECK(it->x0 >= 0.0F);
        CHECK(it->y1 <= 600.0F);
    }
}

#ifdef YANTRA_TEST_DEMO_DATA_DIR
TEST_CASE("project window on the demo: plan tiles, points, map, stale badge, DEMO banner, textures freed (DEMO-07)") {
    const fs::path dir = scratch("demo");
    AppPaths paths;
    paths.data_folders = {YANTRA_TEST_DEMO_DATA_DIR};
    paths.user_data = dir / "user";
    {
        Shell s(paths);
        REQUIRE(s.ui.click("start.file.open_demo"));
        REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
        REQUIRE(s.ui.frames(4));
        CHECK_MESSAGE(s.ui.has("demo_banner"), s.ui.dump());
        CHECK(s.state.project_view()->demo_label == "DEMO - simulated data");
        const PlanCanvas& cv = s.gui.canvas();
        const FloorContent* c = s.state.project_window()->content();
        REQUIRE(c);
        CHECK_MESSAGE(cv.stats().tiles > 0, cv.tile_error());
        CHECK(cv.stats().points == c->points.count());
        CHECK(cv.stats().interpolation);
        CHECK_FALSE(cv.stats().stale_badge);
        CHECK(cv.stats().scale.pixels > 0.0);
        CHECK(UserTexture::live_count() > 0);

        // Hover over a measured point: the readout names it.
        const auto canvas = *s.ui.find("plan.canvas");
        const auto& p0 = c->points.points.front();
        const auto at = cv.view().page_to_screen(p0.plan);
        CHECK(at.x > 0.0);
        s.ui.mouse_move(canvas.x0 + static_cast<float>(at.x), canvas.y0 + static_cast<float>(at.y));
        REQUIRE(s.ui.frames(3));
        REQUIRE(cv.hover().point);
        CHECK(cv.hover().text.find("Measured point") != std::string::npos);

        // A change of the scale makes the map stale: still drawn, with the badge.
        const std::string fid = c->floor_id;
        {
            const bm::Building b = s.state.project()->building();
            bm::Scale sc;
            sc.method = bm::Scale::Method::Factor;
            sc.metres_per_unit = *b.metres_per_unit(*b.find_floor(fid)) * 1.01;
            s.state.project()->set_scale(fid, sc);
        }
        REQUIRE(s.ui.frames(3));
        CHECK(cv.stats().interpolation);
        CHECK(cv.stats().stale_badge);

        // The binned view, when switched on (Map tab, Advanced, UI-19).
        s.ui.set_display_size(1600.0F, 2000.0F);
        REQUIRE(s.ui.click("panel.map"));
        REQUIRE(s.ui.frames(2));
        REQUIRE(s.ui.click("map.advanced"));
        REQUIRE(s.ui.frames(2));
        REQUIRE(s.ui.click("layer.binned"));
        REQUIRE(s.ui.frames(2));
        CHECK(cv.stats().bins > 0);

        // The second floor's tab.
        const std::string second = s.state.project_window()->tabs()[1].id;
        REQUIRE(s.ui.click(tab_key(second)));
        REQUIRE(s.ui.frames(3));
        CHECK(s.state.project_window()->selected() == second);
        CHECK(cv.stats().tiles > 0);

        REQUIRE(s.state.close_project().ok);
        REQUIRE(s.ui.frames(3));
        CHECK(UserTexture::live_count() == 0);
    }
    CHECK(UserTexture::live_count() == 0);
}
#endif
