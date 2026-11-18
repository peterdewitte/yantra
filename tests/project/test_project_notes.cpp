// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-48: the notes on the plan in the project file (schema 10,
// docs/formats/PROJECT.md "plan_notes"): add, read, update, move and
// remove with their change notices, refused values, persistence across a
// reopen, a floor's trim moving its notes with its points, a removed floor
// taking its notes, and the migration of older files (the 0.3 fixture,
// schema 7; the 0.4 fixture, schema 8; a schema 9 file of step 15.3).

#include <doctest/doctest.h>
#include <sqlite3.h>

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "yantra/core/project/project.h"
#include "yantra/core/recording/wav.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;

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

bm::Floor floor_of(const std::string& name, int level) {
    bm::Floor f;
    f.name = name;
    f.level = level;
    f.plan_width = 800;
    f.plan_height = 400;
    return f;
}

std::vector<std::uint8_t> wav_of(double seconds) {
    yantra::recording::Audio a;
    a.sample_rate = 8000;
    a.samples.assign(static_cast<std::size_t>(seconds * a.sample_rate), 1000);
    const std::string b = yantra::recording::wav_bytes(a);
    return {b.begin(), b.end()};
}

pj::PlanNote sample(const std::string& floor) {
    pj::PlanNote n;
    n.floor_id = floor;
    n.plan = bm::Point2{120.5, 80.25};
    n.text = "Server room door\nlocked: ask the caretaker";
    n.picture = {0x89, 'P', 'N', 'G', 0, 1, 2, 3};
    n.picture_name = "door.png";
    n.picture_source = "file";
    n.audio = wav_of(1.5);
    n.audio_seconds = 1.5;
    return n;
}

}  // namespace

TEST_CASE("IND-48 plan notes: add, read, update, move and remove round trip and notify") {
    const fs::path dir = scratch("notes-crud");
    const fs::path file = dir / "n.yantra";
    std::string g, f1;
    std::int64_t a = 0, b = 0;
    {
        auto p = pj::Project::create(file, pj::ProjectInfo{"Notes", "", "", ""}, clock_at("2026-11-18T09:00:00Z"));
        g = p->add_floor(floor_of("Ground floor", 0));
        f1 = p->add_floor(floor_of("First floor", 1));
        std::vector<pj::Change> seen;
        p->subscribe([&](const pj::Change& c) {
            if (c.kind == pj::Change::Kind::Notes) seen.push_back(c);
        });
        CHECK(p->notes().empty());
        a = p->add_note(sample(g));
        pj::PlanNote text_only;
        text_only.floor_id = g;
        text_only.plan = {10, 20};
        text_only.text = "Stairs";
        b = p->add_note(text_only);
        pj::PlanNote other;
        other.floor_id = f1;
        other.plan = {1, 2};
        (void)p->add_note(other);  // an empty note is allowed (the window refuses to save one)
        CHECK(b > a);
        REQUIRE(seen.size() == 3);
        CHECK(seen[0].id == g);
        CHECK(seen[2].id == f1);
        const auto ground = p->notes(g);
        REQUIRE(ground.size() == 2);
        CHECK(ground[0].id == a);  // ordered by id: the plan's numbers 1, 2
        CHECK(ground[1].id == b);
        pj::PlanNote expect = sample(g);
        expect.id = a;
        expect.created_utc = expect.modified_utc = "2026-11-18T09:00:00Z";
        CHECK(ground[0] == expect);
        CHECK(p->notes().size() == 3);
        CHECK(p->notes(f1).size() == 1);

        pj::PlanNote changed = *p->note(a);
        changed.text = "Changed";
        changed.picture.clear();
        changed.picture_name.clear();
        changed.picture_source.clear();
        changed.audio.clear();
        changed.audio_seconds = 0;
        p->update_note(changed);
        CHECK(p->note(a)->text == "Changed");
        CHECK_FALSE(p->note(a)->has_picture());
        CHECK_FALSE(p->note(a)->has_audio());
        CHECK(p->note(a)->floor_id == g);
        p->move_note(b, bm::Point2{300, 200});
        CHECK(p->note(b)->plan == bm::Point2{300, 200});
        CHECK(seen.size() == 5);
        p->remove_note(b);
        CHECK_FALSE(p->note(b).has_value());
        CHECK(seen.size() == 6);
        CHECK_THROWS_AS(p->remove_note(b), pj::ProjectError);
        CHECK_THROWS_AS(p->move_note(b, {1, 1}), pj::ProjectError);
        pj::PlanNote back = sample(g);
        back.id = a;
        p->update_note(back);
        p->save();
    }
    // Persistence: the notes come back after a reopen.
    auto p = pj::Project::open(file);
    const auto ground = p->notes(g);
    REQUIRE(ground.size() == 1);
    CHECK(ground[0].id == a);
    CHECK(ground[0].text == sample(g).text);
    CHECK(ground[0].picture == sample(g).picture);
    CHECK(ground[0].audio == sample(g).audio);
    CHECK(ground[0].audio_seconds == doctest::Approx(1.5));
    std::string error;
    const std::string bytes(ground[0].audio.begin(), ground[0].audio.end());
    const auto audio = yantra::recording::parse_wav(bytes, error);
    REQUIRE_MESSAGE(audio, error);
    CHECK(audio->seconds() == doctest::Approx(1.5));
    // A new note after a removed one never gets its id again.
    pj::PlanNote n;
    n.floor_id = g;
    CHECK(p->add_note(n) > b);
    CHECK(p->check().empty());
}

TEST_CASE("IND-48 plan notes: invalid values are refused and nothing changes") {
    const fs::path dir = scratch("notes-invalid");
    auto p = pj::Project::create(dir / "n.yantra", pj::ProjectInfo{"Notes", "", "", ""});
    const std::string g = p->add_floor(floor_of("Ground floor", 0));
    const auto code = [&](pj::PlanNote n) {
        try {
            (void)p->add_note(std::move(n));
        } catch (const pj::ProjectError& e) {
            return e.code();
        }
        return pj::ProjectError::Code::Io;  // not refused
    };
    pj::PlanNote n = sample(g);
    n.floor_id = "floor-99";
    CHECK(code(n) == pj::ProjectError::Code::NotFound);
    n = sample(g);
    n.plan.x = std::numeric_limits<double>::quiet_NaN();
    CHECK(code(n) == pj::ProjectError::Code::Invalid);
    n = sample(g);
    n.picture_source = "web";
    CHECK(code(n) == pj::ProjectError::Code::Invalid);
    n = sample(g);
    n.picture_source.clear();
    CHECK(code(n) == pj::ProjectError::Code::Invalid);
    n = sample(g);
    n.picture.assign(pj::kMaxPhotoBytes + 1, 0);
    CHECK(code(n) == pj::ProjectError::Code::Invalid);
    n = sample(g);
    n.audio.assign(pj::kMaxNoteAudioBytes + 1, 0);
    CHECK(code(n) == pj::ProjectError::Code::Invalid);
    n = sample(g);
    n.audio_seconds = 121;
    CHECK(code(n) == pj::ProjectError::Code::Invalid);
    n = sample(g);
    n.audio.clear();
    CHECK(code(n) == pj::ProjectError::Code::Invalid);  // a length without audio
    CHECK(p->notes().empty());
    const std::int64_t id = p->add_note(sample(g));
    pj::PlanNote bad = *p->note(id);
    bad.audio_seconds = -1;
    CHECK_THROWS_AS(p->update_note(bad), pj::ProjectError);
    CHECK_THROWS_AS(p->move_note(id, {std::numeric_limits<double>::infinity(), 0}), pj::ProjectError);
    CHECK(*p->note(id) == p->notes(g).front());
    CHECK(p->note(id)->audio_seconds == doctest::Approx(1.5));
}

TEST_CASE("IND-48 plan notes follow a new trim of their floor and go with a removed floor") {
    const fs::path dir = scratch("notes-trim");
    auto p = pj::Project::create(dir / "n.yantra", pj::ProjectInfo{"Notes", "", "", ""});
    const std::string g = p->add_floor(floor_of("Ground floor", 0));
    const std::string f1 = p->add_floor(floor_of("First floor", 1));
    pj::PlanNote n;
    n.floor_id = g;
    n.plan = {150, 100};
    const std::int64_t id = p->add_note(n);
    // A rectangle trim from (100, 50): the note keeps its place on the sheet.
    bm::Floor f = *p->floor(g);
    f.trim.shape = bm::Trim::Shape::Rectangle;
    f.trim.outline = {{100, 50}, {500, 50}, {500, 250}, {100, 250}};
    p->update_floor(f);
    CHECK(p->note(id)->plan.x == doctest::Approx(50));
    CHECK(p->note(id)->plan.y == doctest::Approx(50));
    n.floor_id = f1;
    (void)p->add_note(n);
    p->remove_floor(g);  // no sessions: the floor goes, with its notes
    CHECK_FALSE(p->note(id).has_value());
    CHECK(p->notes().size() == 1);
    CHECK(p->check().empty());
}

TEST_CASE("IND-48 IND-46 projects of Yantra 0.3, 0.4 and of schema 9 migrate to schema 10 without notes") {
    struct Case {
        std::string fixture;  // "" = a schema 9 file made here
        int version;
    };
    for (const Case& c : {Case{"fraunces_0_3.yantra", 7}, Case{"fraunces_0_4.yantra", 8}, Case{"", 9}}) {
        CAPTURE(c.version);
        const fs::path dir = scratch("notes-migrate-" + std::to_string(c.version));
        const fs::path file = dir / "old.yantra";
        if (c.fixture.empty())
            pj::testing::create_at_version(file, 9, pj::ProjectInfo{"Schema 9", "", "", ""});
        else
            fs::copy_file(fs::path(YANTRA_TEST_DATA_DIR) / "project" / c.fixture, file);
        CHECK(query_int(file, "PRAGMA user_version") == c.version);
        std::string floor;
        {
            auto p = pj::Project::open(file, clock_at("2026-11-18T08:00:00Z"));
            CHECK(p->opened_version() == c.version);
            CHECK(p->meta().schema_version == pj::kSchemaVersion);
            CHECK(pj::kSchemaVersion == 10);
            CHECK(p->notes().empty());
            if (p->floors().empty()) floor = p->add_floor(floor_of("Ground floor", 0));
            else floor = p->floors().front().id;
            (void)p->add_note(sample(floor));
        }
        CHECK(query_int(file, "PRAGMA user_version") == pj::kSchemaVersion);
        CHECK(query_int(file.string() + ".schema" + std::to_string(c.version) + ".bak", "PRAGMA user_version") == c.version);
        auto p = pj::Project::open(file);
        REQUIRE(p->notes().size() == 1);
        CHECK(p->notes().front().floor_id == floor);
        CHECK(p->check().empty());
    }
}

TEST_CASE("IND-48 the file refuses note rows that break the schema's rules (defensive)") {
    const fs::path dir = scratch("notes-rules");
    const fs::path file = dir / "a.yantra";
    std::string g;
    {
        auto p = pj::Project::create(file, pj::ProjectInfo{"Notes", "", "", ""});
        g = p->add_floor(floor_of("Ground floor", 0));
        p->close();
    }
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr) == SQLITE_OK);
    const std::string ok = "INSERT INTO plan_notes (floor, x, y, created_utc, modified_utc) VALUES ('" + g + "', 1, 2, 't', 't')";
    CHECK(sqlite3_exec(db, ok.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    for (const std::string& bad : std::vector<std::string>{
             std::string("INSERT INTO plan_notes (floor, x, y, created_utc, modified_utc) VALUES ('floor-77', 1, 2, 't', 't')"),
          "UPDATE plan_notes SET picture_source = 'web'", "UPDATE plan_notes SET picture_source = 'file'",
          "UPDATE plan_notes SET audio_seconds = 3", "UPDATE plan_notes SET audio = x'00', audio_seconds = 500"}) {
        CAPTURE(bad);
        CHECK(sqlite3_exec(db, bad.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK);
    }
    sqlite3_close(db);
}
