// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project schema 3 (step 7.3; docs/formats/PROJECT.md): the floor scale
// from two points and a known distance, shared by the floors of one sheet
// (IND-08); the alignment point between floors of different sources and
// scales (IND-09); walls drawn, edited and classed from the editable
// wall-class table (IND-11); antennas placed with their fields and cells
// (IND-12); units and precision; the round trip through the file of all
// of it, the scenes exported from a reopened project, and the migration
// of schema 1 and 2 files.

#include "example_building.h"
#include "yantra/core/building/edit.h"
#include "yantra/core/project/project.h"
#include "yantra/core/util/sha256.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using Code = pj::ProjectError::Code;
using nlohmann::json;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_file(const fs::path& p, const std::string& bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string fake_png(int seed) {
    std::string s = "\x89PNG\r\n\x1a\n";
    for (int i = 0; i < 2000; ++i) s.push_back(static_cast<char>((i * 13 + seed) & 0xff));
    return s;
}

pj::ProjectInfo info() { return pj::ProjectInfo{"Office survey", "Office", "Example network", ""}; }

Code code_of(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const pj::ProjectError& e) {
        return e.code();
    }
    FAIL("no ProjectError");
    return Code::Invalid;
}

void raw_sql(const fs::path& p, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(p.string().c_str(), &db) == SQLITE_OK);
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    const std::string msg = err != nullptr ? err : "";
    sqlite3_free(err);
    sqlite3_close(db);
    REQUIRE_MESSAGE(rc == SQLITE_OK, msg);
}

bm::Floor floor_of(const std::string& name, int level, const std::string& source = {}, int page = 0) {
    bm::Floor f;
    f.name = name;
    f.level = level;
    f.source = source;
    f.page = page;
    f.plan_width = 1000;
    f.plan_height = 600;
    return f;
}

bm::Scale scale_or_fail(std::optional<bm::Scale> s) {
    REQUIRE(s.has_value());
    return *s;
}

// Collects the change notifications.
struct Changes {
    std::vector<pj::Change> seen;
    int count(pj::Change::Kind k, const std::string& id) const {
        int n = 0;
        for (const pj::Change& c : seen)
            if (c.kind == k && c.id == id) ++n;
        return n;
    }
};

}  // namespace

TEST_CASE("project 7.3: scale from two points and a known distance; floors of one sheet share it (IND-08)") {
    const fs::path dir = scratch("scale");
    write_file(dir / "sheet.png", fake_png(1));
    write_file(dir / "annex.png", fake_png(2));
    const fs::path file = dir / "s.yantra";
    std::string g, f1, annex, bare;
    {
        auto p = pj::Project::create(file, info());
        const std::string sheet = p->add_source(dir / "sheet.png").id;
        const std::string other = p->add_source(dir / "annex.png").id;
        g = p->add_floor(floor_of("Ground floor", 0, sheet));
        f1 = p->add_floor(floor_of("First floor", 1, sheet));
        annex = p->add_floor(floor_of("Annex", -1, other));
        bare = p->add_floor(floor_of("Roof", 2));
        Changes changes;
        p->subscribe([&](const pj::Change& c) { changes.seen.push_back(c); });

        // A floor without scale is allowed, but has no metres: "not to scale".
        bm::Building b = p->building();
        CHECK_FALSE(b.has_scale(*b.find_floor(g)));
        CHECK(bm::format_length(bm::distance_m(b, *b.find_floor(g), {0, 0}, {300, 400})) == "not to scale");
        CHECK(bm::format_area(bm::area_m2(b, *b.find_floor(g), {{0, 0}, {100, 0}, {100, 100}})) == "not to scale");

        // Two points 500 plan units apart are 25 m apart: 0.05 m per unit.
        p->set_scale(g, scale_or_fail(bm::two_point_scale({100, 200}, {600, 200}, 25.0)));
        CHECK(changes.count(pj::Change::Kind::FloorChanged, g) == 1);
        b = p->building();
        const bm::Floor& gf = *b.find_floor(g);
        CHECK(gf.scale.method == bm::Scale::Method::TwoPoints);
        CHECK(*b.metres_per_unit(gf) == doctest::Approx(0.05).epsilon(1e-15));
        CHECK(*bm::distance_m(b, gf, {0, 0}, {300, 400}) == doctest::Approx(25.0).epsilon(1e-15));
        CHECK(bm::format_length(bm::distance_m(b, gf, {0, 0}, {300, 400})) == "25.00 m");
        CHECK(bm::format_area(bm::area_m2(b, gf, {{0, 0}, {200, 0}, {200, 100}, {0, 100}})) == "50.0 m²");

        // A scale bar on the annex: its 10 m bar is 250 units long.
        p->set_scale(annex, scale_or_fail(bm::scale_bar({50, 580}, {300, 580}, 10.0)));
        b = p->building();
        CHECK(*b.metres_per_unit(*b.find_floor(annex)) == doctest::Approx(0.04).epsilon(1e-15));

        // Floors of one sheet share one scale and follow it.
        p->share_scale(g, {f1});
        b = p->building();
        CHECK(b.find_floor(f1)->scale == bm::shared_scale(g));
        CHECK(b.metres_per_unit(*b.find_floor(f1)) == b.metres_per_unit(*b.find_floor(g)));
        p->set_scale(g, scale_or_fail(bm::two_point_scale({0, 0}, {0, 400}, 40.0)));
        CHECK(changes.count(pj::Change::Kind::FloorChanged, f1) == 2);  // shared, then followed
        b = p->building();
        CHECK(*b.metres_per_unit(*b.find_floor(f1)) == doctest::Approx(0.1).epsilon(1e-15));
        CHECK(bm::floors_sharing_scale(b, g) == std::vector<std::string>{f1});

        // Refused: another sheet, a chain, the source floor losing its own
        // scale or leaving, an invalid measurement, an unknown floor.
        CHECK(code_of([&] { p->share_scale(g, {annex}); }) == Code::Invalid);
        CHECK(code_of([&] { p->set_scale(bare, bm::shared_scale(g)); }) == Code::Invalid);
        CHECK(code_of([&] { p->share_scale(f1, {g}); }) == Code::Invalid);
        CHECK(code_of([&] { p->set_scale(g, bm::Scale{}); }) == Code::Invalid);
        CHECK(code_of([&] { p->set_scale(g, bm::shared_scale(f1)); }) == Code::Invalid);
        CHECK(code_of([&] { p->remove_floor(g); }) == Code::Invalid);
        bm::Scale same_points;
        same_points.method = bm::Scale::Method::TwoPoints;
        same_points.a = same_points.b = {5, 5};
        same_points.distance_m = 3.0;
        CHECK_FALSE(bm::two_point_scale({5, 5}, {5, 5}, 3.0));
        CHECK_FALSE(bm::two_point_scale({0, 0}, {5, 5}, 0.0));
        CHECK_FALSE(bm::two_point_scale({0, 0}, {5, 5}, std::nan("")));
        CHECK(code_of([&] { p->set_scale(annex, same_points); }) == Code::Invalid);
        CHECK(code_of([&] { p->set_scale("floor-99", bm::Scale{}); }) == Code::NotFound);
        CHECK(code_of([&] { p->share_scale(g, {"floor-99"}); }) == Code::NotFound);
        // A factor scale, and clearing a scale nobody shares.
        p->set_scale(bare, scale_or_fail(bm::factor_scale(0.02)));
        p->set_scale(bare, bm::Scale{});
        CHECK_FALSE(p->building().has_scale(*p->floor(bare)));
    }
    auto p = pj::Project::open(file);
    const bm::Building b = p->building();
    CHECK(b.find_floor(g)->scale == scale_or_fail(bm::two_point_scale({0, 0}, {0, 400}, 40.0)));
    CHECK(b.find_floor(f1)->scale == bm::shared_scale(g));
    CHECK(b.find_floor(annex)->scale.method == bm::Scale::Method::ScaleBar);
    CHECK(b.find_floor(bare)->scale == bm::Scale{});
}

TEST_CASE("project 7.3: a reference point on every floor aligns floors of different sources and scales (IND-09)") {
    const fs::path dir = scratch("align");
    write_file(dir / "ground.png", fake_png(3));
    write_file(dir / "upper.png", fake_png(4));
    const fs::path file = dir / "a.yantra";
    std::string g, u;
    {
        auto p = pj::Project::create(file, info());
        g = p->add_floor(floor_of("Ground floor", 0, p->add_source(dir / "ground.png").id));
        u = p->add_floor(floor_of("Upper floor", 1, p->add_source(dir / "upper.png").id));
        p->set_scale(g, scale_or_fail(bm::factor_scale(0.05)));
        p->set_scale(u, scale_or_fail(bm::factor_scale(0.02)));
        // The staircase corner: (100, 100) on the ground plan, (400, 300) upstairs.
        p->set_reference_point(g, bm::Point2{100, 100});
        bm::Building b = p->building();
        CHECK(bm::aligned(b, *b.find_floor(g)));
        CHECK_FALSE(bm::aligned(b, *b.find_floor(u)));
        CHECK_FALSE(bm::point_on_floor(b, *b.find_floor(g), {300, 100}, *b.find_floor(u)));
        p->set_reference_point(u, bm::Point2{400, 300});
        b = p->building();
        // 10 m east of the corner on the ground floor is 10 m east upstairs.
        const auto up = bm::point_on_floor(b, *b.find_floor(g), {300, 100}, *b.find_floor(u));
        REQUIRE(up);
        CHECK(up->x == doctest::Approx(900.0).epsilon(1e-12));
        CHECK(up->y == doctest::Approx(300.0).epsilon(1e-12));
        const auto back = bm::point_on_floor(b, *b.find_floor(u), *up, *b.find_floor(g));
        REQUIRE(back);
        CHECK(back->x == doctest::Approx(300.0).epsilon(1e-12));
        CHECK(back->y == doctest::Approx(100.0).epsilon(1e-12));
        // The scene of the ground floor shows the upstairs antenna above
        // the same building point (IND-44 input).
        bm::Cell cell{"", "DAS", "tetra", "tetra_cell", 390'000'000, {{"mcc", 901}, {"mnc", 1}, {"la", 7}, {"colour_code", 1}}};
        const std::string c = p->add_cell(cell);
        bm::Antenna a;
        a.position = *up;
        a.eirp_dbm = 10.0;
        a.cell = c;
        p->add_antenna(u, a);
        REQUIRE(p->building().find_floor(u)->antennas.size() == 1);
        CHECK(code_of([&] { p->set_reference_point("floor-99", std::nullopt); }) == Code::NotFound);
        CHECK(code_of([&] { p->set_reference_point(g, bm::Point2{std::nan(""), 0}); }) == Code::Invalid);
    }
    auto p = pj::Project::open(file);
    CHECK(p->floor(g)->reference_point == bm::Point2{100, 100});
    CHECK(p->floor(u)->reference_point == bm::Point2{400, 300});
    p->set_reference_point(u, std::nullopt);
    CHECK_FALSE(p->floor(u)->reference_point);
}

TEST_CASE("project 7.3: walls drawn as polylines, edited, classed from the editable table (IND-11)") {
    const fs::path dir = scratch("walls");
    const fs::path file = dir / "w.yantra";
    std::string g, f1;
    bm::Building expected;
    {
        auto p = pj::Project::create(file, info());
        g = p->add_floor(floor_of("Ground floor", 0));
        f1 = p->add_floor(floor_of("First floor", 1));
        Changes changes;
        p->subscribe([&](const pj::Change& c) { changes.seen.push_back(c); });
        CHECK(p->wall_classes() == bm::default_wall_classes());

        // Drawing: a polyline with a class gets a generated id; a wall may
        // have its own attenuation instead of a class.
        const std::string w1 = p->add_wall(g, bm::Wall{"", "concrete", {{100, 100}, {500, 100}, {500, 400}}, std::nullopt});
        const std::string w2 = p->add_wall(g, bm::Wall{"", "", {{100, 300}, {400, 300}}, 9.5});
        CHECK(w1 == "wall-1");
        CHECK(w2 == "wall-2");
        CHECK(changes.count(pj::Change::Kind::FloorChanged, g) == 2);
        // Several at once (accepted proposals): all or none.
        const auto ids = p->add_walls(f1, {bm::Wall{"", "glass", {{0, 0}, {10, 0}}, std::nullopt},
                                           bm::Wall{"door-a", "door", {{10, 0}, {12, 0}}, std::nullopt}});
        CHECK(ids == std::vector<std::string>{"wall-3", "door-a"});
        CHECK(code_of([&] {
                  p->add_walls(f1, {bm::Wall{"", "glass", {{0, 0}, {1, 0}}, std::nullopt},
                                    bm::Wall{"", "steel", {{0, 0}, {1, 0}}, std::nullopt}});
              }) == Code::Invalid);
        CHECK(p->walls(f1).size() == 2);
        // Refused walls.
        CHECK(code_of([&] { p->add_wall(g, bm::Wall{"", "brick", {{0, 0}}, std::nullopt}); }) == Code::Invalid);
        CHECK(code_of([&] { p->add_wall(g, bm::Wall{"", "", {{0, 0}, {1, 1}}, std::nullopt}); }) == Code::Invalid);
        CHECK(code_of([&] { p->add_wall(g, bm::Wall{"", "brick", {{0, 0}, {INFINITY, 1}}, std::nullopt}); }) ==
              Code::Invalid);
        CHECK(code_of([&] { p->add_wall(g, bm::Wall{"wall-1", "brick", {{0, 0}, {1, 1}}, std::nullopt}); }) ==
              Code::Invalid);
        CHECK(code_of([&] { p->add_wall("floor-99", bm::Wall{"", "brick", {{0, 0}, {1, 1}}, std::nullopt}); }) ==
              Code::NotFound);

        // Editing: pick the wall near a click, add a corner, drag it, split.
        bm::Floor gf = *p->floor(g);
        const auto hit = bm::nearest_wall(gf, {300, 104}, 10.0);
        REQUIRE(hit);
        CHECK(gf.walls[hit->wall].id == w1);
        CHECK(hit->segment == 0);
        bm::Wall edited = gf.walls[hit->wall];
        REQUIRE(bm::insert_vertex(edited, hit->segment + 1, hit->nearest));
        REQUIRE(bm::move_vertex(edited, 1, {300, 150}));
        p->update_wall(g, edited);
        CHECK(p->walls(g)[0].points == std::vector<bm::Point2>{{100, 100}, {300, 150}, {500, 100}, {500, 400}});
        const auto parts = bm::split_wall(p->walls(g)[0], 2);
        REQUIRE(parts);
        p->update_wall(g, parts->first);
        const std::string w5 = p->add_wall(g, parts->second);
        CHECK(w5 == "wall-4");  // refused calls hand out no id (their transaction is rolled back)
        CHECK(p->walls(g).size() == 3);
        CHECK(p->walls(g)[2].points == std::vector<bm::Point2>{{500, 100}, {500, 400}});
        CHECK(p->walls(g)[2].wall_class == "concrete");
        // Another class, the override removed.
        bm::Wall w2e = p->walls(g)[1];
        w2e.wall_class = "plasterboard";
        w2e.attenuation_db.reset();
        p->update_wall(g, w2e);
        CHECK(code_of([&] { p->update_wall(g, bm::Wall{"nope", "brick", {{0, 0}, {1, 1}}, std::nullopt}); }) ==
              Code::NotFound);
        p->remove_wall(f1, "door-a");
        CHECK(code_of([&] { p->remove_wall(f1, "door-a"); }) == Code::NotFound);
        CHECK(p->add_wall(f1, bm::Wall{"", "door", {{10, 0}, {12, 0}}, std::nullopt}) == "wall-5");  // never reused

        // The wall-class table: a project class, edited defaults, removal
        // only when unused, reset to the defaults.
        const bm::WallClass steel{"steel", "Steel sheet", {{380.0, 30.0}, {900.0, 35.0}}, 1.5};
        p->set_wall_class(steel);
        CHECK(changes.count(pj::Change::Kind::WallClasses, "steel") == 1);
        p->add_wall(f1, bm::Wall{"", "steel", {{0, 5}, {10, 5}}, std::nullopt});
        CHECK(code_of([&] { p->remove_wall_class("steel"); }) == Code::InUse);
        CHECK(code_of([&] { p->remove_wall_class("concrete"); }) == Code::InUse);
        CHECK(code_of([&] { p->remove_wall_class("unobtainium"); }) == Code::NotFound);
        CHECK(code_of([&] { p->set_wall_class(bm::WallClass{"bad", "Bad", {{900, 1}, {400, 2}}, std::nullopt}); }) ==
              Code::Invalid);
        CHECK(code_of([&] { p->set_wall_class(bm::WallClass{"", "No id", {{400, 1}}, std::nullopt}); }) ==
              Code::Invalid);
        CHECK(code_of([&] { p->set_wall_class(bm::WallClass{"none", "No band", {}, std::nullopt}); }) ==
              Code::Invalid);
        bm::WallClass concrete = *p->building().find_class("concrete");
        concrete.bands = {{390.0, 14.0}};
        p->set_wall_class(concrete);
        bm::Building b = p->building();
        // The attenuation at the measured frequency follows the table.
        CHECK(*b.wall_attenuation_db(b.find_floor(g)->walls[0], 390.0) == 14.0);
        CHECK(*b.wall_attenuation_db(b.find_floor(f1)->walls.back(), 600.0) ==
              doctest::Approx(30.0 + 5.0 * std::log(600.0 / 380.0) / std::log(900.0 / 380.0)));
        p->remove_wall_class("coated_glass");
        p->reset_wall_classes();
        b = p->building();
        CHECK(b.find_class("concrete")->bands == bm::default_wall_classes()[0].bands);
        CHECK(b.wall_classes.back().id == "coated_glass");  // added back at the end
        CHECK(b.find_class("steel") != nullptr);            // the project's own class stays
        CHECK(bm::walls_of_class(b, "steel") == 1);
        expected = b;
    }
    auto p = pj::Project::open(file);
    CHECK(p->building() == expected);
    CHECK(p->check().empty());
}

TEST_CASE("project 7.3: antennas placed with type, gain, pattern, height, cell and optional EIRP (IND-12)") {
    const fs::path dir = scratch("antennas");
    const fs::path file = dir / "a.yantra";
    std::string g, f1, das1, donor;
    bm::Building expected;
    {
        auto p = pj::Project::create(file, info());
        g = p->add_floor(floor_of("Ground floor", 0));
        f1 = p->add_floor(floor_of("First floor", 1));
        Changes changes;
        p->subscribe([&](const pj::Change& c) { changes.seen.push_back(c); });
        // The cells the antennas serve (LA/carrier, IND-12).
        const std::string t1 = p->add_cell(bm::Cell{"", "Indoor DAS", "tetra", "tetra_cell", 390'012'500,
                                                    {{"mcc", 901}, {"mnc", 1}, {"la", 100}, {"colour_code", 1}}});
        const std::string m1 = p->add_cell(bm::Cell{"macro", "Macro site", "tetra", "tetra_cell", 392'000'000,
                                                    {{"mcc", 901}, {"mnc", 1}, {"la", 200}, {"colour_code", 3}}});
        CHECK(t1 == "cell-1");
        CHECK(changes.count(pj::Change::Kind::Cells, t1) == 1);
        CHECK(code_of([&] { p->add_cell(bm::Cell{"macro", "", "", "", 0, {}}); }) == Code::Invalid);

        // An indoor DAS antenna: ceiling omni, no EIRP but feed power and losses.
        bm::Antenna das;
        das.name = "DAS 1";
        das.model = "ceiling omni";
        das.position = {250.5, 120.25};
        das.height_m = 2.9;
        das.gain_dbi = 2.15;
        das.feed_power_dbm = 15.0;
        das.cable_loss_db = 3.2;
        das.splitter_loss_db = 3.5;
        das.cell = t1;
        das1 = p->add_antenna(g, das);
        CHECK(das1 == "antenna-1");
        CHECK(changes.count(pj::Change::Kind::FloorChanged, g) == 1);
        // A directional panel aimed at a corridor end, with a known EIRP.
        bm::Antenna panel;
        panel.name = "Panel corridor";
        panel.model = "panel 65";
        panel.position = {800, 300};
        panel.height_m = 2.5;
        panel.gain_dbi = 7.0;
        panel.pattern = bm::directional_pattern(-30.0, 65.0, 18.0);
        CHECK(panel.pattern.azimuth_deg == 330.0);
        REQUIRE(bm::aim_antenna(panel, {800, 100}));  // straight up the plan: 0 degrees
        CHECK(panel.pattern.azimuth_deg == 0.0);
        panel.eirp_dbm = 12.5;
        panel.cell = t1;
        const std::string panel_id = p->add_antenna(g, panel);
        // An outdoor donor direction towards the macro site.
        bm::Antenna don;
        don.name = "Donor";
        don.type = bm::AntennaType::OutdoorDonor;
        don.position = {990, 10};
        don.height_m = 12.0;
        don.gain_dbi = 11.0;
        don.pattern.type = bm::AntennaPattern::Type::Table;
        don.pattern.step_deg = 90.0;
        don.pattern.gain_db = {0.0, -6.0, -20.0, -6.0};
        REQUIRE(bm::aim_antenna(don, {1990, 10}));  // east
        CHECK(*don.direction_deg == 90.0);
        CHECK(don.pattern.azimuth_deg == 90.0);
        don.elevation_deg = 2.5;
        don.cell = m1;
        donor = p->add_antenna(g, don);

        bm::Building b = p->building();
        const auto [df, da] = b.find_antenna(das1);
        REQUIRE(da);
        CHECK(df->id == g);
        CHECK(*da == bm::Antenna{das1, "DAS 1", "ceiling omni", bm::AntennaType::IndoorDas, {250.5, 120.25}, 2.9, 2.15,
                                 bm::AntennaPattern{}, std::nullopt, 15.0, 3.2, 3.5, t1, std::nullopt, std::nullopt});
        CHECK(*da->effective_eirp_dbm() == doctest::Approx(15.0 - 3.2 - 3.5 + 2.15));
        CHECK(bm::nearest_antenna(*df, {255, 125}, 10.0) == std::size_t{0});
        CHECK_FALSE(bm::nearest_antenna(*df, {400, 400}, 10.0));

        // Refused: an unknown cell, an elevation out of range, a used id,
        // a bad pattern, negative losses, an unknown floor.
        bm::Antenna bad = das;
        bad.cell = "cell-99";
        CHECK(code_of([&] { p->add_antenna(g, bad); }) == Code::Invalid);
        bad = don;
        bad.elevation_deg = 95.0;
        CHECK(code_of([&] { p->add_antenna(g, bad); }) == Code::Invalid);
        bad = das;
        bad.id = das1;
        CHECK(code_of([&] { p->add_antenna(f1, bad); }) == Code::Invalid);
        bad = panel;
        bad.pattern.beamwidth_deg = 0.0;
        CHECK(code_of([&] { p->add_antenna(g, bad); }) == Code::Invalid);
        bad = das;
        bad.cable_loss_db = -1.0;
        CHECK(code_of([&] { p->add_antenna(g, bad); }) == Code::Invalid);
        CHECK(code_of([&] { p->add_antenna("floor-99", das); }) == Code::NotFound);

        // Editing, moving to another floor, the cell in use, removing.
        bm::Antenna changed = *da;
        changed.eirp_dbm = 9.0;  // measured now
        changed.height_m = 3.1;
        p->update_antenna(changed);
        CHECK(p->antennas(g)[0] == changed);
        p->move_antenna(panel_id, f1, {100, 50});
        CHECK(p->antennas(g).size() == 2);
        REQUIRE(p->antennas(f1).size() == 1);
        CHECK(p->antennas(f1)[0].position == bm::Point2{100, 50});
        CHECK(p->antennas(f1)[0].pattern == panel.pattern);
        CHECK(changes.count(pj::Change::Kind::FloorChanged, f1) == 1);
        CHECK(code_of([&] { p->remove_cell(t1); }) == Code::InUse);
        bm::Cell macro = p->cells()[1];
        macro.carrier_hz = 392'025'000;
        macro.identity["la"] = 201;
        p->update_cell(macro);
        CHECK(p->cells()[1] == macro);
        const std::string spare = p->add_cell(bm::Cell{"", "Spare", "", "", 0, {}});
        p->remove_cell(spare);
        CHECK(code_of([&] { p->remove_cell(spare); }) == Code::NotFound);
        CHECK(code_of([&] { p->update_antenna(bm::Antenna{}); }) == Code::NotFound);
        CHECK(code_of([&] { p->move_antenna("antenna-99", g, {0, 0}); }) == Code::NotFound);
        const std::string extra = p->add_antenna(f1, das);
        p->remove_antenna(extra);
        CHECK(code_of([&] { p->remove_antenna(extra); }) == Code::NotFound);
        CHECK(p->add_antenna(f1, das) == "antenna-5");  // never reused
        expected = p->building();
        // Removing a floor removes its antennas (and walls).
        p->remove_floor(f1);
        CHECK_FALSE(p->building().find_antenna(panel_id).second);
        expected.floors.erase(expected.floors.begin() + 1);
    }
    auto p = pj::Project::open(file);
    CHECK(p->building() == expected);
    CHECK(p->check().empty());
}

TEST_CASE("project 7.3: plan units and metres keep their precision; distances are given in metres (IND-08)") {
    const fs::path dir = scratch("precision");
    const fs::path file = dir / "p.yantra";
    // Values without a short decimal form, tiny and large ones, negative
    // coordinates (outside the plan is allowed while drawing).
    const double odd = 0.1 + 0.2;
    const double next = std::nextafter(250.0, 1e9);
    bm::Floor f = floor_of("Ground floor", 0);
    f.plan_width = 12345.678901234567;
    f.scale = scale_or_fail(bm::two_point_scale({odd, 1e-9}, {next, 123456789.123}, 12.345678901234));
    f.reference_point = bm::Point2{-0.5, 1.0 / 3.0};
    f.walls.push_back(bm::Wall{"w", "brick", {{odd, -1e-300}, {1e15, 2.0 / 3.0}, {-7.25, 5e-324}}, 1.0 / 7.0});
    bm::Antenna a;
    a.id = "a";
    a.position = {std::sqrt(2.0), std::numbers::pi};
    a.height_m = 2.0 / 3.0;
    a.gain_dbi = -0.1;
    a.eirp_dbm = 1.0 / 9.0;
    a.direction_deg = 359.99999999999994;
    f.antennas.push_back(a);
    const bm::Cell cell{"c", "Max carrier", "", "", 9'000'000'000'000'000'000ULL, {{"big", std::numeric_limits<std::int64_t>::min()}}};
    {
        auto p = pj::Project::create(file, info());
        bm::Building b = p->building();
        b.cells.push_back(cell);
        b.floors.push_back(f);
        b.floors.back().id = "floor-1";
        b.floors.back().antennas[0].cell = "c";
        p->replace_building(b);
        // A carrier SQLite cannot hold (above int64) is refused, not wrapped.
        bm::Building too_big = b;
        too_big.cells[0].carrier_hz = std::numeric_limits<std::uint64_t>::max();
        CHECK(code_of([&] { p->replace_building(too_big); }) == Code::Invalid);
    }
    auto p = pj::Project::open(file);
    const bm::Building b = p->building();
    const bm::Floor& g = b.floors.at(0);
    // Bit for bit (IEEE doubles are stored as 8-byte REALs).
    CHECK(g.plan_width == f.plan_width);
    CHECK(g.scale == f.scale);
    CHECK(g.reference_point == f.reference_point);
    CHECK(g.walls == f.walls);
    CHECK(g.antennas[0].position == a.position);
    CHECK(g.antennas[0].height_m == a.height_m);
    CHECK(g.antennas[0].eirp_dbm == a.eirp_dbm);
    CHECK(g.antennas[0].direction_deg == a.direction_deg);
    CHECK(b.cells.at(0) == cell);
    // Metres: plan units x metres per unit, relative error at the double
    // rounding level; the scale gives the entered distance back.
    const double mpu = *b.metres_per_unit(g);
    CHECK(*bm::distance_m(b, g, g.scale.a, g.scale.b) == doctest::Approx(12.345678901234).epsilon(1e-14));
    const auto m = b.to_plan_metres(g, {1000.0, 2000.0});
    REQUIRE(m);
    CHECK(m->x == 1000.0 * mpu);
    CHECK(b.from_plan_metres(g, *m)->x == doctest::Approx(1000.0).epsilon(1e-15));
    CHECK(b.from_plan_metres(g, *m)->y == doctest::Approx(2000.0).epsilon(1e-15));
    // Display: centimetres for lengths, 0.1 m2 for areas, no "-0.00".
    CHECK(bm::format_length(12.344) == "12.34 m");
    CHECK(bm::format_length(12.346) == "12.35 m");
    CHECK(bm::format_length(-0.0) == "0.00 m");
    CHECK(bm::format_length(std::nullopt) == "not to scale");
    CHECK(bm::format_area(48.24) == "48.2 m²");
}

TEST_CASE("project 7.3: the example office round trips and a reopened project exports the same scenes (RAD-36, TST-33)") {
    const fs::path dir = scratch("office");
    write_file(dir / "office.png", fake_png(5));
    const fs::path file = dir / "o.yantra";
    bm::Building office = yantra::tests::example_office();
    {
        auto p = pj::Project::create(file, info());
        const auto src = p->add_source(dir / "office.png");
        // The example names its plan "plan-1": use the project's source.
        for (bm::PlanSource& s : office.sources) {
            s.id = src.id;
            s.sha256 = yantra::util::Sha256::of(fake_png(5));
        }
        for (bm::Floor& fl : office.floors)
            if (!fl.source.empty()) fl.source = src.id;
        p->replace_building(office);
    }
    auto p = pj::Project::open(file);
    bm::Building back = p->building();
    // The project orders floors by level; the example lists them so too.
    CHECK(back == office);
    // Ids handed out later stay clear of the example's ids.
    CHECK(p->add_wall("f0", bm::Wall{"", "brick", {{0, 0}, {1, 1}}, std::nullopt}).rfind("wall-", 0) == 0);
    CHECK(p->add_cell(bm::Cell{"", "New", "", "", 0, {}}) == "cell-1");
}

TEST_CASE("project 7.3: schema 1 and schema 2 files migrate to schema 3 and on (no scale, the default wall classes)") {
    const fs::path dir = scratch("migrate-3");
    for (int version : {1, 2}) {
        CAPTURE(version);
        const fs::path file = dir / ("v" + std::to_string(version) + ".yantra");
        pj::testing::create_at_version(file, version, pj::ProjectInfo{"Old", "Hall", "", ""});
        raw_sql(file,
                "INSERT INTO floors (id, name, level, height_m, page, plan_width, plan_height, created_utc) VALUES "
                "('floor-1', 'Ground floor', 0, 3.5, 0, 400, 200, '2026-01-01T00:00:00Z');"
                "UPDATE meta SET value = '2' WHERE key = 'next_floor_number';");
        if (version == 2) {
            raw_sql(file, "UPDATE floors SET trim_shape = 'polygon', quarter_turns = 1, deskew_deg = 1.5;"
                          "INSERT INTO floor_outline VALUES ('floor-1', 0, 0, 0), ('floor-1', 1, 400, 0), "
                          "('floor-1', 2, 200, 200);");
        }
        {
            auto p = pj::Project::open(file);
            CHECK(p->opened_version() == version);
            CHECK(p->meta().schema_version == pj::kSchemaVersion);
            CHECK(p->migration_backup() == fs::path(file.string() + ".schema" + std::to_string(version) + ".bak"));
            const bm::Floor f = *p->floor("floor-1");
            CHECK(f.height_m == 3.5);
            CHECK(f.scale == bm::Scale{});
            CHECK_FALSE(f.reference_point);
            CHECK(f.walls.empty());
            CHECK(f.antennas.empty());
            CHECK(f.trim.outline.size() == (version == 2 ? 3u : 0u));
            // The version-3 default table is the model's default table.
            CHECK(p->wall_classes() == bm::default_wall_classes());
            CHECK(p->cells().empty());
            // Schema 3 data can be stored in the migrated file.
            p->set_scale("floor-1", scale_or_fail(bm::factor_scale(0.05)));
            p->set_reference_point("floor-1", bm::Point2{10, 10});
            CHECK(p->add_wall("floor-1", bm::Wall{"", "brick", {{0, 0}, {100, 0}}, std::nullopt}) == "wall-1");
            CHECK(p->add_cell(bm::Cell{"", "Cell", "", "", 0, {}}) == "cell-1");
            CHECK(p->add_antenna("floor-1", bm::Antenna{}) == "antenna-1");
            CHECK(p->add_floor(floor_of("First floor", 1)) == "floor-2");
            CHECK(p->check().empty());
        }
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK(p->floor("floor-1")->walls.size() == 1);
        CHECK(p->floor("floor-1")->scale.metres_per_unit == 0.05);
    }
}

TEST_CASE("project 7.3: the file refuses rows that break the schema's rules (defensive constraints)") {
    const fs::path dir = scratch("constraints");
    const fs::path file = dir / "c.yantra";
    { auto p = pj::Project::create(file, info()); }
    raw_sql(file, "INSERT INTO floors (id, name, level, height_m, created_utc) VALUES ('floor-1', 'G', 0, 3, 'x')");
    auto refused = [&](const std::string& sql) {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK);
        sqlite3_exec(db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
        const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return rc != SQLITE_OK;
    };
    CHECK(refused("UPDATE floors SET scale_method = 'guess'"));
    CHECK(refused("UPDATE floors SET reference_x = 1"));  // x without y
    CHECK(refused("INSERT INTO walls (floor, position, wall_class) VALUES ('floor-1', 0, 'steel')"));
    CHECK(refused("INSERT INTO walls (floor, position) VALUES ('floor-1', 0)"));  // neither class nor attenuation
    CHECK(refused("INSERT INTO wall_class_bands VALUES ('brick', 5, -1, 3)"));
    CHECK(refused("INSERT INTO antennas (id, floor, position, type, x, y, height_m, gain_dbi, pattern, cell) "
                  "VALUES ('a', 'floor-1', 0, 'indoor_das', 0, 0, 1, 0, 'omni', 'no-cell')"));
    CHECK(refused("INSERT INTO antennas (id, floor, position, type, x, y, height_m, gain_dbi, pattern, elevation_deg) "
                  "VALUES ('a', 'floor-1', 0, 'indoor_das', 0, 0, 1, 0, 'omni', 91)"));
    CHECK_FALSE(refused("UPDATE floors SET reference_x = 1, reference_y = 2"));
}
