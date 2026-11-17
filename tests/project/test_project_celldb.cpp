// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project schema 5 (step 7.6; docs/formats/PROJECT.md): the cell database
// in the project's cells (DB-01): round trip of every column, parameters
// typed as stored, neighbour lists with inherited fields, match fields;
// any cell can be removed (no antennas in 0.4); building edits keep the
// database columns; the file's constraints; the migration of schema 1..4
// files.

#include <doctest/doctest.h>
#include <sqlite3.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "yantra/core/celldb/database.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
namespace yc = yantra::celldb;
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

bool raw_sql_fails(const fs::path& p, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(p.string().c_str(), &db) == SQLITE_OK);
    sqlite3_exec(db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return rc != SQLITE_OK;
}

pj::ProjectInfo info() { return pj::ProjectInfo{"Cells", "Hall", "", ""}; }

bm::Floor floor_of(const std::string& name, int level) {
    bm::Floor f;
    f.name = name;
    f.level = level;
    f.plan_width = 400;
    f.plan_height = 200;
    return f;
}

yc::CellDatabase example() {
    yc::CellDatabase db;
    yc::CellEntry a;
    a.cell.id = "site-a";
    a.cell.label = "Site A";
    a.cell.technology_id = "tetra";
    a.cell.schema_id = "tetra_cell";
    a.set_field("frequency_hz", std::int64_t{390'000'000});
    a.set_field("mcc", std::int64_t{206});
    a.set_field("mnc", std::int64_t{1});
    a.set_field("la", std::int64_t{0x12C});
    a.set_field("colour_code", std::int64_t{3});
    a.set_field("rxlev_access_min_dbm", std::int64_t{-110});
    a.set_field("offset", 6.25);
    a.set_field("remark", std::string("roof"));
    a.site_name = "Tower";
    a.sector = "2";
    a.azimuth_deg = 240.0;
    a.colour = "#00aa55";
    a.notes = "line one\nline two";
    a.origin = yc::Origin::Import;
    a.plugin_version = "0.2.2";
    yc::NeighbourRef n1;
    n1.fields["frequency_hz"] = std::int64_t{390'100'000};
    n1.fields["la"] = std::int64_t{0x12C};
    n1.inherited.insert("la");
    yc::NeighbourRef n2;
    n2.fields["frequency_hz"] = std::int64_t{390'200'000};
    a.neighbours = {n1, n2};
    db.add(a);
    yc::CellEntry b;
    b.cell.technology_id = "tetra";
    b.cell.schema_id = "tetra_cell";
    b.set_field("frequency_hz", std::int64_t{390'100'000});
    b.origin = yc::Origin::Learned;
    db.add(b);  // cell-1
    db.set_match_fields("tetra", {"la", "frequency_hz"});
    return db;
}

}  // namespace

TEST_CASE("DB-01 project 7.6: the cell database round-trips through the project file, every column and type") {
    const fs::path dir = scratch("celldb-round-trip");
    const fs::path file = dir / "p.yantra";
    const yc::CellDatabase db = example();
    {
        auto p = pj::Project::create(file, info());
        CHECK(p->meta().schema_version == pj::kSchemaVersion);  // new files: the current schema (>= 5, DB-01)
        CHECK(p->cell_database().entries().empty());
        int changes = 0;
        p->subscribe([&](const pj::Change& c) { changes += c.kind == pj::Change::Kind::Cells ? 1 : 0; });
        p->set_cell_database(db);
        CHECK(changes == 1);
        CHECK(p->cell_database() == db);
        // The project's cells are the entries' cells (DB-01, one list).
        REQUIRE(p->cells().size() == 2);
        CHECK(p->cells()[0] == db.entries()[0].cell);
        // A new cell continues the numbering after cell-1.
        CHECK(p->add_cell(bm::Cell{"", "Next", "", "", 0, {}}) == "cell-2");
        CHECK(p->check().empty());
    }
    auto p = pj::Project::open(file);
    const yc::CellDatabase back = p->cell_database();
    REQUIRE(back.entries().size() == 3);
    const yc::CellEntry& a = back.entries()[0];
    CHECK(a == db.entries()[0]);
    CHECK(std::holds_alternative<double>(a.parameters.at("offset")));
    CHECK(std::holds_alternative<std::string>(a.parameters.at("remark")));
    CHECK(a.neighbours[0].inherited.count("la") == 1);
    CHECK(back.match_fields("tetra") == std::vector<std::string>{"la", "frequency_hz"});
    CHECK(back.entries()[2].origin == yc::Origin::Manual);  // added as a plain cell

    // Building edits keep the database columns of the cells.
    bm::Cell c = a.cell;
    c.label = "Site A (renamed)";
    p->update_cell(c);
    const yc::CellEntry after = *p->cell_database().find("site-a");
    CHECK(after.cell.label == "Site A (renamed)");
    CHECK(after.site_name == "Tower");
    CHECK(after.neighbours.size() == 2);
    // Removing a cell removes its parameters and neighbours (cascade).
    p->remove_cell("site-a");
    CHECK(p->cell_database().find("site-a") == nullptr);
    CHECK(p->check().empty());
}

TEST_CASE("DB-01 project 7.6: invalid entries are refused and nothing changes") {
    const fs::path dir = scratch("celldb-in-use");
    auto p = pj::Project::create(dir / "p.yantra", info());
    p->set_cell_database(example());
    p->add_floor(floor_of("Ground", 0));
    const yc::CellDatabase before = p->cell_database();
    yc::CellDatabase bad = before;
    yc::CellEntry e = *bad.find("cell-1");
    e.colour = "nope";
    // CellDatabase::update validates too; the project checks entries it is given.
    CHECK_THROWS_AS(bad.update(e), yc::CellDbError);
    // Replacing with fewer cells works.
    yc::CellDatabase only_a;
    only_a.add(*before.find("site-a"));
    p->set_cell_database(only_a);
    CHECK(p->cells().size() == 1);
    CHECK(p->check().empty());
}

TEST_CASE("DB-01 project 7.6: the file refuses cell database rows that break the schema's rules") {
    const fs::path dir = scratch("celldb-constraints");
    const fs::path file = dir / "c.yantra";
    {
        auto p = pj::Project::create(file, info());
        p->set_cell_database(example());
    }
    CHECK(raw_sql_fails(file, "UPDATE cells SET origin = 'robot'"));
    CHECK(raw_sql_fails(file, "UPDATE cells SET azimuth_deg = 360"));
    CHECK(raw_sql_fails(file, "INSERT INTO cell_parameters VALUES ('nope', 'x', 1)"));            // no such cell
    CHECK(raw_sql_fails(file, "INSERT INTO cell_parameters VALUES ('site-a', 'x', x'00')"));      // a blob
    CHECK(raw_sql_fails(file, "INSERT INTO cell_neighbours VALUES ('site-a', 31, 'la', 1, 0)"));  // > 31 entries
    CHECK(raw_sql_fails(file, "INSERT INTO cell_neighbours VALUES ('site-a', 5, '', 1, 0)"));
    CHECK(raw_sql_fails(file, "INSERT INTO cell_neighbours VALUES ('site-a', 5, 'la', 1, 2)"));
    // A value the model refuses is reported as damage, not dropped.
    raw_sql(file, "INSERT INTO cell_parameters VALUES ('site-a', 'Bad Name', 1)");
    auto p = pj::Project::open(file);
    CHECK(code_of([&] { (void)p->cell_database(); }) == Code::Damaged);
}

TEST_CASE("DB-01 project 7.6: schema 1..4 files migrate to schema 5; their cells become database entries") {
    const fs::path dir = scratch("migrate-5");
    for (int version : {1, 2, 3, 4}) {
        CAPTURE(version);
        const fs::path file = dir / ("v" + std::to_string(version) + ".yantra");
        pj::testing::create_at_version(file, version, pj::ProjectInfo{"Old", "Hall", "", ""});
        if (version >= 3)
            raw_sql(file,
                    "INSERT INTO cells (id, position, label, technology_id, schema_id, carrier_hz) VALUES "
                    "('cell-1', 0, 'Old cell', 'tetra', 'tetra_cell', 390000000);"
                    "INSERT INTO cell_identity VALUES ('cell-1', 'la', 300);"
                    "INSERT OR REPLACE INTO meta (key, value) VALUES ('next_cell_number', '2');");
        {
            auto p = pj::Project::open(file);
            CHECK(p->opened_version() == version);
            CHECK(p->meta().schema_version == pj::kSchemaVersion);
            CHECK(fs::exists(file.string() + ".schema" + std::to_string(version) + ".bak"));
            yc::CellDatabase db = p->cell_database();
            if (version >= 3) {
                REQUIRE(db.entries().size() == 1);
                const yc::CellEntry& e = db.entries()[0];
                CHECK(e.cell.label == "Old cell");
                CHECK(e.cell.identity.at("la") == 300);
                CHECK(e.origin == yc::Origin::Manual);
                CHECK(e.site_name.empty());
                CHECK_FALSE(e.azimuth_deg);
                CHECK(e.neighbours.empty());
            } else {
                CHECK(db.entries().empty());
            }
            yc::CellEntry n;
            n.cell.technology_id = "tetra";
            n.set_field("frequency_hz", std::int64_t{390'100'000});
            n.site_name = "New";
            db.add(n);
            p->set_cell_database(db);
            CHECK(p->check().empty());
        }
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK(p->cell_database().entries().size() == (version >= 3 ? 2u : 1u));
        CHECK(p->cell_database().entries().back().site_name == "New");
    }
}
