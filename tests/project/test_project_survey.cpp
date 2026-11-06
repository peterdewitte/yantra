// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Project schema 4 (step 7.5; docs/formats/PROJECT.md): the project's
// recordings (REC-02/03), its walk tests and manual-point sets (sessions,
// IND-20) and their measured points with plan positions in plan units and
// metres; metres follow the floor scale, a trim edit keeps the points on
// the sheet, a floor with sessions is not removed; the migration of
// schema 1, 2 and 3 files.

#include "yantra/core/building/edit.h"
#include "yantra/core/project/project.h"

#include <doctest/doctest.h>
#include <sqlite3.h>

#include <cmath>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

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

bm::Floor floor_of(const std::string& name, int level, double w = 400, double h = 200) {
    bm::Floor f;
    f.name = name;
    f.level = level;
    f.plan_width = w;
    f.plan_height = h;
    return f;
}

pj::StoredPoint point(std::int64_t seq, double x, double y, std::uint32_t group = 1,
                      const std::string& kind = "walk") {
    pj::StoredPoint p;
    p.sequence = seq;
    p.time_ns = 1'000'000'000LL * seq;
    p.utc_ns = 1'790'000'000'000'000'000LL + p.time_ns;
    p.plan = {x, y};
    p.kind = kind;
    p.group = group;
    p.sample = R"({"carriers":[{"level":)" + std::to_string(-60 - seq) + "}]}";
    return p;
}

const std::string kHash(64, 'a');

}  // namespace

TEST_CASE("project 7.5: walk tests, their points and recordings are stored and come back unchanged (IND-20, REC-03)") {
    const fs::path dir = scratch("survey-round-trip");
    const fs::path file = dir / "s.yantra";
    std::string ground, first;
    std::int64_t walk = 0, manual = 0, rec = 0;
    pj::Session walk_in;
    std::vector<pj::StoredPoint> walk_points;
    {
        auto p = pj::Project::create(file, pj::ProjectInfo{"Survey", "Hall", "", ""});
        ground = p->add_floor(floor_of("Ground floor", 0));
        first = p->add_floor(floor_of("First floor", 1));
        p->set_scale(ground, *bm::factor_scale(0.05));
        std::vector<pj::Change::Kind> changes;
        p->subscribe([&](const pj::Change& c) { changes.push_back(c.kind); });

        walk_in.floor_id = ground;
        walk_in.kind = "walk";
        walk_in.name = "Ground floor walk";
        walk_in.radio = "simulator 0.2.2, simulator:0";
        walk_in.calibration = R"({"key":"simulator|0|"})";
        walk_in.notes = "Morning, doors \"open\" \xc3\xa9";
        walk_in.recording_session = "3f0c9a1b2d4e5f60";
        walk_in.plugins = R"({"simulator":"0.2.2"})";
        walk = p->add_session(walk_in);
        CHECK(walk > 0);
        // Several walk tests per floor (IND-20): a manual set on the same floor.
        pj::Session m = walk_in;
        m.kind = "manual";
        m.name = "Rooms";
        m.started_utc = "2026-11-05T10:00:00Z";
        manual = p->add_session(m);
        CHECK(manual != walk);
        CHECK(changes == std::vector{pj::Change::Kind::Sessions, pj::Change::Kind::Sessions});

        walk_points = {point(1, 20, 40), point(2, 30, 40), point(3, 40.125, 40.5), point(4, 60, 40, 2)};
        p->add_points(walk, walk_points);
        auto mp = point(1, 100, 100, 1, "manual");
        mp.cycles = 5;
        mp.label = "Room 1";
        p->add_points(manual, {mp});
        CHECK(changes.back() == pj::Change::Kind::Points);
        // Ascending sequences only, finite positions, known kinds; all or none.
        CHECK(code_of([&] { p->add_points(walk, {point(5, 1, 1), point(5, 2, 2)}); }) == Code::Invalid);
        CHECK(code_of([&] { p->add_points(walk, {point(2, 1, 1)}); }) == Code::Invalid);
        CHECK(code_of([&] { p->add_points(walk, {point(9, std::nan(""), 1)}); }) == Code::Invalid);
        CHECK(code_of([&] { p->add_points(walk, {point(9, 1, 1, 1, "drive")}); }) == Code::Invalid);
        CHECK(code_of([&] { p->add_points(999, {point(9, 1, 1)}); }) == Code::NotFound);
        CHECK(p->points(walk).size() == 4);
        // Sessions: unknown floor, kind, mode, continuation.
        pj::Session bad = walk_in;
        bad.floor_id = "floor-9";
        CHECK(code_of([&] { p->add_session(bad); }) == Code::NotFound);
        bad = walk_in;
        bad.kind = "drive";
        CHECK(code_of([&] { p->add_session(bad); }) == Code::Invalid);
        bad = walk_in;
        bad.walk_mode = "random";
        CHECK(code_of([&] { p->add_session(bad); }) == Code::Invalid);
        bad = walk_in;
        bad.continues = 999;
        CHECK(code_of([&] { p->add_session(bad); }) == Code::NotFound);
        // A walk continued on the first floor after a floor change.
        pj::Session up = walk_in;
        up.floor_id = first;
        up.continues = walk;
        const std::int64_t upstairs = p->add_session(up);
        p->add_points(upstairs, {point(1, 5, 5)});

        pj::RecordingFile r;
        r.file_name = "walk_0000.yrec.zst";
        r.session_id = "3f0c9a1b2d4e5f60";
        r.session_name = "walk";
        r.sha256 = kHash;
        r.size = 81233;
        r.reference = (dir / r.file_name).string();
        r.header = R"({"t":"header"})";
        rec = p->add_recording(r);
        CHECK(code_of([&] { p->add_recording(r); }) == Code::Exists);
        r.file_name = "x.yrec";
        r.sha256 = "zz";
        CHECK(code_of([&] { p->add_recording(r); }) == Code::Invalid);
        r.file_name = "";
        r.sha256 = kHash;
        CHECK(code_of([&] { p->add_recording(r); }) == Code::Invalid);
        CHECK(p->check().empty());
    }
    auto p = pj::Project::open(file);
    CHECK(p->opened_version() == pj::kSchemaVersion);
    const auto s = p->session(walk);
    REQUIRE(s);
    pj::Session expect = walk_in;
    expect.id = walk;
    expect.started_utc = s->started_utc;
    CHECK(!s->started_utc.empty());
    CHECK(*s == expect);
    CHECK(p->sessions(ground).size() == 2);
    CHECK(p->sessions(first).size() == 1);
    CHECK(p->sessions(first)[0].continues == walk);
    CHECK(p->sessions().size() == 3);
    CHECK(p->session(manual)->started_utc == "2026-11-05T10:00:00Z");
    // Plan units unchanged (no rounding), metres from the floor's scale.
    const auto pts = p->points(walk);
    REQUIRE(pts.size() == 4);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        pj::StoredPoint e = walk_points[i];
        e.session_id = walk;
        e.metres = bm::Point2{e.plan.x * 0.05, e.plan.y * 0.05};
        CHECK(pts[i] == e);
    }
    CHECK(p->points(manual)[0].cycles == 5);
    CHECK(p->points(manual)[0].label == "Room 1");
    CHECK_FALSE(p->points(p->sessions(first)[0].id)[0].metres);  // the first floor is not to scale
    CHECK(p->point_count(ground) == 5);
    CHECK(p->point_count(first) == 1);
    // Delete last segment removes its points; ended and notes can change.
    CHECK(p->remove_points(walk, 2) == 1);
    CHECK(p->remove_points(walk, 7) == 0);
    pj::Session done = *s;
    done.ended_utc = "2026-11-05T10:30:00Z";
    done.notes = "done";
    p->update_session(done);
    CHECK(p->session(walk)->ended_utc == "2026-11-05T10:30:00Z");
    done.kind = "manual";
    CHECK(code_of([&] { p->update_session(done); }) == Code::Invalid);
    const auto recs = p->recordings();
    REQUIRE(recs.size() == 1);
    CHECK(recs[0].id == rec);
    CHECK(recs[0].size == 81233);
    CHECK(recs[0].sha256 == kHash);
    CHECK(!recs[0].added_utc.empty());
    p->remove_recording(rec);
    CHECK(p->recordings().empty());
    CHECK(code_of([&] { p->remove_recording(rec); }) == Code::NotFound);
    // Removing a session removes its points; the continuation stays (link cleared).
    p->remove_session(walk);
    CHECK(p->points(walk).empty());
    CHECK_FALSE(p->sessions(first)[0].continues);
    CHECK(p->point_count(ground) == 1);
    CHECK(p->check().empty());
}

TEST_CASE("project 7.5: point metres follow the floor's scale, a trim edit keeps points on the sheet, a floor with walk tests stays (IND-20, IND-07, IND-08)") {
    const fs::path dir = scratch("survey-scale");
    auto p = pj::Project::create(dir / "s.yantra", pj::ProjectInfo{"Survey", "", "", ""});
    const std::string g = p->add_floor(floor_of("Ground floor", 0));
    const std::string f1 = p->add_floor(floor_of("First floor", 1));
    pj::Session s;
    s.floor_id = f1;
    const std::int64_t id = p->add_session(s);
    p->add_points(id, {point(1, 150, 100), point(2, 250, 120)});
    CHECK_FALSE(p->points(id)[0].metres);
    p->set_scale(g, *bm::factor_scale(0.04));  // another floor's scale: no change
    CHECK_FALSE(p->points(id)[0].metres);
    p->set_scale(f1, *bm::factor_scale(0.04));
    REQUIRE(p->points(id)[0].metres);
    CHECK(p->points(id)[0].metres->x == doctest::Approx(6.0));
    CHECK(p->points(id)[1].metres->y == doctest::Approx(4.8));
    p->set_scale(f1, *bm::factor_scale(0.05));
    CHECK(p->points(id)[0].metres->x == doctest::Approx(7.5));
    p->set_scale(f1, bm::Scale{});
    CHECK_FALSE(p->points(id)[0].metres);
    p->set_scale(f1, *bm::factor_scale(0.1));
    // A rectangle trim from (100, 50) to (500, 250) on the page: the
    // points keep their place on the sheet.
    bm::Floor f = *p->floor(f1);
    f.trim.shape = bm::Trim::Shape::Rectangle;
    f.trim.outline = {{100, 50}, {500, 50}, {500, 250}, {100, 250}};
    p->update_floor(f);
    const auto moved = p->points(id);
    CHECK(moved[0].plan.x == doctest::Approx(50));
    CHECK(moved[0].plan.y == doctest::Approx(50));
    CHECK(moved[1].plan.x == doctest::Approx(150));
    CHECK(moved[0].metres->x == doctest::Approx(5.0));
    // Other fields of the floor do not move them.
    f = *p->floor(f1);
    f.name = "Level 1";
    p->update_floor(f);
    CHECK(p->points(id)[0].plan.x == doctest::Approx(50));
    // The measured points would be lost: the floor stays.
    CHECK(code_of([&] { p->remove_floor(f1); }) == Code::InUse);
    bm::Building b = p->building();
    b.floors.erase(b.floors.begin() + 1);
    CHECK(code_of([&] { p->replace_building(b); }) == Code::InUse);
    CHECK(p->floors().size() == 2);
    p->remove_session(id);
    p->remove_floor(f1);
    CHECK(p->floors().size() == 1);
}

TEST_CASE("project 9.2: a floor removed with its sessions takes their points along, in one transaction (IND-04)") {
    const fs::path dir = scratch("remove-with-sessions");
    auto p = pj::Project::create(dir / "p.yantra", pj::ProjectInfo{"Hall", "", "", ""});
    bm::Floor g;
    g.name = "Ground floor";
    const std::string g_id = p->add_floor(g);
    bm::Floor up;
    up.name = "First floor";
    up.level = 1;
    const std::string up_id = p->add_floor(up);
    pj::Session s;
    s.floor_id = g_id;
    const std::int64_t a = p->add_session(s);
    p->add_points(a, {point(1, 10, 10), point(2, 20, 10)});
    const std::int64_t b = p->add_session(s);
    p->add_points(b, {point(1, 30, 10)});
    // A session upstairs continues the walk of the ground floor.
    pj::Session cont;
    cont.floor_id = up_id;
    cont.continues = b;
    const std::int64_t c = p->add_session(cont);
    p->add_points(c, {point(1, 5, 5)});
    std::vector<pj::Change::Kind> seen;
    p->subscribe([&](const pj::Change& ch) { seen.push_back(ch.kind); });

    CHECK(code_of([&] { p->remove_floor(g_id, false); }) == Code::InUse);
    CHECK(p->sessions(g_id).size() == 2);
    p->remove_floor(g_id, true);
    CHECK_FALSE(p->floor(g_id));
    CHECK(p->sessions().size() == 1);
    CHECK_FALSE(p->session(a));
    CHECK(p->points(c).size() == 1);
    CHECK_FALSE(p->session(c)->continues);
    CHECK(p->point_count(up_id) == 1);
    CHECK(seen == std::vector<pj::Change::Kind>{pj::Change::Kind::Sessions, pj::Change::Kind::FloorRemoved});
    CHECK(code_of([&] { p->remove_floor(g_id, true); }) == Code::NotFound);
    // Without sessions both forms do the same.
    p->remove_floor(up_id, true);
    CHECK(p->floors().empty());
    CHECK(p->check().empty());
}

TEST_CASE("project 7.5: schema 1, 2 and 3 files migrate to schema 4 and then store walk tests and points") {
    const fs::path dir = scratch("migrate-4");
    for (int version : {1, 2, 3}) {
        CAPTURE(version);
        const fs::path file = dir / ("v" + std::to_string(version) + ".yantra");
        pj::testing::create_at_version(file, version, pj::ProjectInfo{"Old", "Hall", "", ""});
        raw_sql(file,
                "INSERT INTO floors (id, name, level, height_m, page, plan_width, plan_height, created_utc) VALUES "
                "('floor-1', 'Ground floor', 0, 3.5, 0, 400, 200, '2026-01-01T00:00:00Z');"
                "UPDATE meta SET value = '2' WHERE key = 'next_floor_number';");
        {
            auto p = pj::Project::open(file);
            CHECK(p->opened_version() == version);
            CHECK(p->meta().schema_version == pj::kSchemaVersion);
            CHECK(fs::exists(file.string() + ".schema" + std::to_string(version) + ".bak"));
            CHECK(p->sessions().empty());
            CHECK(p->recordings().empty());
            CHECK(p->point_count("floor-1") == 0);
            pj::Session s;
            s.floor_id = "floor-1";
            s.kind = "manual";
            const std::int64_t id = p->add_session(s);
            p->add_points(id, {point(1, 10, 10, 1, "manual")});
            pj::RecordingFile r;
            r.file_name = "a_0000.yrec";
            r.sha256 = kHash;
            p->add_recording(r);
            CHECK(p->check().empty());
        }
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK(p->point_count("floor-1") == 1);
        CHECK(p->recordings().size() == 1);
    }
}

TEST_CASE("project 7.5: the file refuses session and point rows that break the schema's rules") {
    const fs::path dir = scratch("survey-constraints");
    const fs::path file = dir / "c.yantra";
    {
        auto p = pj::Project::create(file, pj::ProjectInfo{"C", "", "", ""});
        p->add_floor(floor_of("G", 0));
        pj::Session s;
        s.floor_id = "floor-1";
        p->add_session(s);
    }
    CHECK(raw_sql_fails(file, "INSERT INTO sessions (floor, kind, started_utc) VALUES ('floor-9', 'walk', 'x')"));
    CHECK(raw_sql_fails(file, "INSERT INTO sessions (floor, kind, started_utc) VALUES ('floor-1', 'drive', 'x')"));
    CHECK(raw_sql_fails(file, "INSERT INTO points (session, sequence, mono_ns, utc_ns, x, y, kind, sample) "
                              "VALUES (9, 1, 0, 0, 1, 1, 'walk', '{}')"));
    CHECK(raw_sql_fails(file, "INSERT INTO points (session, sequence, mono_ns, utc_ns, x, y, x_m, kind, sample) "
                              "VALUES (1, 1, 0, 0, 1, 1, 0.5, 'walk', '{}')"));
    CHECK(raw_sql_fails(file, "INSERT INTO points (session, sequence, mono_ns, utc_ns, x, y, kind, sample) "
                              "VALUES (1, 0, 0, 0, 1, 1, 'walk', '{}')"));
    CHECK(raw_sql_fails(file, "INSERT INTO recordings (file_name, sha256, size, added_utc) VALUES ('a', 'ab', 1, 'x')"));
    CHECK(raw_sql_fails(file, "DELETE FROM floors WHERE id = 'floor-1'"));
    raw_sql(file, "INSERT INTO points (session, sequence, mono_ns, utc_ns, x, y, kind, sample) "
                  "VALUES (1, 1, 0, 0, 1, 1, 'walk', '{}')");
    auto p = pj::Project::open(file);
    CHECK(p->points(1).size() == 1);
    CHECK(p->check().empty());
}

TEST_CASE("TST-33 plan positions survive the project file round trips bit for bit (save, save as, reopen); one point by its sequence") {
    const fs::path dir = scratch("tst33-positions");
    // Positions no decimal text keeps exactly: thirds, tiny and large
    // offsets, negative (outside the trim) and the smallest normal step.
    const std::vector<bm::Point2> plans = {{1.0 / 3.0, 2.0 / 3.0},          {0.1 + 0.2, 1e-9},
                                           {399.99999999999994, 199.0000001}, {-12.5, 1e6 + 0.123456789012},
                                           {std::nextafter(100.0, 200.0), std::nextafter(50.0, 0.0)}};
    std::string floor;
    std::int64_t session = 0;
    std::vector<pj::StoredPoint> in;
    {
        auto p = pj::Project::create(dir / "a.yantra", pj::ProjectInfo{"Positions", "", "", ""});
        floor = p->add_floor(floor_of("Ground floor", 0));
        p->set_scale(floor, *bm::factor_scale(0.05));
        pj::Session s;
        s.floor_id = floor;
        s.kind = "walk";
        session = p->add_session(s);
        for (std::size_t i = 0; i < plans.size(); ++i) in.push_back(point(static_cast<std::int64_t>(i + 1), plans[i].x, plans[i].y));
        p->add_points(session, in);
        p->save();
        p->save_as(dir / "b.yantra");
    }
    for (const char* name : {"a.yantra", "b.yantra"}) {
        CAPTURE(name);
        auto p = pj::Project::open(dir / name);
        const auto out = p->points(session);
        REQUIRE(out.size() == plans.size());
        const bm::Building b = p->building();
        const double mpu = b.metres_per_unit(*b.find_floor(floor)).value();
        for (std::size_t i = 0; i < plans.size(); ++i) {
            CAPTURE(i);
            CHECK(out[i].plan.x == plans[i].x);  // exact, not approximately
            CHECK(out[i].plan.y == plans[i].y);
            REQUIRE(out[i].metres);
            CHECK(out[i].metres->x == plans[i].x * mpu);
            CHECK(out[i].metres->y == plans[i].y * mpu);
            CHECK(out[i].sample == in[i].sample);
            CHECK(p->point(session, out[i].sequence) == out[i]);
        }
        CHECK_FALSE(p->point(session, 99));
        CHECK_FALSE(p->point(session + 1, 1));
    }
}
