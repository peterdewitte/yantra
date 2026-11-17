// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project schema 3 (step 7.3; docs/formats/PROJECT.md): the floor scale
// from two points and a known distance, shared by the floors of one sheet
// (IND-08); the alignment point between floors of different sources and
// scales (IND-09); cells; units and precision; the round trip through the
// file of all of it and the migration of schema 1 and 2 files. The walls,
// wall classes and antennas of 0.3 are removed with their
// requirements; test_project_migration.cpp covers dropping them (IND-46).

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
        CHECK(code_of([&] { p->set_reference_point("floor-99", std::nullopt); }) == Code::NotFound);
        CHECK(code_of([&] { p->set_reference_point(g, bm::Point2{std::nan(""), 0}); }) == Code::Invalid);
    }
    auto p = pj::Project::open(file);
    CHECK(p->floor(g)->reference_point == bm::Point2{100, 100});
    CHECK(p->floor(u)->reference_point == bm::Point2{400, 300});
    p->set_reference_point(u, std::nullopt);
    CHECK_FALSE(p->floor(u)->reference_point);
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
    f.plan_height = std::numbers::pi;
    const bm::Cell cell{"c", "Max carrier", "", "", 9'000'000'000'000'000'000ULL, {{"big", std::numeric_limits<std::int64_t>::min()}}};
    {
        auto p = pj::Project::create(file, info());
        bm::Building b = p->building();
        b.cells.push_back(cell);
        b.floors.push_back(f);
        b.floors.back().id = "floor-1";
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
    CHECK(g.plan_height == f.plan_height);
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

TEST_CASE("project 7.3: the example office round trips (TST-33)") {
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
    CHECK(p->add_cell(bm::Cell{"", "New", "", "", 0, {}}) == "cell-1");
}

TEST_CASE("project 7.3: schema 1 and schema 2 files migrate to schema 3 and on (no scale)") {
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
            CHECK(f.trim.outline.size() == (version == 2 ? 3u : 0u));
            CHECK(p->cells().empty());
            // Nothing was dropped on the way through schema 3 (IND-46: the
            // default wall classes alone are no note).
            CHECK_FALSE(p->migration_note());
            // Schema 3 data can be stored in the migrated file.
            p->set_scale("floor-1", scale_or_fail(bm::factor_scale(0.05)));
            p->set_reference_point("floor-1", bm::Point2{10, 10});
            CHECK(p->add_cell(bm::Cell{"", "Cell", "", "", 0, {}}) == "cell-1");
            CHECK(p->add_floor(floor_of("First floor", 1)) == "floor-2");
            CHECK(p->check().empty());
        }
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK(p->floor("floor-1")->reference_point == bm::Point2{10, 10});
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
    CHECK(refused("INSERT INTO cells (id, position, carrier_hz) VALUES ('c', 0, -1)"));
    CHECK(refused("INSERT INTO cell_identity VALUES ('no-cell', 'la', 1)"));
    CHECK(refused("INSERT INTO walls (floor, position) VALUES ('floor-1', 0)"));  // no such table since schema 8
    CHECK_FALSE(refused("UPDATE floors SET reference_x = 1, reference_y = 2"));
}
