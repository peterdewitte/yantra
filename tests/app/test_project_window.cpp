// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project window's model and the project commands of the app (TST-50,
// step 9.2): floor tabs ordered by level, rename / level / reorder /
// remove with confirmation (IND-04), the layers and their settings
// (IND-04, IND-30, IND-43, PP-05), the scale bar (IND-08), the live file
// (Save = checkpoint, the lock message, recent files; 7.1) and the DEMO
// banner (DEMO-07).
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "yantra/app/app_state.h"
#include "yantra/app/project_window.h"
#include "yantra/app/settings.h"
#include "yantra/core/building/edit.h"
#include "yantra/core/project/project.h"
#include "yantra/core/survey/points.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "window" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

/// A project with three floors given out of level order, walls, an
/// antenna and a walk test with points on the ground floor.
std::unique_ptr<pj::Project> make_building(const fs::path& file) {
    auto p = pj::Project::create(file, pj::ProjectInfo{"Depot", "Site", "", ""});
    bm::Floor up;
    up.name = "First floor";
    up.level = 1;
    (void)p->add_floor(up);
    bm::Floor ground;
    ground.name = "Ground floor";
    ground.level = 0;
    ground.plan_width = 400;
    ground.plan_height = 200;
    const std::string g = p->add_floor(ground);
    bm::Floor cellar;
    cellar.name = "Basement";
    cellar.level = -1;
    (void)p->add_floor(cellar);
    bm::Wall w;
    w.wall_class = "concrete";
    w.points = {{10, 10}, {390, 10}};
    (void)p->add_wall(g, w);
    w.wall_class = "plasterboard";
    w.points = {{200, 10}, {200, 190}};
    (void)p->add_wall(g, w);
    bm::Antenna a;
    a.name = "Hall";
    a.position = {100, 100};
    (void)p->add_antenna(g, a);
    pj::Session s;
    s.floor_id = g;
    const std::int64_t id = p->add_session(s);
    std::vector<pj::StoredPoint> pts;
    for (int i = 1; i <= 30; ++i) {
        pj::StoredPoint sp;
        sp.sequence = i;
        sp.time_ns = i * 1'000'000'000LL;
        sp.plan = {static_cast<double>(10 * i), 50.0};
        sp.sample = R"({"carriers":[{"role":"serving","f":390000000,"level":)" + std::to_string(-60 - i) +
                    R"(,"unit":"dBm"}]})";
        pts.push_back(sp);
    }
    p->add_points(id, pts);
    return p;
}

std::string floor_id(pj::Project& p, const std::string& name) {
    for (const bm::Floor& f : p.floors())
        if (f.name == name) return f.id;
    return {};
}

}  // namespace

TEST_CASE("layer settings: defaults, order, visibility and their JSON (IND-04)") {
    LayerSettings l;
    REQUIRE(l.order.size() == std::size(kLayers));
    CHECK(l.order.front() == Layer::Plan);
    CHECK(l.order.back() == Layer::Points);
    CHECK_FALSE(l.visible(Layer::Binned));  // an extra view (IND-30)
    for (Layer x : {Layer::Plan, Layer::Walls, Layer::Antennas, Layer::Points, Layer::Interpolation})
        CHECK(l.visible(x));
    CHECK_FALSE(l.move(Layer::Points, +1));  // already on top
    CHECK_FALSE(l.move(Layer::Plan, -1));
    CHECK(l.move(Layer::Points, -1));
    CHECK(l.position(Layer::Points) == l.order.size() - 2);
    l.set_visible(Layer::Walls, false);
    l.set_visible(Layer::Binned, true);
    l.bin_size_m = 5.0;
    for (Layer x : kLayers) CHECK(parse_layer(layer_key(x)) == x);
    CHECK_FALSE(parse_layer("maps"));

    AppSettings s;
    s.layers = l;
    std::string error;
    const AppSettings back = settings_from_json(settings_to_json(s), error);
    CHECK(error.empty());
    CHECK(back.layers == l);

    // Unknown and doubled keys are dropped; missing layers come back in
    // their default place; a bad square size keeps the default.
    const AppSettings odd = settings_from_json(
        R"({"layers":{"order":["points","maps","points","plan"],"hidden":["walls",3,"nope"],"bin_size_m":0.1}})",
        error);
    CHECK(odd.layers.order.size() == std::size(kLayers));
    CHECK(odd.layers.position(Layer::Points) < odd.layers.position(Layer::Plan));
    CHECK_FALSE(odd.layers.visible(Layer::Walls));
    CHECK(odd.layers.visible(Layer::Binned));
    CHECK(odd.layers.bin_size_m == 2.0);
}

TEST_CASE("scale bar: a round length near the target, or \"not to scale\" (IND-08)") {
    ScaleBar b = scale_bar(0.01, 1.0);  // 1 px = 1 cm: 120 px ~ 1.2 m -> 1 m
    CHECK(b.length_m == doctest::Approx(1.0));
    CHECK(b.pixels == doctest::Approx(100.0));
    CHECK(b.label == "1 m");
    b = scale_bar(0.01, 0.5);  // 120 px ~ 2.4 m -> 2 m
    CHECK(b.label == "2 m");
    CHECK(b.pixels == doctest::Approx(100.0));
    b = scale_bar(0.05, 0.2);  // 120 px ~ 30 m -> 20 m
    CHECK(b.label == "20 m");
    b = scale_bar(0.001, 10.0);  // 120 px ~ 12 mm -> 1 cm
    CHECK(b.label == "1 cm");
    b = scale_bar(10.0, 0.01);  // 120 px ~ 120 km -> 100 km
    CHECK(b.label == "100 km");
    for (const auto& none : {scale_bar(std::nullopt, 1.0), scale_bar(0.0, 1.0), scale_bar(0.01, 0.0)}) {
        CHECK(none.label == "not to scale");
        CHECK(none.pixels == 0.0);
    }
}

TEST_CASE("project window: floor tabs by level, content of the layers, notes (IND-04, IND-30, PP-05)") {
    const fs::path dir = scratch("tabs");
    auto p = make_building(dir / "depot.yantra");
    ProjectWindow w(*p);
    const auto& tabs = w.tabs();
    REQUIRE(tabs.size() == 3);
    CHECK(tabs[0].name == "Basement");
    CHECK(tabs[1].name == "Ground floor");
    CHECK(tabs[2].name == "First floor");
    CHECK(tabs[0].label == "Basement (-1)");
    CHECK(tabs[1].points == 30);
    CHECK(tabs[1].tooltip.find("30 measured points") != std::string::npos);
    CHECK(tabs[1].tooltip.find("Not to scale") != std::string::npos);
    CHECK(w.selected() == tabs[0].id);  // the lowest floor first
    CHECK_FALSE(w.select("floor-99"));

    const std::string g = floor_id(*p, "Ground floor");
    REQUIRE(w.select(g));
    const FloorContent* c = w.content();
    REQUIRE(c);
    CHECK(c->floor_id == g);
    CHECK(c->walls.size() == 2);
    CHECK(c->walls[0].rgb != c->walls[1].rgb);  // concrete darker than plasterboard
    CHECK(c->antennas.size() == 1);
    CHECK(c->antennas[0].label == "Hall");
    // IND-30: exactly the shared point list.
    yantra::survey::PointQuery q;
    q.floor_id = g;
    CHECK(c->points == yantra::survey::point_list(*p, q));
    CHECK_FALSE(c->has_plan);
    CHECK(c->width == 400);
    CHECK_FALSE(c->metres_per_unit);
    CHECK_FALSE(c->binned);
    CHECK(c->binned_note.find("scale") != std::string::npos);
    CHECK_FALSE(c->interpolation);

    LayerSettings layers;
    auto rows = w.layer_rows(layers);
    REQUIRE(rows.size() == std::size(kLayers));
    CHECK(rows.front().layer == Layer::Points);  // top first
    CHECK(rows.back().layer == Layer::Plan);
    CHECK(rows.front().label == "Measured points (30)");
    CHECK_FALSE(rows.front().can_move_up);
    CHECK(rows.front().can_move_down);
    const auto row = [&](Layer l) {
        return *std::find_if(rows.begin(), rows.end(), [&](const LayerRow& r) { return r.layer == l; });
    };
    CHECK(row(Layer::Walls).label == "Walls (2)");
    CHECK_FALSE(row(Layer::Plan).available);
    CHECK(row(Layer::Plan).note.find("no plan picture") != std::string::npos);
    CHECK(row(Layer::Interpolation).label == "Map (interpolated)");
    CHECK(row(Layer::Interpolation).note.find("scale") != std::string::npos);
    CHECK_FALSE(row(Layer::Binned).visible);

    // With a scale: the binned view appears with its label (PP-05); the
    // square size comes from the settings.
    const std::uint64_t gen = c->generation;
    p->set_scale(g, *bm::factor_scale(0.1));  // 30 points at x = 1 .. 30 m
    c = w.content(2.0);
    REQUIRE(c->metres_per_unit);
    CHECK(c->generation > gen);
    REQUIRE(c->binned);
    CHECK(c->binned->label.find("Binned view: 2 m squares") != std::string::npos);
    CHECK(c->binned->bins.size() == 16);  // x from 1 to 30 m in 2 m squares
    CHECK(c->binned->bins[0].min.x == doctest::Approx(0.0));
    CHECK(c->binned->bins[0].max.x == doctest::Approx(20.0));  // 2 m = 20 plan units
    c = w.content(5.0);
    CHECK(c->binned->bins.size() == 7);
    rows = w.layer_rows(layers);
    CHECK(row(Layer::Binned).note == c->binned->label);
    CHECK(row(Layer::Interpolation).note == "Not computed for this floor yet.");

    // Hover: a point within the radius; nothing elsewhere.
    HoverReadout h = w.hover({50, 51}, 3.0, layers);
    REQUIRE(h.point);
    CHECK(*h.point == 4);
    CHECK(h.text.find("Measured point 5 of 30") != std::string::npos);
    CHECK(h.text.find("-65.0 dBm") != std::string::npos);
    CHECK_FALSE(w.hover({50, 150}, 3.0, layers).any);
    layers.set_visible(Layer::Points, false);
    CHECK_FALSE(w.hover({50, 51}, 3.0, layers).any);

    // Empty floors say so.
    REQUIRE(w.select(floor_id(*p, "First floor")));
    c = w.content();
    CHECK(c->points.count() == 0);
    CHECK(c->width > 0.0);  // a blank sheet of a default size
    CHECK(c->walls.empty());
}

TEST_CASE("project window: rename, level, reorder and remove with confirmation (IND-04)") {
    const fs::path dir = scratch("commands");
    auto p = make_building(dir / "depot.yantra");
    ProjectWindow w(*p);
    const std::string g = floor_id(*p, "Ground floor");
    const std::string up = floor_id(*p, "First floor");
    const std::string down = floor_id(*p, "Basement");

    CHECK_FALSE(w.rename_floor(g, "").ok);
    REQUIRE(w.rename_floor(g, "Hall level").ok);
    CHECK(w.tabs()[1].name == "Hall level");
    // Level must be free; height within range.
    Result r = w.set_level(g, 1, 3.0);
    CHECK_FALSE(r.ok);
    CHECK(r.message.find("First floor") != std::string::npos);
    CHECK_FALSE(w.set_level(g, 0, 0.0).ok);
    CHECK_FALSE(w.set_level(g, 0, 51.0).ok);
    REQUIRE(w.set_level(up, 2, 4.5).ok);
    CHECK(p->floor(up)->height_m == 4.5);
    CHECK(w.tabs()[2].level == 2);

    // Reorder: the floors swap levels, the tabs follow.
    CHECK_FALSE(w.move_floor(down, -1).ok);
    CHECK_FALSE(w.move_floor(up, +1).ok);
    r = w.move_floor(down, +1);
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(w.tabs()[0].id == g);
    CHECK(w.tabs()[1].id == down);
    CHECK(p->floor(g)->level == -1);
    CHECK(p->floor(down)->level == 0);
    CHECK_FALSE(w.move_floor("floor-99", 1).ok);

    // Remove: asked first, naming what goes with it; cancel keeps it.
    REQUIRE(w.select(g));
    r = w.request_remove(g);
    REQUIRE(r.ok);
    REQUIRE(w.pending_remove());
    const std::string& qn = w.pending_remove()->question;
    CHECK(qn.find("\"Hall level\"") != std::string::npos);
    CHECK(qn.find("2 walls") != std::string::npos);
    CHECK(qn.find("1 antenna") != std::string::npos);
    CHECK(qn.find("1 walk test") != std::string::npos);
    CHECK(qn.find("30 measured points") != std::string::npos);
    w.cancel_remove();
    CHECK_FALSE(w.confirm_remove().ok);
    CHECK(p->floors().size() == 3);
    REQUIRE(w.request_remove(g).ok);
    r = w.confirm_remove();
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(p->floors().size() == 2);
    CHECK(p->sessions().empty());
    CHECK(w.tabs().size() == 2);
    CHECK(w.selected() == down);  // the next floor up
    CHECK_FALSE(w.request_remove(g).ok);
    REQUIRE(w.request_remove(up).ok);
    REQUIRE(w.confirm_remove().ok);
    CHECK(w.selected() == down);  // the one below
    CHECK(p->check().empty());
}

#ifdef YANTRA_TEST_DEMO_PROJECT
TEST_CASE("project window: the stored interpolation layer, its readout and the stale mark (IND-43, IND-45)") {
    const fs::path dir = scratch("interpolation");
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, dir / "fraunces.yantra");
    auto p = pj::Project::open(dir / "fraunces.yantra");
    ProjectWindow w(*p);
    REQUIRE(w.tabs().size() == 2);
    const FloorContent* c = w.content();
    REQUIRE(c);
    CHECK(c->has_plan);
    REQUIRE(c->metres_per_unit);
    REQUIRE(c->interpolation);
    const InterpolationLayer& in = *c->interpolation;
    CHECK(in.label.find("interpolated") == 0);
    CHECK_FALSE(in.stale);
    REQUIRE(in.cols > 0);
    CHECK(in.rgba.size() == static_cast<std::size_t>(in.cols) * static_cast<std::size_t>(in.rows) * 4U);
    CHECK(in.max.x > in.min.x);
    CHECK(in.points == c->points.count());
    CHECK(c->interpolations.size() >= 1);
    // Alpha is the confidence (IND-43): some cells fade, some are opaque.
    std::size_t faded = 0, opaque = 0;
    for (std::size_t i = 3; i < in.rgba.size(); i += 4) {
        if (in.rgba[i] < 255) ++faded;
        if (in.rgba[i] == 255) ++opaque;
    }
    CHECK(faded > 0);
    CHECK(opaque > 0);
    CHECK(w.show_interpolation(in.id));
    CHECK_FALSE(w.show_interpolation(-5));

    // Readout over the map, away from the points (the points layer hidden).
    LayerSettings layers;
    layers.set_visible(Layer::Points, false);
    const Point2 mid{(in.min.x + in.max.x) * 0.5, (in.min.y + in.max.y) * 0.5};
    const HoverReadout h = w.hover(mid, 1.0, layers);
    CHECK(h.any);
    CHECK_FALSE(h.point);
    CHECK(h.text.find("dBm") != std::string::npos);
    CHECK((h.text.find("interpolated") != std::string::npos || h.text.find("measured") != std::string::npos));
    layers.set_visible(Layer::Interpolation, false);
    CHECK_FALSE(w.hover(mid, 1.0, layers).any);

    // A changed wall makes it stale: shown, marked, with the reason.
    const std::string fid = c->floor_id;
    std::vector<bm::Wall> walls = p->walls(fid);
    REQUIRE_FALSE(walls.empty());
    p->remove_wall(fid, walls.front().id);
    c = w.content();
    REQUIRE(c->interpolation);
    CHECK(c->interpolation->stale);
    CHECK_FALSE(c->interpolation->stale_reasons.empty());
    const auto rows = w.layer_rows(LayerSettings{});
    const auto it = std::find_if(rows.begin(), rows.end(), [](const LayerRow& r) { return r.layer == Layer::Interpolation; });
    CHECK(it->note.find("Stale: ") == 0);

    // The plan tiles of the floor render.
    PlanTiles t = w.plan_tiles(fid, 32U << 20U);
    REQUIRE_MESSAGE(t.renderer, t.error);
    const auto& layout = t.renderer->layout();
    const auto keys = layout.tiles_for(0, layout.max_lod(), 0, 0, layout.extent().width, layout.extent().height);
    t.renderer->request(keys);
    t.renderer->wait_idle();
    CHECK(t.renderer->find(keys.front()));
    CHECK_FALSE(w.plan_tiles("floor-99", 1U << 20U).renderer);
}
#endif

TEST_CASE("app: floor properties, reorder and remove through the dialogs; layers in the settings") {
    const fs::path dir = scratch("app");
    make_building(dir / "depot.yantra")->close();
    AppState st({}, AppPaths{});
    REQUIRE(st.open_project(dir / "depot.yantra").ok);
    ProjectWindow* w = st.project_window();
    REQUIRE(w);
    const std::string g = floor_id(*st.project(), "Ground floor");

    REQUIRE(st.edit_floor(g).ok);
    CHECK(st.dialog() == Dialog::FloorProperties);
    CHECK(st.floor_properties_form().name == "Ground floor");
    st.floor_properties_form().name = "Hall";
    st.floor_properties_form().level = 1;  // taken
    Result r = st.confirm_floor_properties();
    CHECK_FALSE(r.ok);
    CHECK(st.dialog() == Dialog::FloorProperties);
    st.floor_properties_form().level = 5;
    st.floor_properties_form().height_m = 4.0;
    r = st.confirm_floor_properties();
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(st.dialog() == Dialog::None);
    CHECK(st.project()->floor(g)->name == "Hall");
    CHECK(st.project()->floor(g)->level == 5);
    CHECK(w->tabs().back().id == g);
    CHECK_FALSE(st.edit_floor("floor-99").ok);

    CHECK_FALSE(st.move_floor(g, +1).ok);
    REQUIRE(st.move_floor(g, -1).ok);
    CHECK(w->tabs()[1].id == g);

    REQUIRE(st.request_remove_floor(g).ok);
    CHECK(st.dialog() == Dialog::RemoveFloor);
    st.cancel_dialog();
    CHECK_FALSE(w->pending_remove());
    CHECK(st.project()->floors().size() == 3);
    REQUIRE(st.request_remove_floor(g).ok);
    REQUIRE(st.confirm_remove_floor().ok);
    CHECK(st.dialog() == Dialog::None);
    CHECK(st.project()->floors().size() == 2);
    CHECK(st.status().find("Removed floor Hall") != std::string::npos);

    // Add floor selects the new floor.
    REQUIRE(st.add_floor("Roof", 9, 2.5).ok);
    CHECK(w->tabs().back().name == "Roof");
    CHECK(w->selected() == w->tabs().back().id);

    // Layers are settings, not project data.
    st.set_layer_visible(Layer::Walls, false);
    CHECK_FALSE(st.settings().layers.visible(Layer::Walls));
    CHECK(st.settings_changed());
    CHECK_FALSE(st.move_layer(Layer::Points, +1).ok);
    REQUIRE(st.move_layer(Layer::Plan, +1).ok);
    CHECK(st.settings().layers.order[1] == Layer::Plan);
    CHECK_FALSE(st.set_bin_size(0.1).ok);
    REQUIRE(st.set_bin_size(4.0).ok);
    CHECK(st.settings().layers.bin_size_m == 4.0);

    REQUIRE(st.close_project().ok);
    CHECK(st.project_window() == nullptr);
    CHECK_FALSE(st.edit_floor(g).ok);
    CHECK_FALSE(st.move_floor(g, 1).ok);
}

TEST_CASE("live project file: Save is a checkpoint, a locked file gives the lock message, recent files (7.1)") {
    const fs::path dir = scratch("live");
    const fs::path file = dir / "depot.yantra";
    make_building(file)->close();
    AppSettings settings;
    AppState st(settings, AppPaths{});

    // Opened elsewhere (another Yantra holds the lock): refused, with what
    // to do; the file stays in the recent list.
    {
        auto other = pj::Project::open(file);
        const Result r = st.open_project(file);
        CHECK_FALSE(r.ok);
        CHECK(r.message.find("open in another Yantra window or program") != std::string::npos);
        CHECK(r.message.find("close it there") != std::string::npos);
        CHECK(st.status_is_error());
        CHECK(st.screen() == Screen::Start);
    }
    REQUIRE(st.open_project(file).ok);
    CHECK(st.recent_projects().front().path == file.lexically_normal());
    const auto v = st.project_view();
    REQUIRE(v);
    CHECK(v->file_note.find("stored in the file as you make them") != std::string::npos);
    CHECK(v->demo_label.empty());
    CHECK(st.window_title() == "Yantra - Depot");
    // A change is in the file at once; Save folds the log in (no -wal
    // content left), and there is nothing to discard on Close.
    REQUIRE(st.add_floor("Roof", 7, 3.0).ok);
    REQUIRE(st.save().ok);
    CHECK(st.status().find("Saved") == 0);
    std::error_code ec;
    CHECK(fs::file_size(file.string() + "-wal", ec) == 0);
    {
        // While open here, nobody else can open it either.
        bool locked = false;
        try {
            (void)pj::Project::open(file);
        } catch (const pj::ProjectError& e) {
            locked = e.code() == pj::ProjectError::Code::Locked;
        }
        CHECK(locked);
    }
    REQUIRE(st.close_project().ok);
    CHECK(pj::Project::open(file)->floors().size() == 4);
    // A file that is gone leaves the recent list when it is chosen.
    fs::remove(file);
    CHECK_FALSE(st.open_project(file).ok);
    CHECK(st.recent_projects().empty());
}

TEST_CASE("DEMO banner (DEMO-07): from the demo description's label, for every project made of example floors") {
    const fs::path dir = scratch("banner");
    AppPaths paths;
    paths.data_folders = {fs::path(YANTRA_SOURCE_DIR) / "data"};
    paths.user_data = dir / "user";
    AppState st({}, paths);
    // A project whose floors are bundled example floors (as the demo and
    // the MAK example buildings, or a Save as copy of them).
    {
        auto p = pj::Project::create(dir / "copy.yantra", pj::ProjectInfo{"Copy", "", "", ""});
        bm::Floor f;
        f.name = "First floor";
        f.example_id = "fraunces-1";
        (void)p->add_floor(f);
        p->close();
    }
    REQUIRE(st.open_project(dir / "copy.yantra").ok);
    CHECK(st.demo_label() == "DEMO - simulated data");
    CHECK(st.project_view()->demo_label == "DEMO - simulated data");
    CHECK(st.window_title() == "Yantra - Copy [DEMO]");
    REQUIRE(st.close_project().ok);
    CHECK(st.demo_label().empty());
    // The user's own project carries no banner.
    make_building(dir / "own.yantra")->close();
    REQUIRE(st.open_project(dir / "own.yantra").ok);
    CHECK(st.demo_label().empty());
    CHECK(st.window_title() == "Yantra - Depot");
}
