// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project schema 6 (IND-40..45, docs/formats/PROJECT.md): the propagation
// settings and the stored interpolation results; migration from every
// earlier schema.

#include <doctest/doctest.h>
#include <sqlite3.h>

#include <cmath>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using Code = pj::ProjectError::Code;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

Code code_of(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const pj::ProjectError& e) {
        return e.code();
    }
    FAIL("no ProjectError");
    return Code::Invalid;
}

bool raw_sql_fails(const fs::path& p, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(p.string().c_str(), &db) == SQLITE_OK);
    sqlite3_exec(db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return rc != SQLITE_OK;
}

pj::ProjectInfo info() { return pj::ProjectInfo{"Maps", "Hall", "", ""}; }

bm::Floor floor_of(const std::string& name, int level) {
    bm::Floor f;
    f.name = name;
    f.level = level;
    f.plan_width = 400;
    f.plan_height = 200;
    return f;
}

pj::StoredInterpolation result(const std::string& floor_id, const std::string& key, int cols = 3, int rows = 2) {
    pj::StoredInterpolation r;
    r.floor_id = floor_id;
    r.key = key;
    r.building_sha256 = std::string(64, 'a');
    r.points_sha256 = std::string(64, 'b');
    r.settings_sha256 = std::string(64, 'c');
    r.parameters_json = R"({"format":1})";
    r.result_json = R"({"label":"interpolated"})";
    r.origin_m = {0.5, -1.0};
    r.cell_m = 0.25;
    r.cols = cols;
    r.rows = rows;
    for (int i = 0; i < cols * rows * 12; ++i) r.cells.push_back(static_cast<std::uint8_t>(i * 7));
    return r;
}

}  // namespace

TEST_CASE("schema 6: propagation settings default, round trip, validation, change notice (IND-40)") {
    const fs::path dir = scratch("propagation");
    const fs::path file = dir / "p.yantra";
    std::vector<pj::Change> seen;
    {
        auto p = pj::Project::create(file, info());
        CHECK(p->propagation() == pj::PropagationSettings{});
        p->subscribe([&](const pj::Change& c) { seen.push_back(c); });
        pj::PropagationSettings s;
        s.grid_m = 0.5;
        s.max_reflections = 1;
        s.path_loss_exponent = 3.1;
        s.reference_distance_m = 2.0;
        s.reflection_loss_db = 8.0;
        s.rx_height_m = 1.2;
        s.other_floors = false;
        s.default_eirp_dbm = 15.0;
        CHECK(s.validate().empty());
        p->set_propagation(s);
        CHECK(p->propagation() == s);
        REQUIRE(seen.size() == 1);
        CHECK(seen[0].kind == pj::Change::Kind::Propagation);
        // Invalid values are refused and change nothing.
        auto bad = s;
        bad.grid_m = 0.01;
        CHECK(code_of([&] { p->set_propagation(bad); }) == Code::Invalid);
        bad = s;
        bad.max_reflections = 4;
        CHECK_FALSE(bad.validate().empty());
        bad = s;
        bad.path_loss_exponent = std::nan("");
        CHECK(code_of([&] { p->set_propagation(bad); }) == Code::Invalid);
        bad = s;
        bad.default_eirp_dbm = 99.0;
        CHECK(code_of([&] { p->set_propagation(bad); }) == Code::Invalid);
        CHECK(p->propagation() == s);
        CHECK(seen.size() == 1);
        CHECK(p->check().empty());
    }
    auto p = pj::Project::open(file);
    CHECK(p->propagation().path_loss_exponent == 3.1);
    CHECK_FALSE(p->propagation().other_floors);
    // The table itself refuses values outside the ranges.
    p.reset();
    CHECK(raw_sql_fails(file, "UPDATE propagation SET grid_m = 50 WHERE id = 1"));
    CHECK(raw_sql_fails(file, "INSERT INTO propagation (id) VALUES (2)"));
}

TEST_CASE("schema 6: interpolation results store, replace by key, list, read, remove, cascade (IND-45)") {
    const fs::path dir = scratch("interpolations");
    const fs::path file = dir / "p.yantra";
    std::int64_t kept = 0;
    std::string ground;
    {
        auto p = pj::Project::create(file, info());
        ground = p->add_floor(floor_of("Ground", 0));
        const std::string first = p->add_floor(floor_of("First", 1));
        std::vector<pj::Change> seen;
        p->subscribe([&](const pj::Change& c) { seen.push_back(c); });
        const auto a = p->store_interpolation(result(ground, "level|strongest|auto"));
        const auto b = p->store_interpolation(result(ground, "level|frequency:390000000|auto", 2, 2));
        const auto c = p->store_interpolation(result(first, "level|strongest|auto"));
        CHECK(a != b);
        REQUIRE(seen.size() == 3);
        CHECK(seen[0].kind == pj::Change::Kind::Interpolations);
        CHECK(seen[0].id == ground);
        CHECK(seen[2].id == first);
        // Listed by key, without cells unless asked for.
        auto list = p->interpolations(ground);
        REQUIRE(list.size() == 2);
        CHECK(list[0].key == "level|frequency:390000000|auto");
        CHECK(list[0].cells.empty());
        CHECK(list[1].cols == 3);
        CHECK(list[1].created_utc.size() == 20);
        CHECK(list[1].created_with.rfind("Yantra ", 0) == 0);
        list = p->interpolations(ground, true);
        CHECK(list[1].cells == result(ground, "x").cells);
        // Read back whole.
        auto r = p->interpolation(a);
        REQUIRE(r);
        auto want = result(ground, "level|strongest|auto");
        want.id = a;
        want.created_utc = r->created_utc;
        want.created_with = r->created_with;
        CHECK(*r == want);
        CHECK_FALSE(p->interpolation(999));
        // The same floor and key replaces the result.
        auto again = result(ground, "level|strongest|auto", 1, 1);
        again.points_sha256 = std::string(64, 'd');
        const auto a2 = p->store_interpolation(again);
        CHECK(p->interpolations(ground).size() == 2);
        CHECK_FALSE(p->interpolation(a));
        CHECK(p->interpolation(a2)->points_sha256 == std::string(64, 'd'));
        // Remove.
        p->remove_interpolation(b);
        CHECK(p->interpolations(ground).size() == 1);
        CHECK(seen.back().id == ground);
        CHECK(code_of([&] { p->remove_interpolation(b); }) == Code::NotFound);
        // Removing a floor removes its results.
        p->remove_floor(first);
        CHECK_FALSE(p->interpolation(c));
        CHECK(p->check().empty());
        kept = a2;
    }
    auto p = pj::Project::open(file);
    REQUIRE(p->interpolation(kept));
    CHECK(p->interpolation(kept)->cols == 1);
    CHECK(p->interpolations(ground, true)[0].cells.size() == 12);
}

TEST_CASE("schema 6: invalid interpolation results are refused") {
    const fs::path dir = scratch("interpolations-invalid");
    auto p = pj::Project::create(dir / "p.yantra", info());
    const std::string f = p->add_floor(floor_of("Ground", 0));
    auto r = result(f, "k");
    r.key.clear();
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::Invalid);
    r = result(f, "k");
    r.points_sha256 = "abc";
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::Invalid);
    r = result(f, "k");
    r.building_sha256 = std::string(64, 'A');  // lower case only
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::Invalid);
    r = result(f, "k");
    r.parameters_json = "[1, 2]";
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::Invalid);
    r = result(f, "k");
    r.result_json = "{broken";
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::Invalid);
    r = result(f, "k");
    r.cells.pop_back();
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::Invalid);
    r = result(f, "k");
    r.cell_m = 0.0;
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::Invalid);
    r = result(f, "k");
    r.rows = 0;
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::Invalid);
    r = result(f, "k");
    r.origin_m.x = std::nan("");
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::Invalid);
    r = result("floor-99", "k");
    CHECK(code_of([&] { p->store_interpolation(r); }) == Code::NotFound);
    CHECK(p->interpolations(f).empty());
}

TEST_CASE("schema 1..5 files migrate to schema 6: default settings, no results") {
    const fs::path dir = scratch("migrate-6");
    for (int version : {1, 2, 3, 4, 5}) {
        CAPTURE(version);
        const fs::path file = dir / ("v" + std::to_string(version) + ".yantra");
        pj::testing::create_at_version(file, version, pj::ProjectInfo{"Old", "Hall", "", ""});
        {
            auto p = pj::Project::open(file);
            CHECK(p->opened_version() == version);
            CHECK(p->meta().schema_version == pj::kSchemaVersion);
            CHECK(fs::exists(file.string() + ".schema" + std::to_string(version) + ".bak"));
            CHECK(p->propagation() == pj::PropagationSettings{});
            const std::string f = p->add_floor(floor_of("Ground", 0));
            CHECK(p->interpolations(f).empty());
            auto s = p->propagation();
            s.path_loss_exponent = 2.8;
            p->set_propagation(s);
            p->store_interpolation(result(f, "level|strongest|auto"));
            CHECK(p->check().empty());
        }
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK(p->propagation().path_loss_exponent == 2.8);
        REQUIRE(p->floors().size() == 1);
        CHECK(p->interpolations(p->floors()[0].id).size() == 1);
    }
}
