// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the "About building" information in the project file (schema 9,
// docs/formats/PROJECT.md "about_building"): round trip, validation, the
// change notice, the survey date of a new project, and the migration of
// older files (IND-46 style): the 0.3 fixture (schema 7) and a project of
// Yantra 0.4 (tests/fixtures/project/fraunces_0_4.yantra: the Fraunces
// demo project as the 14.x build made it, schema 8, its plan sources as
// references and without stored maps).

#include <doctest/doctest.h>
#include <sqlite3.h>

#include <filesystem>
#include <string>
#include <vector>

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

pj::ProjectOptions clock_at(const std::string& iso) {
    pj::ProjectOptions o;
    o.clock = [iso] { return iso; };
    return o;
}

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

pj::AboutBuilding sample() {
    pj::AboutBuilding a;
    a.address = "Example Street 1\n1000 Example City";
    a.latitude = 50.8466;
    a.longitude = 4.3528;
    a.map_zoom = 18;
    a.surveyor = "Demo surveyor";
    a.survey_date = "2026-11-02";
    a.notes = "Line one\nLine two: caf\xc3\xa9";
    a.photo = {0x89, 'P', 'N', 'G', 0, 1, 2, 3, 0xFF};
    a.photo_name = "front.png";
    a.photo_source = "file";
    return a;
}

}  // namespace

TEST_CASE("IND-47 About building: a new project's survey date is today, every field round trips, changes notify") {
    const fs::path dir = scratch("about-round-trip");
    const fs::path file = dir / "a.yantra";
    {
        auto p = pj::Project::create(file, pj::ProjectInfo{"About", "", "", ""}, clock_at("2026-11-05T12:00:00Z"));
        const pj::AboutBuilding fresh = p->about();
        CHECK(fresh.address.empty());
        CHECK_FALSE(fresh.has_position());
        CHECK(fresh.map_zoom == 17);
        CHECK(fresh.survey_date == "2026-11-05");  // the day the project was made
        CHECK(fresh.photo.empty());
        CHECK(fresh.photo_source.empty());
        int about_changes = 0;
        p->subscribe([&](const pj::Change& c) { about_changes += c.kind == pj::Change::Kind::About ? 1 : 0; });
        p->set_about(sample());
        CHECK(about_changes == 1);
        CHECK(p->about() == sample());
    }
    {
        auto p = pj::Project::open(file);
        CHECK(p->about() == sample());
        // The photo removed, the position cleared.
        pj::AboutBuilding a = sample();
        a.photo.clear();
        a.photo_name = "ignored.png";
        a.photo_source.clear();
        a.latitude.reset();
        a.longitude.reset();
        a.survey_date.clear();
        p->set_about(a);
        const pj::AboutBuilding b = p->about();
        CHECK(b.photo.empty());
        CHECK(b.photo_name.empty());  // a name only with a photo
        CHECK_FALSE(b.has_position());
        CHECK(b.survey_date.empty());
        CHECK(p->check().empty());
    }
    CHECK(query_int(file, "SELECT count(*) FROM about_building") == 1);
}

TEST_CASE("IND-47 About building: invalid values are refused and nothing changes") {
    const fs::path dir = scratch("about-invalid");
    auto p = pj::Project::create(dir / "a.yantra", pj::ProjectInfo{"About", "", "", ""});
    p->set_about(sample());
    const auto refused = [&](const pj::AboutBuilding& a) {
        CHECK_THROWS_AS(p->set_about(a), pj::ProjectError);
        CHECK(p->about() == sample());
    };
    pj::AboutBuilding a = sample();
    a.longitude.reset();
    refused(a);
    a = sample();
    a.latitude = 86.0;
    refused(a);
    a = sample();
    a.longitude = -180.5;
    refused(a);
    a = sample();
    a.map_zoom = 0;
    refused(a);
    a.map_zoom = 20;
    refused(a);
    for (const char* d : {"2026-13-01", "2026-1-01", "today", "2026-11-00", "2026/11/02"}) {
        a = sample();
        a.survey_date = d;
        refused(a);
    }
    a = sample();
    a.photo_source = "scanner";
    refused(a);
    a = sample();
    a.photo_source.clear();  // a photo without its source
    refused(a);
    a = sample();
    a.photo.clear();  // a source without a photo
    refused(a);
    a = sample();
    a.photo.assign(pj::kMaxPhotoBytes + 1, 0);
    refused(a);
}

TEST_CASE("IND-47 IND-46 a project of Yantra 0.3 and one of Yantra 0.4 migrate to schema 9 with an empty About") {
    struct Case {
        const char* fixture;
        int version;
    };
    for (const Case c : {Case{"fraunces_0_3.yantra", 7}, Case{"fraunces_0_4.yantra", 8}}) {
        CAPTURE(c.fixture);
        const fs::path dir = scratch(std::string("about-migrate-") + std::to_string(c.version));
        const fs::path file = dir / "fraunces.yantra";
        fs::copy_file(fs::path(YANTRA_TEST_DATA_DIR) / "project" / c.fixture, file);
        CHECK(query_int(file, "PRAGMA user_version") == c.version);
        const fs::path backup = file.string() + ".schema" + std::to_string(c.version) + ".bak";
        {
            auto p = pj::Project::open(file, clock_at("2026-11-18T08:00:00Z"));
            CHECK(p->opened_version() == c.version);
            CHECK(p->meta().schema_version == pj::kSchemaVersion);
            CHECK(p->migration_backup() == backup);
            // Only the 0.3 file drops something (walls, antennas, maps).
            CHECK(p->migration_note().has_value() == (c.version < 8));
            const pj::AboutBuilding a = p->about();
            CHECK(a.address.empty());
            CHECK(a.surveyor.empty());
            CHECK(a.notes.empty());
            CHECK(a.photo.empty());
            CHECK_FALSE(a.has_position());
            CHECK(a.survey_date == "2026-11-18");  // the day of the migration
            // Kept: the project, its floors, sessions and points.
            CHECK(p->meta().info.name == "Fraunces Tavern (demo)");
            REQUIRE(p->floors().size() == 2);
            std::size_t points = 0;
            for (const auto& f : p->floors())
                for (const auto& s : p->sessions(f.id)) points += p->points(s.id).size();
            CHECK(points > 100);
            p->set_about(sample());
        }
        CHECK(fs::exists(backup));
        CHECK(query_int(backup, "PRAGMA user_version") == c.version);
        CHECK(query_int(file, "PRAGMA user_version") == pj::kSchemaVersion);
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK_FALSE(p->migration_note().has_value());
        CHECK(p->about() == sample());
        CHECK(p->check().empty());
    }
}

TEST_CASE("IND-47 the file refuses About rows that break the schema's rules (defensive)") {
    const fs::path dir = scratch("about-rules");
    const fs::path file = dir / "a.yantra";
    pj::Project::create(file, pj::ProjectInfo{"About", "", "", ""})->close();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK);
    for (const char* bad : {"UPDATE about_building SET latitude = 10 WHERE id = 1",
                            "UPDATE about_building SET latitude = 99, longitude = 1 WHERE id = 1",
                            "UPDATE about_building SET map_zoom = 25 WHERE id = 1",
                            "UPDATE about_building SET photo_source = 'web' WHERE id = 1",
                            "INSERT INTO about_building (id) VALUES (2)"}) {
        CAPTURE(bad);
        CHECK(sqlite3_exec(db, bad, nullptr, nullptr, nullptr) != SQLITE_OK);
    }
    sqlite3_close(db);
}
