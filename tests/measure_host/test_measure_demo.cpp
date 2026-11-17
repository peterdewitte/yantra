// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Fraunces demo project (DEMO-05, DEMO-06, DEMO-09, DEMO-13;
// docs/DEMO.md): the project yantra-demo makes at build time
// (DEMO_FRAUNCES_DIR) opens with its floors, trims, scales, walls, DAS
// antennas, cell database, walk recordings and stored interpolations;
// the recordings replay to the stored points and statistics; making it
// again gives the same recordings and project content; the simulator
// picks the Fraunces scenario by the floors' example ids (RAD-18) and,
// with the floors' fixed demo scenes (DEMO-05), its levels at the walk
// points agree with the project's model (TST-36, 0.5 dB); the demo scenarios of data/demo/scenarios.json can be selected
// (DEMO-09). The cases skip without the radio tools.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include <yantra/core/building/model.h>
#include <yantra/core/context/building_context.h>
#include <yantra/core/demo/demo_project.h>
#include <yantra/core/project/project.h>
#include <yantra/core/radio/scene_export.h>
#include <yantra/core/raytrace/model.h>
#include <yantra/core/survey/interpolation.h>
#include <yantra/core/survey/points.h>
#include <yantra/core/survey/stats.h>
#include <yantra/core/survey/viewer.h>
#include <yantra/core/util/sha256.h>

#include "demo_helpers.h"
#include "e2e_helpers.h"
#include "measure_rig.h"

using namespace mh;
using namespace demo_test;
namespace yb = yantra::building;
namespace yd = yantra::demo;
namespace yj = yantra::project;
namespace ys = yantra::survey;
namespace rt = yantra::raytrace;

namespace {

const fs::path kDescription = fs::path(DEMO_DATA_DIR) / "fraunces" / "fraunces.json";

bool demo_built() {
    if (std::string(DEMO_FRAUNCES_DIR).empty() || !fs::exists(fs::path(DEMO_FRAUNCES_DIR) / "fraunces.yantra")) {
        MESSAGE("no demo project built (no radio tools): skipped");
        return false;
    }
    return true;
}

// A copy of the built demo project (project file and recordings), so a
// case never changes the build's files.
fs::path demo_copy(const std::string& name) {
    const fs::path dir = scratch(name);
    fs::copy(DEMO_FRAUNCES_DIR, dir, fs::copy_options::recursive);
    return dir;
}

}  // namespace

TEST_CASE("DEMO-05 the Fraunces demo project opens with 2 floors, trims, scales, walls, DAS antennas, cells and walks") {
    if (!demo_built()) return;
    const yd::DemoDescription d = yd::read_description(kDescription);
    const fs::path dir = demo_copy("open");
    auto p = yj::Project::open(dir / "fraunces.yantra");
    REQUIRE(p->check().empty());
    const yb::Building b = p->building();
    CHECK(b.name == "Fraunces Tavern");
    REQUIRE(b.floors.size() == 2);
    CHECK(d.floors.size() == 2);
    // Two HABS sheets, copied in unchanged.
    const auto sources = p->sources();
    REQUIRE(sources.size() == 2);
    std::set<std::string> hashes;
    for (const auto& s : sources) hashes.insert(s.plan.sha256);
    for (const yd::DemoSheet& s : d.sheets) CHECK(hashes.count(s.sha256) == 1);
    // The cells of the cell database: the DAS cell and the macro cell.
    REQUIRE(p->cell_database().entries().size() == 2);
    REQUIRE(b.find_cell("T1") != nullptr);
    CHECK(b.find_cell("T1")->carrier_hz == 390'000'000);
    REQUIRE(b.find_cell("M1") != nullptr);
    const auto recordings = p->recordings();
    REQUIRE(recordings.size() == 2);
    for (const yd::DemoFloor& df : d.floors) {
        CAPTURE(df.id);
        const yb::Floor* f = b.find_floor(df.id);
        REQUIRE(f != nullptr);
        CHECK(f->example_id == df.example_id);
        CHECK(f->example_id.rfind("fraunces-", 0) == 0);
        CHECK(f->trim.outline.size() == 4);
        CHECK(f->plan_width > 1000.0);
        CHECK(f->plan_height > 1000.0);
        // The sheet's 10-foot scale bar: about 3 mm per pixel.
        const auto mpu = b.metres_per_unit(*f);
        REQUIRE(mpu);
        CHECK(*mpu == doctest::Approx(df.scale_length_m / std::hypot(df.scale_b.x - df.scale_a.x,
                                                                     df.scale_b.y - df.scale_a.y)));
        CHECK(*mpu > 0.002);
        CHECK(*mpu < 0.004);
        CHECK(f->reference_point.has_value());
        CHECK(f->walls.size() >= 20);
        CHECK(f->walls == df.walls);
        REQUIRE(f->antennas.size() >= 2);
        for (const yb::Antenna& a : f->antennas) {
            CHECK(a.type == yb::AntennaType::IndoorDas);
            CHECK(a.cell == "T1");
            CHECK(a.feed_power_dbm.has_value());
        }
        // One continuous walk, recorded with its fixed session id.
        const auto sessions = p->sessions(df.id);
        REQUIRE(sessions.size() == 1);
        CHECK(sessions[0].walk_mode == "continuous");
        CHECK(sessions[0].name == df.walk.name);
        CHECK(sessions[0].recording_session == yd::walk_session_id(d, df));
        CHECK(sessions[0].started_utc.rfind(d.start_utc.substr(0, 16), 0) == 0);
        const ys::PointList list = points_of(*p, d, df.id);
        CHECK(list.to_scale);
        CHECK(list.unit == "dBm");
        CHECK(list.count() >= 60);  // a walk of about 75 m at 1 m/s, one point per second
        for (const ys::SurveyPoint& pt : list.points) {
            REQUIRE(pt.value.has_value());
            CHECK(pt.cell == "la=100 mcc=901 mnc=1");  // T1
            CHECK(*pt.value > -110.0);
            CHECK(*pt.value < -20.0);
        }
        const auto rec = std::find_if(recordings.begin(), recordings.end(),
                                      [&](const yj::RecordingFile& r) { return r.session_id == sessions[0].recording_session; });
        REQUIRE(rec != recordings.end());
        CHECK(rec->reference == "recordings/" + rec->file_name);  // no folder of the computer that made it
        CHECK(rec->sha256 == file_sha256(dir / rec->reference));
        // The stored interpolation of the floor (DEMO-05).
        const auto stored = ys::stored_interpolations(*p, df.id);
        REQUIRE(stored.size() == 1);
        CHECK(stored[0].quality.points == list.count());
        CHECK(!ys::staleness(*p, stored[0]).stale);
    }
}

TEST_CASE("DEMO-06 the demo walk recordings replay to the stored points and statistics (IND-20, IND-30)") {
    if (!demo_built()) return;
    const yd::DemoDescription d = yd::read_description(kDescription);
    const fs::path dir = demo_copy("replay");
    auto demo = yj::Project::open(dir / "fraunces.yantra");
    // The same building in a new project, the recordings imported into it.
    std::shared_ptr<yj::Project> fresh = yd::create_project(d, dir / "fresh.yantra");
    yp::PluginLoader loader{yp::PluginLoaderConfig{}};
    ys::ImportOptions o;
    o.replay.catalog = tetra_catalog(loader);
    std::vector<fs::path> files;
    for (const auto& r : demo->recordings()) files.push_back(dir / r.reference);
    const auto imported = ys::import_recordings(fresh, files, o);
    REQUIRE(imported.size() == 2);
    for (const yd::DemoFloor& f : d.floors) {
        CAPTURE(f.id);
        const ys::PointList want = points_of(*demo, d, f.id);
        const ys::PointList got = points_of(*fresh, d, f.id);
        REQUIRE(got.count() == want.count());
        for (std::size_t i = 0; i < got.count(); ++i) {
            CHECK(got.points[i].time_ns == want.points[i].time_ns);
            CHECK(got.points[i].utc_ns == want.points[i].utc_ns);
            CHECK(got.points[i].plan.x == want.points[i].plan.x);
            CHECK(got.points[i].plan.y == want.points[i].plan.y);
            CHECK(got.points[i].value == want.points[i].value);
            CHECK(got.points[i].group == want.points[i].group);
        }
        ys::StatsOptions so;
        so.thresholds = {-95.0, -85.0};
        CHECK(ys::floor_stats(got, so) == ys::floor_stats(want, so));
    }
}

TEST_CASE("DEMO-05 each floor's stored interpolation is the ray-tracing interpolation of its walk (IND-41..45)") {
    if (!demo_built()) return;
    const yd::DemoDescription d = yd::read_description(kDescription);
    const fs::path dir = demo_copy("interpolation");
    auto p = yj::Project::open(dir / "fraunces.yantra");
    for (const yd::DemoFloor& f : d.floors) {
        CAPTURE(f.id);
        const auto stored = ys::stored_interpolations(*p, f.id);
        REQUIRE(stored.size() == 1);
        const ys::Interpolation& s = stored[0];
        CHECK(s.request == yd::interpolation_request(*p, d, f));
        CHECK(!s.estimated_sources);  // the floor's DAS antennas serve T1
        CHECK(s.floor_cells > 0);
        CHECK(s.quality.rms_db < 3.0);
        const auto again = ys::interpolate(*p, s.request, demo_run());
        REQUIRE(again);
        CHECK(again->quality.points == s.quality.points);
        CHECK(again->quality.rms_db == doctest::Approx(s.quality.rms_db).epsilon(1e-6));
        REQUIRE(again->value_dbm.size() == s.value_dbm.size());
        double worst = 0.0;
        for (std::size_t i = 0; i < s.value_dbm.size(); ++i)
            if (std::isfinite(s.value_dbm[i])) worst = std::max(worst, std::fabs(double(again->value_dbm[i]) - s.value_dbm[i]));
        CHECK(worst <= 1e-3);
        // A readout over a measured point is that point's value (IND-43).
        const ys::PointList list = points_of(*p, d, f.id);
        const ys::Readout r = ys::readout(s, *list.points[list.count() / 2].metres);
        CHECK(r.on_map);
        CHECK(r.measured);
    }
}

TEST_CASE("DEMO-06 making the demo project again gives the same recordings and project content") {
    if (!demo_built()) return;
    const yd::DemoDescription d = yd::read_description(kDescription);
    const fs::path dir = scratch("again");
    REQUIRE(run_yantra_demo(kDescription, dir));
    // The reference: the build's project, or a second run (kDemoMadeHere).
    fs::path reference = DEMO_FRAUNCES_DIR;
    if (!kDemoMadeHere) {
        reference = scratch("again-reference");
        REQUIRE(run_yantra_demo(kDescription, reference));
    }
    for (const auto& e : fs::directory_iterator(reference / "recordings")) {
        CAPTURE(e.path().filename().string());
        CHECK(file_sha256(e.path()) == file_sha256(dir / "recordings" / e.path().filename()));
    }
    const fs::path built = kDemoMadeHere ? demo_copy("again-built") : reference;
    auto a = yj::Project::open(built / "fraunces.yantra");
    auto b = yj::Project::open(dir / "fraunces.yantra");
    check_same_content(*a, *b, d);
}

TEST_CASE("RAD-18 the simulator picks the Fraunces scenario by the floors' example ids; its levels at the walk points are the project's model (TST-36, DEMO-05)") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    const yd::DemoDescription d = yd::read_description(kDescription);
    // The project's building, made from the description (no yantra-gen needed).
    const fs::path dir = scratch("simulator");
    std::shared_ptr<yj::Project> p = yd::create_project(d, dir / "fraunces.yantra");
    const yb::Building b = p->building();
    rt::ModelParams params = ys::model_params(p->propagation());
    params.max_reflections = 0;  // the multi-wall model has none (the Fraunces scenario)

    Rig rig{true};
    TetraPlugin tetra{rig};
    // DEMO-05: the floors' fixed demo scenes by example id, as in the app.
    yc::BuildingContext context(rig.publisher, yc::demo_scenes({fs::path(DEMO_DATA_DIR) / "scenes"}));
    context.set_building(b);
    for (const yd::DemoFloor& f : d.floors) {
        // Served byte for byte as committed, and the scene the exporter
        // makes from the description (so the levels are those of before).
        const auto snap = rig.publisher->snapshot();
        REQUIRE(snap->scenes.count(f.id));
        CHECK(snap->scenes.at(f.id) == *yd::read_demo_scene(fs::path(DEMO_DATA_DIR) / "scenes", f.example_id));
        CHECK(nlohmann::json::parse(snap->scenes.at(f.id)) == nlohmann::json::parse(yr::export_scene(b, f.id).json));
    }
    context.show_floor(d.floors[0].id);
    context.set_cursor(d.floors[0].walk.waypoints[0]);
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(!en.devices.empty());
    rig.state->set_device_setting(e.id, en.devices.front().key, "record_rate_hz", 10.0);
    rig.state->set_device_setting(e.id, en.devices.front().key, "gain_db", 0.0);
    sim_levels_dbfs(*rig.state, e.id, en.devices.front().key);  // calibrated below (-20 dB)
    std::string error;
    auto dev = rig.host->open(e.id, en.devices.front().key, error);
    REQUIRE_MESSAGE(dev, error);
    yr::Plan plan;
    plan.type = YR_PLAN_CARRIERS;
    plan.carriers_hz = {390'000'000, 390'200'000};
    REQUIRE(dev->start(plan) == YR_OK);
    auto table = std::make_shared<ycal::Table>();
    table->put(flat_entry(*dev->calibration_key(), -20.0, true));  // the true offset at gain 0 (RAD-61)
    rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table, ycal::AntennaSettings{}));
    for (const yd::DemoFloor& f : d.floors) {
        CAPTURE(f.id);
        context.show_floor(f.id);
        const yb::Floor* bf = b.find_floor(f.id);
        // Every third waypoint of the walk.
        std::vector<yb::Point2> units, metres;
        for (std::size_t i = 0; i < f.walk.waypoints.size(); i += 3) {
            units.push_back(f.walk.waypoints[i]);
            metres.push_back(*b.to_plan_metres(*bf, f.walk.waypoints[i]));
        }
        const rt::PathSet set = rt::trace_points(b, f.id, metres, params);
        const rt::PointLevels want = rt::evaluate(b, set, params);
        for (std::size_t i = 0; i < units.size(); ++i) {
            const yb::Point2 m = metres[i];
            CAPTURE(m.x);
            CAPTURE(m.y);
            std::size_t mark = 0;
            {
                const std::lock_guard lock(rig.out->m);
                mark = rig.out->records.size();
            }
            context.set_cursor(units[i]);
            const auto here = [&](const ym::MappedRecord& r) {
                return r.schema->id == "tetra_cell" && text(r, "sim_floor_id") == f.id &&
                       std::fabs(num(r, "sim_x_m").value_or(1e9) - m.x) < 1e-6 &&
                       std::fabs(num(r, "sim_y_m").value_or(1e9) - m.y) < 1e-6;
            };
            // 10 T1 records after 5 that let the receiver settle, and the
            // scenario's macro cell M1 on 390.2 MHz.
            std::vector<ym::MappedRecord> got;
            bool macro = false;
            REQUIRE(rig.out->wait([&] {
                got.clear();
                std::size_t seen = 0;
                for (std::size_t k = mark; k < rig.out->records.size(); ++k) {
                    const ym::MappedRecord& r = rig.out->records[k];
                    if (!here(r)) continue;
                    if (r.frequency_hz == 390'200'000 && text(r, "cell_id") == "M1") macro = true;
                    if (r.frequency_hz != 390'000'000) continue;
                    if (++seen > 5) got.push_back(r);
                }
                return got.size() >= 10 && macro;
            }));
            double sum = 0.0;
            for (const auto& r : got) {
                CHECK(r.correction.calibrated);
                CHECK(text(r, "cell_id") == "T1");
                sum += r.level;
            }
            const double sim_dbm = sum / static_cast<double>(got.size());
            MESSAGE(f.id << " (" << m.x << ", " << m.y << ") m: ray tracer " << want.best_dbm[i]
                         << " dBm, simulator " << sim_dbm << " dBm");
            CHECK(std::fabs(want.best_dbm[i] - sim_dbm) <= 0.5);
        }
    }
    dev->stop();
    dev->close();
}

TEST_CASE("DEMO-09 every demo scenario can be selected in the simulator and is the one that runs") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    const auto scenarios = yd::read_demo_scenarios(fs::path(DEMO_DATA_DIR) / "scenarios.json");
    REQUIRE(scenarios.size() >= 5);
    std::set<std::string> ids;
    for (const auto& s : scenarios) ids.insert(s.id);
    // DEMO-09: good and weak DAS, a serving-cell change between floors, a
    // failed site and a weak-signal case.
    for (const char* want : {"indoor_good_das", "indoor_weak_das", "floor_change", "failed_site", "weak_signal"})
        CHECK(ids.count(want) == 1);
    // The simulator's scenario setting lists the scenario folder by id.
    std::vector<std::string> folder;
    for (const auto& f : fs::directory_iterator(RADIOS_SCENARIO_DIR))
        if (f.path().extension() == ".json") folder.push_back(f.path().stem().string());
    std::sort(folder.begin(), folder.end());
    for (const auto& s : scenarios) {
        CAPTURE(s.id);
        CHECK(!s.title.empty());
        const auto it = std::find(folder.begin(), folder.end(), s.id);
        REQUIRE(it != folder.end());
        std::set<std::string> cells;
        std::ifstream in(fs::path(RADIOS_SCENARIO_DIR) / (s.id + ".json"));
        const nlohmann::json scenario = nlohmann::json::parse(in);
        for (const auto& c : scenario.at("cells"))
            if (c.value("on_air", true)) cells.insert(c.at("id").get<std::string>());
        Rig rig{true};
        TetraPlugin tetra{rig};
        const yr::RadioPluginEntry e = rig.host->load_file(path);
        REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
        const yr::EnumerateResult en = rig.host->enumerate(e.id);
        REQUIRE(!en.devices.empty());
        const std::string key = en.devices.front().key;
        rig.state->set_device_setting(e.id, key, "scenario", yp::EnumValue{1 + static_cast<std::int64_t>(it - folder.begin())});
        rig.state->set_device_setting(e.id, key, "record_rate_hz", 10.0);
        std::string error;
        auto dev = rig.host->open(e.id, key, error);
        REQUIRE_MESSAGE(dev, error);
        REQUIRE(dev->start(serving(0)) == YR_OK);
        // Records of the scenario's own cells only.
        std::set<std::string> seen;
        REQUIRE(rig.out->wait([&] {
            seen.clear();
            std::size_t n = 0;
            for (const ym::MappedRecord& r : rig.out->records) {
                if (r.schema->id != "tetra_cell") continue;
                const std::string id = text(r, "cell_id");
                if (!id.empty()) seen.insert(id), ++n;
            }
            return n >= 5;
        }));
        for (const std::string& id : seen) {
            CAPTURE(id);
            CHECK(cells.count(id) == 1);
        }
        dev->stop();
        dev->close();
    }
}

TEST_CASE("DEMO-09 the good DAS scenario measures stronger indoor levels than the weak one (yantra-gen ground truth)") {
    const std::string dir = E2E_RECORDS_DIR;
    if (dir.empty() || !fs::exists(fs::path(dir) / "indoor_good_das.records.jsonl")) {
        MESSAGE("no record files of the radios' scenarios: skipped");
        return;
    }
    // Mean serving level of the DAS cell D0 over the same walk.
    const auto mean_d0 = [&](const std::string& id) {
        const auto f = ym::read_record_file(fs::path(dir) / (id + ".records.jsonl"));
        REQUIRE(f.ok());
        FileRun run;
        run.run(*f.value);
        double sum = 0.0;
        std::size_t n = 0;
        for (const ym::MappedRecord& r : run.records())
            if (r.schema->id == "tetra_cell" && text(r, "cell_id") == "D0") sum += r.level, ++n;
        REQUIRE(n > 10);
        return sum / static_cast<double>(n);
    };
    const double good = mean_d0("indoor_good_das");
    const double weak = mean_d0("indoor_weak_das");
    MESSAGE("D0 mean level: good DAS " << good << " dBm, weak DAS " << weak << " dBm");
    CHECK(good - weak >= 20.0);
    CHECK(good > -50.0);
    CHECK(weak < -70.0);
}
