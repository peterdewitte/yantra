// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Indoor project file (IND-01, IND-03, IND-04, NFR-05; format in
// docs/formats/PROJECT.md): create/open round trip, floors and their
// order, the building model round trip, source copies and references with
// hashes, save-as, schema migration and newer files, damaged and cut
// files, the lock against a second instance, crash safety (writers killed
// mid-way, injected faults) and a large project.
// Copied from yantra-v2 8c7a3e1: tests/project/test_project.cpp, adapted
// for the building model's floors and sources (sessions, samples and
// provenance belong to later steps).

#include "yantra/core/building/model_json.h"
#include "yantra/core/project/project.h"
#include "yantra/core/util/sha256.h"

#include <doctest/doctest.h>
#include <sqlite3.h>

#include <algorithm>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using Code = pj::ProjectError::Code;

namespace {

// An empty scratch folder per test case.
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

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string as_string(const std::vector<std::uint8_t>& v) { return std::string(v.begin(), v.end()); }

// Small made-up plan files: a PNG signature or a PDF header and bytes.
std::string fake_png(int seed, std::size_t size = 3000) {
    std::string s = "\x89PNG\r\n\x1a\n";
    for (std::size_t i = 0; i < size; ++i) s.push_back(static_cast<char>((i * 31 + static_cast<std::size_t>(seed)) & 0xff));
    return s;
}

std::string fake_pdf(int seed) {
    std::string s = "%PDF-1.7\n";
    for (int i = 0; i < 2000; ++i) s.push_back(static_cast<char>('a' + (i * 7 + seed) % 26));
    return s;
}

pj::ProjectOptions fixed_clock() {
    pj::ProjectOptions o;
    o.clock = [] { return std::string("2026-11-05T12:00:00Z"); };
    o.tool = "Yantra test";
    return o;
}

pj::ProjectInfo info() { return pj::ProjectInfo{"Town hall survey", "Town hall", "Example network", "DAS check"}; }

Code code_of(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const pj::ProjectError& e) {
        return e.code();
    }
    FAIL("no ProjectError");
    return Code::Invalid;
}

std::string message_of(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const pj::ProjectError& e) {
        return e.what();
    }
    return "";
}

// Runs SQL on a closed project file directly.
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

bool no_side_files(const fs::path& dir) {
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string n = e.path().filename().string();
        if (n.find(".tmp-") != std::string::npos || n.find("-wal") != std::string::npos ||
            n.find("-journal") != std::string::npos || n.find("-shm") != std::string::npos)
            return false;
    }
    return true;
}

bm::Floor floor_named(const std::string& name, int level, double height = 3.0) {
    bm::Floor f;
    f.name = name;
    f.level = level;
    f.height_m = height;
    return f;
}

std::vector<std::string> names(const std::vector<bm::Floor>& floors) {
    std::vector<std::string> out;
    for (const bm::Floor& f : floors) out.push_back(f.name);
    return out;
}

std::vector<int> levels(const std::vector<bm::Floor>& floors) {
    std::vector<int> out;
    for (const bm::Floor& f : floors) out.push_back(f.level);
    return out;
}

}  // namespace

TEST_CASE("project: create, reopen, metadata round trip (IND-01)") {
    const fs::path dir = scratch("roundtrip");
    const fs::path file = dir / "townhall.yantra";
    {
        auto p = pj::Project::create(file, info(), fixed_clock());
        CHECK(p->path() == file);
        CHECK(p->is_open());
        const pj::ProjectMeta m = p->meta();
        CHECK(m.info == info());
        CHECK(m.created_utc == "2026-11-05T12:00:00Z");
        CHECK(m.created_with == "Yantra test");
        CHECK(m.schema_version == pj::kSchemaVersion);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK(p->migration_backup().empty());
        // The building is named after the site; floors come later.
        const bm::Building b = p->building();
        CHECK(b.name == "Town hall");
        CHECK(b.floors.empty());
        CHECK(b.wall_classes == bm::default_wall_classes());
        pj::ProjectInfo changed = info();
        changed.notes = "second visit";
        p->set_info(changed);
        CHECK(p->meta().info.notes == "second visit");
        CHECK(code_of([&] { p->set_info(pj::ProjectInfo{}); }) == Code::Invalid);
        p->set_building_name("Town hall, east wing");
        p->set_floor_attenuation_db(18.5);
        CHECK(code_of([&] { p->set_floor_attenuation_db(-1.0); }) == Code::Invalid);
        p->close();
        CHECK_FALSE(p->is_open());
        CHECK(code_of([&] { (void)p->meta(); }) == Code::Closed);
    }
    // A closed project is one file.
    CHECK(no_side_files(dir));
    auto p = pj::Project::open(file);
    CHECK(p->meta().info.notes == "second visit");
    CHECK(p->meta().info.operator_name == "Example network");
    CHECK(p->building().name == "Town hall, east wing");
    CHECK(p->building().floor_attenuation_db == 18.5);

    // Without a site the building gets the project's name.
    auto q = pj::Project::create(dir / "nosite.yantra", pj::ProjectInfo{"Depot", "", "", ""});
    CHECK(q->building().name == "Depot");
    // create() never replaces a file and needs a name.
    CHECK(code_of([&] { pj::Project::create(file, info()); }) == Code::Exists);
    CHECK(code_of([&] { pj::Project::create(dir / "x.yantra", pj::ProjectInfo{}); }) == Code::Invalid);
    CHECK_FALSE(fs::exists(dir / "x.yantra"));
}

TEST_CASE("project: floors ordered by level, renamed, levels and heights, reordered, removed (IND-04)") {
    const fs::path dir = scratch("floors");
    const fs::path file = dir / "f.yantra";
    auto p = pj::Project::create(file, info());
    std::vector<pj::Change> changes;
    const int token = p->subscribe([&](const pj::Change& c) { changes.push_back(c); });

    const std::string g = p->add_floor(floor_named("Ground floor", 0, 3.5));
    const std::string b = p->add_floor(floor_named("Basement", -1));
    const std::string f1 = p->add_floor(floor_named("First floor", 1));
    CHECK(g == "floor-1");
    CHECK(b == "floor-2");
    CHECK(f1 == "floor-3");
    CHECK(names(p->floors()) == std::vector<std::string>{"Basement", "Ground floor", "First floor"});
    CHECK(p->floor(g)->height_m == 3.5);
    CHECK_FALSE(p->floor("floor-9").has_value());
    REQUIRE(changes.size() == 3);
    CHECK(changes[0].kind == pj::Change::Kind::FloorAdded);
    CHECK(changes[0].id == g);

    // A given id is kept; a used id, a used level, an empty name, a bad
    // height and an unknown source are refused and change nothing.
    bm::Floor roof = floor_named("Roof", 5);
    roof.id = "roof";
    CHECK(p->add_floor(roof) == "roof");
    CHECK(code_of([&] { p->add_floor(roof); }) == Code::Invalid);
    CHECK(message_of([&] { p->add_floor(floor_named("Mezzanine", 0)); }).find("Ground floor") != std::string::npos);
    CHECK(code_of([&] { p->add_floor(floor_named("", 7)); }) == Code::Invalid);
    CHECK(code_of([&] { p->add_floor(floor_named("Low", 8, 0.0)); }) == Code::Invalid);
    CHECK(code_of([&] { p->add_floor(floor_named("Odd", 9, -2.0)); }) == Code::Invalid);
    CHECK(code_of([&] { p->add_floor(floor_named("Far", 5000)); }) == Code::Invalid);
    bm::Floor with_source = floor_named("Annex", 10);
    with_source.source = "source-7";
    CHECK(code_of([&] { p->add_floor(with_source); }) == Code::NotFound);
    CHECK(p->floors().size() == 4);

    // Rename, level, height.
    p->rename_floor(g, "Ground floor (hall)");
    CHECK(code_of([&] { p->rename_floor(g, ""); }) == Code::Invalid);
    CHECK(code_of([&] { p->rename_floor("nope", "x"); }) == Code::NotFound);
    p->set_floor_level("roof", 2, 2.4);
    CHECK(p->floor("roof")->level == 2);
    CHECK(p->floor("roof")->height_m == 2.4);
    CHECK(code_of([&] { p->set_floor_level("roof", -1); }) == Code::Invalid);  // the basement's
    p->set_floor_height(b, 2.8);
    CHECK(code_of([&] { p->set_floor_height(b, 0.0); }) == Code::Invalid);
    CHECK(p->floor(b)->height_m == 2.8);

    // Reorder: the floors keep the set of levels {-1, 0, 1, 2}.
    changes.clear();
    p->move_floor("roof", 0);
    CHECK(names(p->floors()) == std::vector<std::string>{"Roof", "Basement", "Ground floor (hall)", "First floor"});
    CHECK(levels(p->floors()) == std::vector<int>{-1, 0, 1, 2});
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].kind == pj::Change::Kind::FloorsReordered);
    p->move_floor("roof", 99);  // clamped to the end
    CHECK(names(p->floors()) == std::vector<std::string>{"Basement", "Ground floor (hall)", "First floor", "Roof"});
    CHECK(code_of([&] { p->move_floor("nope", 0); }) == Code::NotFound);

    // Remove; ids are never reused.
    p->remove_floor(f1);
    CHECK(code_of([&] { p->remove_floor(f1); }) == Code::NotFound);
    CHECK(p->add_floor(floor_named("New first floor", 7)) == "floor-4");
    p->unsubscribe(token);

    // Everything survives a reopen, ordered by level.
    const std::vector<bm::Floor> before = p->floors();
    p->close();
    auto q = pj::Project::open(file);
    CHECK(q->floors() == before);
    CHECK(names(q->floors()) == std::vector<std::string>{"Basement", "Ground floor (hall)", "Roof", "New first floor"});
    CHECK(q->add_floor(floor_named("Top", 8)) == "floor-5");
}

TEST_CASE("project: data of later schema versions is refused, never dropped") {
    const fs::path dir = scratch("unsupported");
    auto p = pj::Project::create(dir / "u.yantra", info());
    bm::Floor trimmed = floor_named("Ground floor", 0);
    trimmed.trim.shape = bm::Trim::Shape::Rectangle;
    trimmed.trim.outline = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    CHECK(code_of([&] { p->add_floor(trimmed); }) == Code::Unsupported);
    bm::Floor turned = floor_named("Ground floor", 0);
    turned.trim.quarter_turns = 1;
    CHECK(code_of([&] { p->add_floor(turned); }) == Code::Unsupported);
    bm::Floor scaled = floor_named("Ground floor", 0);
    scaled.scale.method = bm::Scale::Method::Factor;
    scaled.scale.metres_per_unit = 0.05;
    CHECK(code_of([&] { p->add_floor(scaled); }) == Code::Unsupported);
    bm::Floor aligned = floor_named("Ground floor", 0);
    aligned.reference_point = bm::Point2{1, 2};
    CHECK(code_of([&] { p->add_floor(aligned); }) == Code::Unsupported);
    bm::Floor walled = floor_named("Ground floor", 0);
    walled.walls.push_back(bm::Wall{"w1", "brick", {{0, 0}, {1, 0}}, std::nullopt});
    CHECK(code_of([&] { p->add_floor(walled); }) == Code::Unsupported);
    bm::Floor with_antenna = floor_named("Ground floor", 0);
    with_antenna.antennas.emplace_back();
    with_antenna.antennas.back().id = "a1";
    CHECK(code_of([&] { p->add_floor(with_antenna); }) == Code::Unsupported);
    CHECK(p->floors().empty());

    bm::Building b = p->building();
    b.cells.push_back(bm::Cell{"c1", "Cell", "", "", 0, {}});
    CHECK(code_of([&] { p->replace_building(b); }) == Code::Unsupported);
    b = p->building();
    b.wall_classes.pop_back();
    CHECK(code_of([&] { p->replace_building(b); }) == Code::Unsupported);
    const std::string id = p->add_floor(floor_named("Ground floor", 0));
    bm::Floor f = *p->floor(id);
    f.walls.push_back(bm::Wall{"w1", "brick", {{0, 0}, {1, 0}}, std::nullopt});
    CHECK(code_of([&] { p->update_floor(f); }) == Code::Unsupported);
    CHECK(p->floor(id)->walls.empty());
}

TEST_CASE("project: the building model round trip (IND-04, TST-33)") {
    const fs::path dir = scratch("building");
    write_file(dir / "plans.pdf", fake_pdf(1));
    write_file(dir / "annex.png", fake_png(2));
    const fs::path file = dir / "b.yantra";
    bm::Building expected;
    {
        auto p = pj::Project::create(file, info());
        const std::string pdf = p->add_source(dir / "plans.pdf").id;
        const std::string png = p->add_source(dir / "annex.png").id;
        CHECK(pdf == "source-1");
        CHECK(png == "source-2");
        bm::Floor g = floor_named("Ground floor", 0, 3.6);
        g.source = pdf;
        g.page = 0;
        g.plan_width = 1191.0;
        g.plan_height = 842.25;
        g.slab_attenuation_db = 21.5;
        bm::Floor f1 = floor_named("First floor", 1);
        f1.source = pdf;  // a second floor of the same sheet's document
        f1.page = 2;
        f1.example_id = "example-1";
        f1.plan_width = 1191.0;
        f1.plan_height = 842.25;
        bm::Floor a = floor_named("Annex", -1, 2.5);
        a.source = png;
        a.plan_width = 3008;
        a.plan_height = 0.1 + 0.2;  // a value without a short decimal form
        p->add_floor(g);
        p->add_floor(f1);
        p->add_floor(a);
        p->add_floor(floor_named("Roof (no plan)", 2));
        p->set_floor_attenuation_db(12.0);
        expected = p->building();
    }
    REQUIRE(expected.validate().empty());
    CHECK(expected.name == "Town hall");
    CHECK(expected.floor_attenuation_db == 12.0);
    REQUIRE(expected.sources.size() == 2);
    CHECK(expected.sources[0].file_name == "plans.pdf");
    CHECK(expected.sources[0].sha256 == yantra::util::Sha256::of(fake_pdf(1)));
    CHECK(expected.sources[0].embedded);
    REQUIRE(expected.floors.size() == 4);
    CHECK(expected.floors[0].name == "Annex");
    CHECK(expected.floors[0].plan_height == 0.1 + 0.2);
    CHECK(expected.floors[1].slab_attenuation_db == 21.5);
    CHECK(expected.floors[2].example_id == "example-1");
    CHECK(expected.floors[3].source.empty());

    // Reopen: the same model, also through its JSON form.
    auto p = pj::Project::open(file);
    CHECK(p->building() == expected);
    bm::Building parsed;
    std::string error;
    REQUIRE(bm::parse_building(bm::building_to_json(p->building()), parsed, error));
    CHECK(parsed == expected);

    // replace_building into another project holding the same sources.
    const fs::path other = dir / "other.yantra";
    {
        auto q = pj::Project::create(other, pj::ProjectInfo{"Copy", "", "", ""});
        q->add_floor(floor_named("Old floor", 0));
        q->add_source(dir / "plans.pdf");
        q->add_source(dir / "annex.png");
        q->replace_building(parsed);
        CHECK(q->building() == expected);
        CHECK(q->floors().size() == 4);  // "Old floor" went: floor-1 is now the Ground floor
        // A floor added later gets a fresh id.
        CHECK(q->add_floor(floor_named("Top", 3)) == "floor-5");
        // A source the project does not hold, or with another hash, is refused.
        bm::Building wrong = parsed;
        wrong.sources[1].sha256 = std::string(64, 'f');
        CHECK(code_of([&] { q->replace_building(wrong); }) == Code::Invalid);
        wrong = parsed;
        wrong.sources.push_back(bm::PlanSource{"source-9", "x.png", "", true});
        CHECK(code_of([&] { q->replace_building(wrong); }) == Code::NotFound);
        wrong = parsed;
        wrong.floors[1].level = wrong.floors[0].level;
        CHECK(code_of([&] { q->replace_building(wrong); }) == Code::Invalid);
        CHECK(q->floors().size() == 5);  // unchanged by the refused calls
    }
    auto q = pj::Project::open(other);
    bm::Building back = q->building();
    back.floors.pop_back();  // "Top"
    back.name = expected.name;
    CHECK(back == expected);
}

TEST_CASE("project: source files are copied in with their hash, deduplicated, never changed (IND-03)") {
    const fs::path dir = scratch("sources");
    const std::string png = fake_png(1);
    const std::string pdf = fake_pdf(2);
    write_file(dir / "ground.png", png);
    write_file(dir / "plans.pdf", pdf);
    write_file(dir / "same.png", png);
    write_file(dir / "plan.TGA", std::string(100, '\0'));
    const fs::path file = dir / "s.yantra";
    {
        auto p = pj::Project::create(file, info());
        const pj::AddSourceResult a = p->add_source(dir / "ground.png");
        CHECK(a.added);
        const pj::AddSourceResult b = p->add_source(dir / "plans.pdf");
        const pj::AddSourceResult again = p->add_source(dir / "same.png");
        CHECK_FALSE(again.added);
        CHECK(again.id == a.id);
        const pj::AddSourceResult tga = p->add_source(dir / "plan.TGA");
        const auto s = p->source(a.id);
        REQUIRE(s.has_value());
        CHECK(s->plan.file_name == "ground.png");
        CHECK(s->plan.embedded);
        CHECK(s->media_type == "image/png");
        CHECK(s->size == png.size());
        CHECK(s->plan.sha256 == yantra::util::Sha256::of(png));
        CHECK(p->source(b.id)->media_type == "application/pdf");
        CHECK(p->source(tga.id)->media_type == "image/x-tga");
        CHECK(as_string(p->read_source(b.id)) == pdf);
        CHECK(p->sources().size() == 3);
        // A floor uses a source; it cannot be removed while it does.
        bm::Floor f = floor_named("Ground floor", 0);
        f.source = a.id;
        const std::string fid = p->add_floor(f);
        CHECK(code_of([&] { p->remove_source(a.id); }) == Code::InUse);
        p->remove_source(tga.id);
        CHECK(code_of([&] { p->remove_source(tga.id); }) == Code::NotFound);
        CHECK(code_of([&] { (void)p->read_source(tga.id); }) == Code::NotFound);
        CHECK(code_of([&] { p->add_source(dir / "missing.png"); }) == Code::Io);
        CHECK(code_of([&] { p->add_source(dir); }) == Code::Io);
        p->export_source(b.id, dir / "exported.pdf");
        CHECK(read_file(dir / "exported.pdf") == pdf);
        (void)fid;
    }
    // The originals are untouched.
    CHECK(read_file(dir / "ground.png") == png);
    CHECK(read_file(dir / "plans.pdf") == pdf);
    // Moved to another computer (another folder, originals gone): the
    // copies are still there.
    const fs::path moved = scratch("sources-moved") / "s.yantra";
    fs::copy_file(file, moved);
    fs::remove_all(dir);
    auto p = pj::Project::open(moved);
    CHECK(as_string(p->read_source("source-1")) == png);
    CHECK(as_string(p->read_source("source-2")) == pdf);
    CHECK(p->check().empty());
}

TEST_CASE("project: licence-restricted sources are referenced by hash (IND-03, DEMO-12)") {
    const fs::path dir = scratch("references");
    const std::string pdf = fake_pdf(3) + std::string(200000, 'x');
    const fs::path cached = dir / "cache" / "restricted.pdf";
    fs::create_directories(cached.parent_path());
    write_file(cached, pdf);
    const fs::path file = dir / "r.yantra";
    auto p = pj::Project::create(file, info());
    const pj::AddSourceResult r = p->add_source_reference(cached);
    CHECK(r.added);
    CHECK_FALSE(p->add_source_reference(cached).added);
    const auto s = p->source(r.id);
    REQUIRE(s.has_value());
    CHECK_FALSE(s->plan.embedded);
    CHECK(s->plan.sha256 == yantra::util::Sha256::of(pdf));
    CHECK(s->reference == fs::absolute(cached).lexically_normal().string());
    CHECK(as_string(p->read_source(r.id)) == pdf);
    p->save();
    CHECK(fs::file_size(file) < 100000);  // the content is not in the project
    CHECK(p->building().sources.at(0).embedded == false);

    // The cache moved: read it from its new place, checked by hash.
    const fs::path elsewhere = dir / "other-cache.pdf";
    fs::rename(cached, elsewhere);
    CHECK(code_of([&] { (void)p->read_source(r.id); }) == Code::Io);
    CHECK(as_string(p->read_source_from(r.id, elsewhere)) == pdf);
    CHECK(p->check().empty());  // a missing cache is not a damaged project
    // Another file with the same name is not the source.
    write_file(cached, std::string(pdf.size(), 'y'));
    CHECK(code_of([&] { (void)p->read_source(r.id); }) == Code::Damaged);
    write_file(dir / "short.pdf", "short");
    CHECK(code_of([&] { (void)p->read_source_from(r.id, dir / "short.pdf"); }) == Code::Damaged);
}

TEST_CASE("project: save_as writes a copy and continues with it") {
    const fs::path dir = scratch("saveas");
    const fs::path a = dir / "a.yantra";
    const fs::path b = dir / "b.yantra";
    write_file(dir / "plan.png", fake_png(5));
    auto p = pj::Project::create(a, info());
    p->add_source(dir / "plan.png");
    p->add_floor(floor_named("Ground floor", 0));
    int saved = 0;
    p->subscribe([&](const pj::Change& c) { saved += c.kind == pj::Change::Kind::Saved ? 1 : 0; });
    p->save_as(b);
    CHECK(saved == 1);
    CHECK(p->path() == b);
    p->add_floor(floor_named("First floor", 1));
    CHECK(code_of([&] { p->save_as(b); }) == Code::Invalid);  // itself
    CHECK(code_of([&] { p->save_as(a); }) == Code::Exists);
    p->close();
    CHECK(no_side_files(dir));
    {
        auto old = pj::Project::open(a);
        CHECK(old->floors().size() == 1);
    }
    auto q = pj::Project::open(b);
    CHECK(q->floors().size() == 2);
    CHECK(as_string(q->read_source("source-1")) == fake_png(5));

    // Replacing an existing file (the UI asked), but never one that is open.
    q->save_as(a, true);
    CHECK(q->path() == a);
    {
        auto again = pj::Project::open(b);
        CHECK(code_of([&] { q->save_as(b, true); }) == Code::Locked);
    }
    q->save_as(b, true);
    q.reset();
    auto r = pj::Project::open(a);
    CHECK(r->floors().size() == 2);
    CHECK(r->check().empty());
}

TEST_CASE("project: injected faults during create, save_as and migration leave whole files") {
    const fs::path dir = scratch("faults");
    const fs::path file = dir / "p.yantra";
    struct Crash : std::runtime_error {
        Crash() : std::runtime_error("crash") {}
    };
    auto fail_at = [](std::string point) {
        pj::ProjectOptions o;
        o.fault = [point](std::string_view at) {
            if (at == point) throw Crash();
        };
        return o;
    };
    // create: the target name never appears.
    CHECK_THROWS_AS(pj::Project::create(file, info(), fail_at("create:written")), Crash);
    CHECK_FALSE(fs::exists(file));
    CHECK(no_side_files(dir));

    auto p = pj::Project::create(file, info());
    p->add_floor(floor_named("Ground floor", 0));
    p.reset();
    // save_as to a new name: nothing at the target.
    auto failing = pj::Project::open(file, fail_at("save_as:written"));
    CHECK_THROWS_AS(failing->save_as(dir / "copy.yantra"), Crash);
    CHECK_FALSE(fs::exists(dir / "copy.yantra"));
    // ... and over an existing file: it keeps its old content.
    {
        auto other = pj::Project::create(dir / "existing.yantra", pj::ProjectInfo{"Existing", "", "", ""});
    }
    const std::string before = read_file(dir / "existing.yantra");
    CHECK_THROWS_AS(failing->save_as(dir / "existing.yantra", true), Crash);
    CHECK(read_file(dir / "existing.yantra") == before);
    // The project itself is still open and usable.
    CHECK(failing->path() == file);
    failing->add_floor(floor_named("First floor", 1));
    failing.reset();
    CHECK(no_side_files(dir));

    // Migration: a failing step leaves the file at its old version.
    pj::ProjectOptions next = fail_at("migrate:step");
    next.testing_extra_steps = {"CREATE TABLE future_data (x INTEGER)"};
    CHECK_THROWS_AS(pj::Project::open(file, next), Crash);
    {
        auto q = pj::Project::open(file);
        CHECK(q->opened_version() == pj::kSchemaVersion);
        CHECK(q->floors().size() == 2);
    }
}

TEST_CASE("project: an older schema version is migrated on open, with a backup") {
    const fs::path dir = scratch("migrate");
    const fs::path file = dir / "old.yantra";
    pj::testing::create_at_version(file, pj::kSchemaVersion, pj::ProjectInfo{"Old", "Site", "", ""});
    {
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK(p->meta().info.name == "Old");
        p->add_floor(floor_named("Ground floor", 0));
    }
    // A later Yantra with one more schema step opens this file: it is
    // migrated in one transaction after a backup copy.
    pj::ProjectOptions later;
    later.testing_extra_steps = {"CREATE TABLE future_data (x INTEGER); INSERT INTO future_data VALUES (42)"};
    {
        auto p = pj::Project::open(file, later);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        CHECK(p->meta().schema_version == pj::kSchemaVersion + 1);
        CHECK(p->migration_backup() == fs::path(file.string() + ".schema1.bak"));
        CHECK(p->floors().size() == 1);
    }
    // The backup is the old file, which this (older) build still opens.
    {
        auto backup = pj::Project::open(file.string() + ".schema1.bak");
        CHECK(backup->meta().schema_version == pj::kSchemaVersion);
        CHECK(backup->floors().size() == 1);
    }
    // The migrated file is now too new for this build, and is not touched.
    const std::string bytes = read_file(file);
    const std::string msg = message_of([&] { pj::Project::open(file); });
    CHECK(msg.find("newer Yantra") != std::string::npos);
    CHECK(code_of([&] { pj::Project::open(file); }) == Code::TooNew);
    CHECK(read_file(file) == bytes);
    // Without a backup when asked so.
    const fs::path other = dir / "nobackup.yantra";
    pj::testing::create_at_version(other, pj::kSchemaVersion, pj::ProjectInfo{"Old", "", "", ""});
    later.backup_before_migration = false;
    {
        auto p = pj::Project::open(other, later);
        CHECK(p->migration_backup().empty());
    }
    CHECK_FALSE(fs::exists(other.string() + ".schema1.bak"));
    CHECK(code_of([] { pj::testing::create_at_version("unused.yantra", pj::kSchemaVersion + 1, info()); }) ==
          Code::Invalid);
}

TEST_CASE("project: newer, foreign, damaged and cut files are refused with a reason") {
    const fs::path dir = scratch("damaged");
    const fs::path good = dir / "good.yantra";
    write_file(dir / "plan.png", fake_png(9, 300000));
    {
        auto p = pj::Project::create(good, info());
        p->add_source(dir / "plan.png");
        for (int i = 0; i < 20; ++i) p->add_floor(floor_named("Floor " + std::to_string(i), i));
    }
    const std::string bytes = read_file(good);

    // A newer schema.
    const fs::path newer = dir / "newer.yantra";
    fs::copy_file(good, newer);
    raw_sql(newer, "PRAGMA user_version = " + std::to_string(pj::kSchemaVersion + 5));
    const std::string newer_bytes = read_file(newer);
    CHECK(code_of([&] { pj::Project::open(newer); }) == Code::TooNew);
    CHECK(read_file(newer) == newer_bytes);  // not touched

    // Not a project: a text file, an empty file, another SQLite database,
    // a project without its format mark, a missing file.
    write_file(dir / "text.yantra", "this is not a project file, just some text that is long enough");
    CHECK(code_of([&] { pj::Project::open(dir / "text.yantra"); }) == Code::NotAProject);
    write_file(dir / "empty.yantra", "");
    CHECK(code_of([&] { pj::Project::open(dir / "empty.yantra"); }) == Code::NotAProject);
    raw_sql(dir / "foreign.yantra", "CREATE TABLE t (x); PRAGMA user_version = 1");
    CHECK(code_of([&] { pj::Project::open(dir / "foreign.yantra"); }) == Code::NotAProject);
    fs::copy_file(good, dir / "nomark.yantra");
    raw_sql(dir / "nomark.yantra", "DELETE FROM meta WHERE key = 'format'");
    CHECK(code_of([&] { pj::Project::open(dir / "nomark.yantra"); }) == Code::NotAProject);
    CHECK(code_of([&] { pj::Project::open(dir / "missing.yantra"); }) == Code::Io);
    CHECK(code_of([&] { pj::Project::open(dir); }) == Code::Io);

    // Cut files (a copy that stopped half way): refused, never a crash.
    for (const std::size_t keep : {std::size_t{50}, std::size_t{100}, std::size_t{4096}, bytes.size() / 2,
                                   bytes.size() - 4096}) {
        CAPTURE(keep);
        const fs::path cut = dir / ("cut-" + std::to_string(keep) + ".yantra");
        write_file(cut, bytes.substr(0, keep));
        const Code c = code_of([&] { pj::Project::open(cut); });
        CHECK((c == Code::Damaged || c == Code::NotAProject));
    }
    // Damaged pages: overwritten b-tree pages are found by the check at
    // open; overwritten blob bytes by the source hash.
    {
        std::string broken = bytes;
        for (std::size_t i = 4096 + 8; i < 4096 + 400; ++i) broken[i] = static_cast<char>(0xA5);
        write_file(dir / "pages.yantra", broken);
        const Code c = code_of([&] {
            auto p = pj::Project::open(dir / "pages.yantra");
            if (!p->check().empty()) throw pj::ProjectError(Code::Damaged, "check");
        });
        CHECK((c == Code::Damaged || c == Code::NotAProject));
    }
    {
        const std::string png = fake_png(9, 300000);
        std::string broken = bytes;
        const std::size_t at = broken.find(png.substr(100000, 64));
        REQUIRE(at != std::string::npos);
        broken[at + 10] = static_cast<char>(broken[at + 10] ^ 0x01);
        write_file(dir / "blob.yantra", broken);
        auto p = pj::Project::open(dir / "blob.yantra");
        CHECK(code_of([&] { (void)p->read_source("source-1"); }) == Code::Damaged);
        const auto problems = p->check();
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].find("SHA-256") != std::string::npos);
    }
    // The good file still opens.
    auto p = pj::Project::open(good);
    CHECK(p->check().empty());
}

TEST_CASE("project: one instance at a time; the lock goes with the process") {
    const fs::path dir = scratch("lock");
    const fs::path file = dir / "l.yantra";
    {
        auto p = pj::Project::create(file, info());
        // A second open in the same process is refused with a reason.
        CHECK(code_of([&] { pj::Project::open(file); }) == Code::Locked);
        CHECK(message_of([&] { pj::Project::open(file); }).find("open in another Yantra") != std::string::npos);
        p->close();
        auto again = pj::Project::open(file);  // released by close
    }
    // Another process holds it, then dies without closing.
    int ready[2];
    REQUIRE(::pipe(ready) == 0);
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ::close(ready[0]);
        try {
            auto p = pj::Project::open(file);
            if (::write(ready[1], "r", 1) != 1) ::_exit(2);
            ::pause();
        } catch (...) {
            ::_exit(1);
        }
        ::_exit(0);
    }
    ::close(ready[1]);
    char c = 0;
    REQUIRE(::read(ready[0], &c, 1) == 1);
    CHECK(code_of([&] { pj::Project::open(file); }) == Code::Locked);
    ::kill(child, SIGKILL);
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    ::close(ready[0]);
    auto p = pj::Project::open(file);
    CHECK(p->check().empty());
}

TEST_CASE("project: a writer killed in the middle leaves a project with whole changes") {
    const fs::path dir = scratch("crash");
    const fs::path file = dir / "c.yantra";
    { auto p = pj::Project::create(file, info()); }
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        // Adds floors, renames them and moves them around without end,
        // reporting each added floor once it is committed, until killed.
        ::close(fds[0]);
        try {
            auto p = pj::Project::open(file);
            for (int n = 0;; ++n) {
                const std::string id = p->add_floor(floor_named("Floor " + std::to_string(n), n % 2001 - 1000));
                p->rename_floor(id, "Floor " + std::to_string(n) + " (renamed)");
                p->move_floor(id, 0);
                if (::write(fds[1], "c", 1) != 1) ::_exit(2);
            }
        } catch (...) {
            ::_exit(1);
        }
    }
    ::close(fds[1]);
    int commits = 0;
    char c = 0;
    while (commits < 40 && ::read(fds[0], &c, 1) == 1) ++commits;
    REQUIRE(commits == 40);
    ::kill(child, SIGKILL);
    while (::read(fds[0], &c, 1) == 1) ++commits;  // reported before it died
    ::close(fds[0]);
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status));

    auto p = pj::Project::open(file);
    CHECK(p->check().empty());
    const std::vector<bm::Floor> floors = p->floors();
    // Nothing committed is lost.
    CHECK(floors.size() >= static_cast<std::size_t>(commits));
    const std::vector<int> ls = levels(floors);
    CHECK(std::set<int>(ls.begin(), ls.end()).size() == floors.size());
    CHECK(p->building().validate().empty());
    p.reset();
    CHECK(no_side_files(dir));
}

TEST_CASE("project: a save_as killed in the middle never leaves half a file under the target name") {
    const fs::path dir = scratch("crash-saveas");
    const fs::path file = dir / "c.yantra";
    write_file(dir / "plan.png", fake_png(3, 4'000'000));
    {
        auto p = pj::Project::create(file, info());
        p->add_source(dir / "plan.png");
        p->add_floor(floor_named("Ground floor", 0));
    }
    int fds[2];
    REQUIRE(::pipe(fds) == 0);
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ::close(fds[0]);
        try {
            auto p = pj::Project::open(file);
            for (int n = 0;; ++n) {
                p->save_as(dir / ("copy-" + std::to_string(n) + ".yantra"));
                if (::write(fds[1], "c", 1) != 1) ::_exit(2);
            }
        } catch (...) {
            ::_exit(1);
        }
    }
    ::close(fds[1]);
    int copies = 0;
    char c = 0;
    while (copies < 3 && ::read(fds[0], &c, 1) == 1) ++copies;
    REQUIRE(copies == 3);
    ::kill(child, SIGKILL);
    ::close(fds[0]);
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    // Every file under a project name opens whole; temporary files may be
    // left behind by the crash (never under the target name).
    int opened = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string n = e.path().filename().string();
        if (e.path().extension() != ".yantra" || n.find(".tmp-") != std::string::npos) continue;
        CAPTURE(n);
        auto p = pj::Project::open(e.path());
        CHECK(p->check().empty());
        CHECK(p->floors().size() == 1);
        CHECK(as_string(p->read_source("source-1")) == fake_png(3, 4'000'000));
        ++opened;
    }
    CHECK(opened >= 4);  // the original and the reported copies
}

TEST_CASE("project: calls from several threads are serialised") {
    const fs::path dir = scratch("threads");
    auto p = pj::Project::create(dir / "t.yantra", info());
    std::vector<std::thread> threads;
    constexpr int kThreads = 4;
    constexpr int kEach = 25;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kEach; ++i) {
                const int level = t * kEach + i;
                const std::string id = p->add_floor(floor_named("F" + std::to_string(level), level));
                p->rename_floor(id, "Floor " + std::to_string(level));
                (void)p->building();
            }
        });
    }
    for (std::thread& t : threads) t.join();
    const std::vector<bm::Floor> floors = p->floors();
    REQUIRE(floors.size() == kThreads * kEach);
    std::set<std::string> ids;
    for (std::size_t i = 0; i < floors.size(); ++i) {
        CHECK(floors[i].level == static_cast<int>(i));
        CHECK(floors[i].name == "Floor " + std::to_string(i));
        ids.insert(floors[i].id);
    }
    CHECK(ids.size() == floors.size());
}

TEST_CASE("project: a large project (many floors, large sources) round trips") {
    // The spec gives no size or time limit for project files; this checks
    // that a large building and multi-megabyte sources are stored whole.
    const fs::path dir = scratch("large");
    const fs::path file = dir / "large.yantra";
    constexpr int kFloors = 300;
    const std::string big = fake_png(4, 24'000'000);
    write_file(dir / "big.png", big);
    {
        auto p = pj::Project::create(file, info());
        const std::string src = p->add_source(dir / "big.png").id;
        for (int i = 0; i < kFloors; ++i) {
            bm::Floor f = floor_named("Floor " + std::to_string(i), i - 100);
            f.source = src;
            f.page = i;
            f.plan_width = 10000 + i;
            f.plan_height = 7000.5;
            p->add_floor(f);
        }
        p->move_floor("floor-300", 0);
    }
    auto p = pj::Project::open(file);
    const bm::Building b = p->building();
    CHECK(b.validate().empty());
    REQUIRE(b.floors.size() == kFloors);
    CHECK(b.floors.front().id == "floor-300");
    CHECK(b.floors.back().page == kFloors - 2);
    CHECK(as_string(p->read_source("source-1")) == big);
    CHECK(p->check().empty());
    CHECK(fs::file_size(file) < big.size() + 2'000'000);  // one copy only
}

TEST_CASE("project: docs/formats/PROJECT.md lists every table and column of the schema (NFR-05, DOC-12)") {
    const fs::path dir = scratch("doc");
    const fs::path file = dir / "d.yantra";
    { auto p = pj::Project::create(file, info()); }
    // Columns of the real schema.
    std::map<std::string, std::set<std::string>> schema;
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(file.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    REQUIRE(sqlite3_prepare_v2(db,
                               "SELECT m.name, p.name FROM sqlite_schema m, pragma_table_info(m.name) p "
                               "WHERE m.type = 'table'",
                               -1, &st, nullptr) == SQLITE_OK);
    while (sqlite3_step(st) == SQLITE_ROW) {
        schema[reinterpret_cast<const char*>(sqlite3_column_text(st, 0))].insert(
            reinterpret_cast<const char*>(sqlite3_column_text(st, 1)));
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    // Columns of the document: the first cell of each table row under a
    // "### `table`" heading of the version-1 tables.
    std::map<std::string, std::set<std::string>> doc;
    std::ifstream in(fs::path(YANTRA_SOURCE_DIR) / "docs" / "formats" / "PROJECT.md");
    REQUIRE(in);
    std::string line;
    std::string table;
    while (std::getline(in, line)) {
        if (line.rfind("### `", 0) == 0) {
            table = line.substr(5, line.find('`', 5) - 5);
            doc[table];
        } else if (line.rfind("## ", 0) == 0) {
            table.clear();
        } else if (!table.empty() && line.rfind("| `", 0) == 0) {
            const std::string cell = line.substr(2, line.find(" |", 2) - 2);
            for (std::size_t at = cell.find('`'); at != std::string::npos; at = cell.find('`', cell.find('`', at + 1) + 1)) {
                const std::size_t end = cell.find('`', at + 1);
                doc[table].insert(cell.substr(at + 1, end - at - 1));
            }
        }
    }
    std::string diff;
    for (const auto& [t, cols] : schema)
        for (const std::string& c : cols)
            if (!doc[t].count(c)) diff += " missing " + t + "." + c;
    for (const auto& [t, cols] : doc)
        for (const std::string& c : cols)
            if (!schema[t].count(c)) diff += " extra " + t + "." + c;
    CHECK_MESSAGE(diff.empty(), diff);
    // The document names the current version.
    CHECK(read_file(fs::path(YANTRA_SOURCE_DIR) / "docs" / "formats" / "PROJECT.md")
              .find("schema version, now **" + std::to_string(pj::kSchemaVersion) + "**") != std::string::npos);
}
