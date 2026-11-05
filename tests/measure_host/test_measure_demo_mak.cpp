// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The MAK example buildings of the guided tour (DEMO-12, DEMO-13, IND-03;
// docs/DEMO.md, data/demo/mak/): the three descriptions validate (sheet
// URL, SHA-256, size, credit; floors with trims inside the sheet,
// approximate scales, walls, antennas, walks and reference points inside
// the trims; the example ids select the radios' MAK scenarios); a sheet
// that is not the described file is refused with a clear message; with a
// stand-in image (same pixel size, its own hash in a copy of the
// description) and with the real sheet (only when YANTRA_MAK_CACHE names
// a folder holding it: the plans are never in the repository) the
// project builds and references the sheet by SHA-256; the projects
// yantra-demo makes at build time (DEMO_MAK_DIR, no image) open with a
// walk recording and an interpolation per floor, the recordings replay to
// the stored points and statistics, and making them again gives the same
// recordings and project content. Cases that need the build's projects
// skip without the radio tools.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <yantra/core/building/model.h>
#include <yantra/core/demo/demo_project.h>
#include <yantra/core/plan/import.h>
#include <yantra/core/project/project.h>
#include <yantra/core/survey/interpolation.h>
#include <yantra/core/survey/points.h>
#include <yantra/core/survey/stats.h>
#include <yantra/core/survey/viewer.h>

#include "demo_helpers.h"
#include "plan_fixtures.h"

using namespace demo_test;
namespace yb = yantra::building;
namespace yp = yantra::plugin;
namespace yplan = yantra::plan;

namespace {

const std::vector<std::string> kBuildings{"mak_beer_house", "mak_haus_claeson", "mak_villa_bunzl"};

fs::path description_file(const std::string& id) { return fs::path(DEMO_DATA_DIR) / "mak" / (id + ".json"); }

bool built(const std::string& id) {
    if (std::string(DEMO_MAK_DIR).empty() || !fs::exists(fs::path(DEMO_MAK_DIR) / id / (id + ".yantra"))) {
        MESSAGE("no MAK example projects built (no radio tools): skipped");
        return false;
    }
    return true;
}

// A copy of a built example project (project file and recordings), so a
// case never changes the build's files.
fs::path built_copy(const std::string& id, const std::string& name) {
    const fs::path dir = scratch(name + "-" + id);
    fs::copy(fs::path(DEMO_MAK_DIR) / id, dir, fs::copy_options::recursive);
    return dir;
}

nlohmann::json read_json(const fs::path& p) {
    std::ifstream in(p);
    nlohmann::json j = nlohmann::json::parse(in);
    return j;
}

void write_json(const fs::path& p, const nlohmann::json& j) { std::ofstream(p) << j.dump(1) << "\n"; }

// A copy of a description next to its cell database in `dir`, changed by
// `edit`.
fs::path description_copy(const std::string& id, const fs::path& dir,
                          const std::function<void(nlohmann::json&)>& edit) {
    nlohmann::json j = read_json(description_file(id));
    edit(j);
    fs::copy_file(fs::path(DEMO_DATA_DIR) / "mak" / "cells.csv", dir / "cells.csv",
                  fs::copy_options::overwrite_existing);
    const fs::path out = dir / (id + ".json");
    write_json(out, j);
    return out;
}

// A synthetic stand-in for a MAK sheet: a JPEG test pattern of the
// sheet's pixel size (no MAK data), written as the sheet's file name.
fs::path stand_in(const yd::DemoSheet& s, const fs::path& dir, int width = 0, int height = 0) {
    const auto bytes = yantra::plan::fixtures::jpeg(
        yantra::plan::fixtures::pattern(width ? width : s.width, height ? height : s.height), 80);
    const fs::path p = dir / s.file;
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                             static_cast<std::streamsize>(bytes.size()));
    return p;
}

// The checks of a project made from a description, whichever way the
// sheet was found.
void check_made(const yj::Project& p, const yd::DemoDescription& d) {
    REQUIRE(p.check().empty());
    const yb::Building b = p.building();
    CHECK(b.name == d.building_name);
    REQUIRE(d.sheets.size() == 1);
    const yd::DemoSheet& sheet = d.sheets[0];
    const auto sources = p.sources();
    REQUIRE(sources.size() == 1);
    CHECK_FALSE(sources[0].plan.embedded);  // a reference, never a copy (IND-03)
    CHECK(sources[0].plan.sha256 == sheet.sha256);
    CHECK(sources[0].plan.file_name == sheet.file);
    CHECK(sources[0].size == sheet.size_bytes);
    CHECK(sources[0].media_type == sheet.media_type);
    const auto pages = p.source_pages(sources[0].plan.id);
    REQUIRE(pages.size() == 1);
    CHECK(pages[0].width == sheet.width);
    CHECK(pages[0].height == sheet.height);
    REQUIRE(b.floors.size() == d.floors.size());
    for (const yd::DemoFloor& df : d.floors) {
        CAPTURE(df.id);
        const yb::Floor* f = b.find_floor(df.id);
        REQUIRE(f != nullptr);
        CHECK(f->source == sources[0].plan.id);
        CHECK(f->example_id == df.example_id);
        CHECK(f->plan_width == doctest::Approx(df.trim_b.x - df.trim_a.x));
        CHECK(f->plan_height == doctest::Approx(df.trim_b.y - df.trim_a.y));
        CHECK(f->scale.method == yb::Scale::Method::Factor);
        REQUIRE(b.metres_per_unit(*f));
        CHECK(*b.metres_per_unit(*f) == doctest::Approx(df.metres_per_unit));
        CHECK(f->walls == df.walls);
        CHECK(f->antennas == df.antennas);
        // Walls and antennas land inside the trimmed plan, and so on the
        // sheet: plan units 0..plan size.
        for (const yb::Wall& w : f->walls)
            for (const yb::Point2& pt : w.points) {
                CHECK(pt.x >= 0.0);
                CHECK(pt.y >= 0.0);
                CHECK(pt.x <= f->plan_width);
                CHECK(pt.y <= f->plan_height);
            }
        for (const yb::Antenna& a : f->antennas) {
            CHECK(a.position.x >= 0.0);
            CHECK(a.position.y >= 0.0);
            CHECK(a.position.x <= f->plan_width);
            CHECK(a.position.y <= f->plan_height);
        }
        CHECK(df.trim_b.x <= sheet.width);
        CHECK(df.trim_b.y <= sheet.height);
    }
}

}  // namespace

TEST_CASE("DEMO-12 the three MAK example descriptions validate: sheet, credit, floors, approximate scales, walls and antennas inside the trims") {
    std::set<std::string> hashes;
    for (const std::string& id : kBuildings) {
        CAPTURE(id);
        const yd::DemoDescription d = yd::read_description(description_file(id));
        CHECK(d.id == id);
        CHECK(d.label == "DEMO - simulated data");
        REQUIRE(d.sheets.size() == 1);
        const yd::DemoSheet& s = d.sheets[0];
        // DEMO-12: source URL, expected SHA-256, licence and credit; never
        // shipped (licence-restricted).
        CHECK(s.restricted);
        CHECK(s.source_url.rfind("https://mak-media.azureedge.net/", 0) == 0);
        CHECK(s.item_url.rfind("https://sammlung.mak.at/", 0) == 0);
        CHECK(s.sha256.size() == 64);
        hashes.insert(s.sha256);
        CHECK(s.media_type == "image/jpeg");
        CHECK(s.size_bytes > 100'000);
        CHECK(s.width > 1000);
        CHECK(s.height > 1000);
        CHECK(s.licence == "CC BY-NC-ND 4.0");
        CHECK(s.credit.holder == "MAK - Museum of Applied Arts, Vienna");
        CHECK(s.credit.inventory_number.rfind("KI ", 0) == 0);
        CHECK(!s.credit.designer.empty());
        CHECK(d.project.notes.find(s.credit.inventory_number) != std::string::npos);
        // Floors: one per plan of the sheet, ordered levels, approximate
        // scales, DAS antennas on every floor, one walk each.
        CHECK(d.floors.size() >= 3);
        std::set<int> levels;
        std::set<std::string> antennas;
        for (const yd::DemoFloor& f : d.floors) {
            CAPTURE(f.id);
            levels.insert(f.level);
            CHECK(f.sheet == s.id);
            CHECK(f.scale_approximate);
            CHECK(!f.scale_note.empty());
            CHECK(f.metres_per_unit > 0.02);
            CHECK(f.metres_per_unit < 0.04);
            CHECK(f.walls.size() >= 1);
            CHECK(f.antennas.size() >= 1);
            CHECK(f.walk.waypoints.size() >= 6);
            const auto scenario = yd::scenario_for_example(RADIOS_SCENARIO_DIR, f.example_id);
            REQUIRE(scenario);
            CHECK(scenario->stem().string() == id);  // the radios' scenario of the building (RAD-18)
            for (const yb::Antenna& a : f.antennas) {
                CHECK(a.cell == "T1");
                antennas.insert(a.id);
            }
        }
        CHECK(levels.size() == d.floors.size());
        // The scenario sets each DAS antenna's EIRP by id (RAD-61): the
        // description's antennas are the same, with the same EIRP.
        const nlohmann::json sc = read_json(fs::path(RADIOS_SCENARIO_DIR) / (id + ".json"));
        std::set<std::string> in_scenario;
        for (const auto& a : sc.at("antennas")) in_scenario.insert(a.at("id").get<std::string>());
        CHECK(antennas == in_scenario);
        for (const yd::DemoFloor& f : d.floors)
            for (const yb::Antenna& a : f.antennas)
                for (const auto& o : sc.at("antennas"))
                    if (o.at("id") == a.id) {
                        REQUIRE(a.effective_eirp_dbm());
                        CHECK(*a.effective_eirp_dbm() == doctest::Approx(o.at("eirp_dbm").get<double>()));
                    }
    }
    CHECK(hashes.size() == kBuildings.size());
}

TEST_CASE("DEMO-12 a description with a wall, antenna or walk outside a trim, or a trim outside the sheet, is refused") {
    const fs::path dir = scratch("mak-bad");
    const auto refused = [&](const std::function<void(nlohmann::json&)>& edit, const std::string& want) {
        const fs::path f = description_copy("mak_villa_bunzl", dir, edit);
        CAPTURE(want);
        bool thrown = false;
        try {
            yd::read_description(f);
        } catch (const yd::DemoError& e) {
            thrown = true;
            MESSAGE(e.what());
            CHECK(std::string(e.what()).find(want) != std::string::npos);
        }
        CHECK(thrown);
    };
    refused([](nlohmann::json& j) { j["floors"][0]["walls"][0]["points"][0] = {-5, 10}; }, "wall w1");
    refused([](nlohmann::json& j) { j["floors"][1]["antennas"][0]["position"] = {9000, 10}; }, "antenna das-2");
    refused([](nlohmann::json& j) { j["floors"][2]["walk"]["waypoints"][1] = {10, 5000}; }, "walk waypoint");
    refused([](nlohmann::json& j) { j["floors"][0]["trim"] = {{85, 170}, {2000, 600}}; }, "not inside sheet");
    refused([](nlohmann::json& j) { j["sheets"][0].erase("credit"); }, "credit");
    refused([](nlohmann::json& j) { j["floors"][0].erase("scale"); }, "scale_bar or scale");
}

TEST_CASE("DEMO-12 a sheet that is not the described file is refused with a clear message; a missing one names the download") {
    const fs::path dir = scratch("mak-wrong");
    for (const std::string& id : kBuildings) {
        CAPTURE(id);
        const yd::DemoDescription d = yd::read_description(description_file(id));
        const yd::DemoSheet& s = d.sheets[0];
        // Missing: refused unless the build's reference-only mode.
        yd::CreateOptions o;
        o.sheet_folder = dir / "empty";
        fs::create_directories(o.sheet_folder);
        try {
            yd::create_project(d, dir / "missing.yantra", o);
            FAIL("a missing sheet was accepted");
        } catch (const yd::DemoError& e) {
            const std::string m = e.what();
            MESSAGE(m);
            CHECK(m.find("is missing") != std::string::npos);
            CHECK(m.find(s.source_url) != std::string::npos);
        }
        CHECK_FALSE(fs::exists(dir / "missing.yantra"));
        // A stand-in of the same pixel size is not the described file.
        o.sheet_folder = dir;
        const fs::path image = stand_in(s, dir);
        try {
            yd::create_project(d, dir / "wrong.yantra", o);
            FAIL("a sheet with another SHA-256 was accepted");
        } catch (const yd::DemoError& e) {
            const std::string m = e.what();
            MESSAGE(m);
            CHECK(m.find("is not the described file") != std::string::npos);
            CHECK(m.find(file_sha256(image)) != std::string::npos);
            CHECK(m.find(s.sha256) != std::string::npos);
            CHECK(m.find(s.source_url) != std::string::npos);
        }
        CHECK_FALSE(fs::exists(dir / "wrong.yantra"));
        CHECK_THROWS_AS(yd::check_sheet_file(s, dir), yd::DemoError);
        fs::remove(image);
    }
    fs::remove_all(dir);  // no stand-in named like a MAK sheet stays behind
}

TEST_CASE("DEMO-12 with a stand-in sheet the example project builds and references it by SHA-256 (IND-03)") {
    for (const std::string& id : kBuildings) {
        CAPTURE(id);
        const fs::path dir = scratch("mak-stand-in-" + id);
        const yd::DemoDescription real = yd::read_description(description_file(id));
        const fs::path image = stand_in(real.sheets[0], dir);
        const std::string hash = file_sha256(image);
        const auto size = fs::file_size(image);
        // The description with the stand-in's hash and size (a copy).
        const fs::path desc = description_copy(id, dir, [&](nlohmann::json& j) {
            j["sheets"][0]["sha256"] = hash;
            j["sheets"][0]["size_bytes"] = size;
        });
        const yd::DemoDescription d = yd::read_description(desc);
        yd::CreateOptions o;
        o.sheet_folder = dir;
        std::shared_ptr<yj::Project> p = yd::create_project(d, dir / (id + ".yantra"), o);
        check_made(*p, d);
        const yj::SourceFile src = p->sources().front();
        CHECK(src.reference == fs::absolute(image).lexically_normal().string());
        // The project holds no copy: its content is read from the file
        // and checked against the hash, from any folder.
        CHECK(p->read_source(src.plan.id).size() == size);
        CHECK(p->read_source_from(src.plan.id, image).size() == size);
        // The plan import opens the referenced sheet; each floor's trim is
        // on its page.
        auto imp = yplan::PlanImport::open(*p, src.plan.id);
        CHECK(imp->page_count() == 1);
        p->close();
        CHECK(fs::file_size(dir / (id + ".yantra")) < size);
        // A stand-in of another pixel size with the described hash is
        // refused too.
        const fs::path other = dir / "other";
        fs::create_directories(other);
        const fs::path small = stand_in(d.sheets[0], other, 640, 480);
        const fs::path desc2 = description_copy(id, other, [&](nlohmann::json& j) {
            j["sheets"][0]["sha256"] = file_sha256(small);
            j["sheets"][0]["size_bytes"] = fs::file_size(small);
        });
        o.sheet_folder = other;
        try {
            yd::create_project(yd::read_description(desc2), other / "small.yantra", o);
            FAIL("a sheet of another pixel size was accepted");
        } catch (const yd::DemoError& e) {
            MESSAGE(e.what());
            CHECK(std::string(e.what()).find("pixels") != std::string::npos);
        }
        fs::remove_all(dir);  // no stand-in named like a MAK sheet stays behind
    }
}

TEST_CASE("DEMO-12 with the real MAK sheets (YANTRA_MAK_CACHE) the example projects build with the hash checked") {
    const char* cache = std::getenv("YANTRA_MAK_CACHE");
    if (!cache || !*cache) {
        MESSAGE("YANTRA_MAK_CACHE is not set (the MAK plans are never in the repository; set it to a folder "
                "holding the downloaded sheets as named in data/demo/mak/*.json): skipped");
        return;
    }
    for (const std::string& id : kBuildings) {
        CAPTURE(id);
        const yd::DemoDescription d = yd::read_description(description_file(id));
        if (!fs::exists(fs::path(cache) / d.sheets[0].file)) {
            MESSAGE(d.sheets[0].file << " is not in " << cache << ": skipped");
            continue;
        }
        const fs::path dir = scratch("mak-real-" + id);
        yd::CreateOptions o;
        o.sheet_folder = cache;
        REQUIRE(yd::check_sheet_file(d.sheets[0], cache));
        std::shared_ptr<yj::Project> p = yd::create_project(d, dir / (id + ".yantra"), o);
        check_made(*p, d);
        const yj::SourceFile src = p->sources().front();
        CHECK(p->read_source(src.plan.id).size() == d.sheets[0].size_bytes);
        // Every floor's trim renders from the real sheet.
        auto imp = yplan::PlanImport::open(*p, src.plan.id);
        CHECK(imp->page_count() == 1);
        // The same project as the reference-only one of the build (but the
        // path hint).
        o.require_restricted_sheets = false;
        o.sheet_folder = dir / "none";
        std::shared_ptr<yj::Project> q = yd::create_project(d, dir / "reference.yantra", o);
        CHECK(p->building() == q->building());
        auto sa = p->sources(), sb = q->sources();
        sa[0].reference.clear(), sb[0].reference.clear(), sa[0].added_utc.clear(), sb[0].added_utc.clear();
        CHECK(sa == sb);
        CHECK(p->source_pages(sa[0].plan.id) == q->source_pages(sb[0].plan.id));
        p->close();
        CHECK(fs::file_size(dir / (id + ".yantra")) < d.sheets[0].size_bytes);  // no copy inside
    }
}

TEST_CASE("DEMO-13 the MAK example projects hold a walk recording and an interpolation per floor, and no image") {
    for (const std::string& id : kBuildings) {
        CAPTURE(id);
        if (!built(id)) return;
        const yd::DemoDescription d = yd::read_description(description_file(id));
        const fs::path dir = built_copy(id, "open");
        auto p = yj::Project::open(dir / (id + ".yantra"));
        check_made(*p, d);
        // The sheet is not there: the project only references it.
        CHECK_THROWS_AS(p->read_source(p->sources().front().plan.id), yj::ProjectError);
        CHECK(p->meta().info.notes == d.project.notes);  // with the credit and "approximate"
        const auto recordings = p->recordings();
        REQUIRE(recordings.size() == d.floors.size());
        for (const yd::DemoFloor& df : d.floors) {
            CAPTURE(df.id);
            const auto sessions = p->sessions(df.id);
            REQUIRE(sessions.size() == 1);
            CHECK(sessions[0].walk_mode == "continuous");
            CHECK(sessions[0].name == df.walk.name);
            CHECK(sessions[0].recording_session == yd::walk_session_id(d, df));
            const ys::PointList list = points_of(*p, d, df.id);
            CHECK(list.to_scale);
            CHECK(list.count() >= 15);  // walks of 20-50 m at 1 m/s, one point per second
            for (const ys::SurveyPoint& pt : list.points) {
                REQUIRE(pt.value.has_value());
                CHECK(pt.cell == "la=100 mcc=901 mnc=1");  // T1
                CHECK(*pt.value > -110.0);
                CHECK(*pt.value < -10.0);
            }
            const auto rec = std::find_if(recordings.begin(), recordings.end(), [&](const yj::RecordingFile& r) {
                return r.session_id == sessions[0].recording_session;
            });
            REQUIRE(rec != recordings.end());
            CHECK(rec->reference == "recordings/" + rec->file_name);
            CHECK(rec->sha256 == file_sha256(dir / rec->reference));
            // A recording holds records, never image data (DEMO-13).
            const std::string bytes = file_bytes(dir / rec->reference);
            CHECK(bytes.find("\xFF\xD8\xFF") == std::string::npos);
            CHECK(bytes.find(d.label) != std::string::npos);
            const auto stored = ys::stored_interpolations(*p, df.id);
            REQUIRE(stored.size() == 1);
            CHECK(stored[0].quality.points == list.count());
            CHECK(stored[0].quality.rms_db < 3.0);
            CHECK(!stored[0].estimated_sources);
            CHECK(!ys::staleness(*p, stored[0]).stale);
        }
    }
}

TEST_CASE("DEMO-13 the MAK walk recordings replay to the stored points and statistics") {
    for (const std::string& id : kBuildings) {
        CAPTURE(id);
        if (!built(id)) return;
        const yd::DemoDescription d = yd::read_description(description_file(id));
        const fs::path dir = built_copy(id, "replay");
        auto demo = yj::Project::open(dir / (id + ".yantra"));
        yd::CreateOptions o;
        o.require_restricted_sheets = false;
        o.sheet_folder = dir / "no-sheets";
        std::shared_ptr<yj::Project> fresh = yd::create_project(d, dir / "fresh.yantra", o);
        yp::PluginLoader loader{yp::PluginLoaderConfig{}};
        ys::ImportOptions io;
        io.replay.catalog = tetra_catalog(loader);
        std::vector<fs::path> files;
        for (const auto& r : demo->recordings()) files.push_back(dir / r.reference);
        const auto imported = ys::import_recordings(fresh, files, io);
        REQUIRE(imported.size() == d.floors.size());
        for (const yd::DemoFloor& f : d.floors) {
            CAPTURE(f.id);
            const ys::PointList want = points_of(*demo, d, f.id);
            const ys::PointList got = points_of(*fresh, d, f.id);
            REQUIRE(got.count() == want.count());
            for (std::size_t i = 0; i < got.count(); ++i) {
                CHECK(got.points[i].time_ns == want.points[i].time_ns);
                CHECK(got.points[i].plan.x == want.points[i].plan.x);
                CHECK(got.points[i].plan.y == want.points[i].plan.y);
                CHECK(got.points[i].value == want.points[i].value);
            }
            ys::StatsOptions so;
            so.thresholds = {-95.0, -85.0};
            CHECK(ys::floor_stats(got, so) == ys::floor_stats(want, so));
        }
    }
}

TEST_CASE("DEMO-13 making the MAK example projects again gives the same recordings and project content") {
    for (const std::string& id : kBuildings) {
        CAPTURE(id);
        if (!built(id)) return;
        const yd::DemoDescription d = yd::read_description(description_file(id));
        const fs::path dir = scratch("mak-again-" + id);
        REQUIRE(run_yantra_demo(description_file(id), dir, "--reference-sheets"));
        for (const auto& e : fs::directory_iterator(fs::path(DEMO_MAK_DIR) / id / "recordings")) {
            CAPTURE(e.path().filename().string());
            CHECK(file_sha256(e.path()) == file_sha256(dir / "recordings" / e.path().filename()));
        }
        const fs::path b = built_copy(id, "again-built");
        auto x = yj::Project::open(b / (id + ".yantra"));
        auto y = yj::Project::open(dir / (id + ".yantra"));
        check_same_content(*x, *y, d);
    }
}
