// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-46: project files of Yantra 0.3 and earlier open in 0.4. Their
// walls, wall classes, antennas and ray-tracing maps are dropped on load
// (the migration to schema 8, after a byte-for-byte backup of the old
// file), the open says once what it dropped (Project::migration_note), and
// the floors, cells, recordings, sessions and points are kept. The fixture
// tests/fixtures/project/fraunces_0_3.yantra is the Fraunces demo project
// as Yantra 0.2.0 (0.3 spec) made it, schema 7, its plan sources as
// references (no image content).

#include <doctest/doctest.h>
#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
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

// One integer from a query on a closed project file.
long long query_int(const fs::path& p, const std::string& sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(p.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(st) == SQLITE_ROW);
    const long long v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    sqlite3_close(db);
    return v;
}

}  // namespace

TEST_CASE("IND-46 a project of Yantra 0.3 opens: walls, wall classes, antennas and ray-tracing maps dropped and noted once") {
    const fs::path dir = scratch("fraunces-0.3");
    const fs::path fixture = fs::path(YANTRA_TEST_DATA_DIR) / "project" / "fraunces_0_3.yantra";
    const fs::path file = dir / "fraunces.yantra";
    fs::copy_file(fixture, file);
    const std::string original = read_file(fixture);
    REQUIRE(original.size() > 100'000);
    // What 0.3 held (the fixture is schema 7).
    CHECK(query_int(file, "PRAGMA user_version") == 7);
    CHECK(query_int(file, "SELECT count(*) FROM walls") == 41);
    CHECK(query_int(file, "SELECT count(*) FROM antennas") == 5);
    CHECK(query_int(file, "SELECT count(*) FROM interpolations") == 2);

    const fs::path backup = file.string() + ".schema7.bak";
    {
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == 7);
        CHECK(p->meta().schema_version == pj::kSchemaVersion);
        REQUIRE(p->migration_note());
        const pj::MigrationNote& n = *p->migration_note();
        CHECK(n.from_version == 7);
        CHECK(n.walls == 41);
        CHECK(n.antennas == 5);
        CHECK(n.wall_classes == 0);  // only the default classes
        CHECK(n.ray_tracing_maps == 2);
        CHECK(n.backup == backup);
        CHECK(p->migration_backup() == backup);

        // Kept: the project, its floors (trims, scales, reference points),
        // cells, recordings, sessions and points.
        CHECK(p->meta().info.name == "Fraunces Tavern (demo)");
        const auto floors = p->floors();
        REQUIRE(floors.size() == 2);
        std::size_t sessions = 0, points = 0;
        for (const auto& f : floors) {
            CAPTURE(f.id);
            CHECK(f.example_id.rfind("fraunces-", 0) == 0);
            CHECK(f.trim.outline.size() == 4);
            CHECK(p->building().has_scale(f));
            CHECK(f.reference_point.has_value());
            sessions += p->sessions(f.id).size();
            points += p->point_count(f.id);
            // The ray-tracing maps are gone; IND-41 makes a map on request.
            CHECK(p->interpolations(f.id).empty());
        }
        CHECK(sessions == 2);
        CHECK(points == 151);
        CHECK(p->cells().size() == 2);
        CHECK(p->recordings().size() == 2);
        CHECK(p->sources().size() == 2);
        CHECK(p->interpolation_settings() == pj::InterpolationSettings{});
        CHECK(p->building().validate().empty());
    }
    // The backup is the old file, byte for byte; the migrated file has no
    // walls, antennas or slabs any more.
    REQUIRE(fs::exists(backup));
    CHECK(read_file(backup) == original);
    CHECK(query_int(file, "PRAGMA user_version") == pj::kSchemaVersion);
    CHECK(query_int(file, "SELECT count(*) FROM sqlite_master WHERE name IN ('walls', 'wall_points', "
                          "'wall_classes', 'wall_class_bands', 'antennas', 'antenna_pattern_gains', 'propagation')") == 0);
    CHECK(query_int(file, "SELECT count(*) FROM pragma_table_info('building') WHERE name = 'floor_attenuation_db'") == 0);
    CHECK(query_int(file, "SELECT count(*) FROM pragma_table_info('floors') WHERE name = 'slab_attenuation_db'") == 0);
    CHECK(query_int(file, "SELECT count(*) FROM meta WHERE key IN ('next_wall_number', 'next_antenna_number')") == 0);

    // A second open: nothing to drop, no note; the backup stays as it is.
    {
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK_FALSE(p->migration_note());
        CHECK(p->migration_backup().empty());
        CHECK(p->floors().size() == 2);
    }
    CHECK(read_file(backup) == original);
}

TEST_CASE("IND-46 the note counts the project's own wall classes, walls and antennas of every floor; none for an empty old file") {
    const fs::path dir = scratch("note-counts");
    const fs::path file = dir / "v6.yantra";
    pj::testing::create_at_version(file, 6, pj::ProjectInfo{"Old", "Hall", "", ""});
    raw_sql(file,
            "INSERT INTO floors (id, name, level, height_m, created_utc) VALUES "
            "('floor-1', 'Ground', 0, 3, 'x'), ('floor-2', 'First', 1, 3, 'x');"
            "INSERT INTO wall_classes (id, position, name) VALUES ('steel', 8, 'Steel'), ('felt', 9, 'Felt');"
            "INSERT INTO wall_class_bands VALUES ('steel', 0, 400, 30), ('felt', 0, 400, 1);"
            "INSERT INTO walls (floor, position, id, wall_class) VALUES ('floor-1', 0, 'wall-1', 'steel'), "
            "('floor-2', 0, 'wall-2', 'brick'), ('floor-2', 1, 'wall-3', 'felt');"
            "INSERT INTO wall_points VALUES (1, 0, 0, 0), (1, 1, 1, 0), (2, 0, 0, 0), (2, 1, 0, 1), (3, 0, 1, 1), "
            "(3, 1, 2, 2);"
            "INSERT INTO antennas (id, floor, position, type, x, y, height_m, gain_dbi, pattern) VALUES "
            "('antenna-1', 'floor-2', 0, 'indoor_das', 5, 5, 2.7, 2, 'omni');");
    {
        auto p = pj::Project::open(file);
        REQUIRE(p->migration_note());
        CHECK(p->migration_note()->from_version == 6);
        CHECK(p->migration_note()->walls == 3);
        CHECK(p->migration_note()->wall_classes == 2);
        CHECK(p->migration_note()->antennas == 1);
        CHECK(p->migration_note()->ray_tracing_maps == 0);
        CHECK(p->floors().size() == 2);
    }
    // Without a backup the note says so (an empty path).
    const fs::path other = dir / "v3.yantra";
    pj::testing::create_at_version(other, 3, pj::ProjectInfo{"Old", "", "", ""});
    raw_sql(other, "INSERT INTO floors (id, name, level, height_m, created_utc) VALUES ('floor-1', 'G', 0, 3, 'x');"
                   "INSERT INTO walls (floor, position, wall_class) VALUES ('floor-1', 0, 'concrete');");
    pj::ProjectOptions no_backup;
    no_backup.backup_before_migration = false;
    {
        auto p = pj::Project::open(other, no_backup);
        REQUIRE(p->migration_note());
        CHECK(p->migration_note()->walls == 1);
        CHECK(p->migration_note()->backup.empty());
    }
    // An old file with nothing to drop migrates without a note.
    const fs::path empty = dir / "v7.yantra";
    pj::testing::create_at_version(empty, 7, pj::ProjectInfo{"Old", "", "", ""});
    auto p = pj::Project::open(empty);
    CHECK(p->opened_version() == 7);
    CHECK_FALSE(p->migration_note());
    CHECK(fs::exists(empty.string() + ".schema7.bak"));
}
