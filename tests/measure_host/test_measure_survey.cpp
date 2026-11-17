// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A walk test end to end (IND-20, IND-30, GEO-02, RAD-19, REC-07,
// TST-32, TST-34; docs/SURVEY.md): the radios' simulator through
// RadioHost, the measurement pipeline (real clock, CycleRunner) and the
// TETRA plugin. Its position comes from the walk test's walk position
// (position_source auto: the walk while one runs) on a floor drawn in a
// project (scale, antenna, a concrete wall). A continuous walk is clicked
// on the plan; its points lie at the interpolated positions, the walker
// was there, and the levels are what the multi-wall model predicts at the
// simulator's position. The walk is recorded; its replay through a
// WalkTest of its own gives the same points and statistics; a "window"
// and a "report" get the same point list.

#include <cmath>
#include <numbers>

#include <nlohmann/json.hpp>

#include <yantra/core/building/edit.h>
#include <yantra/core/context/building_context.h>
#include <yantra/core/project/project.h>
#include <yantra/core/recording/replay.h>
#include <yantra/core/recording/writer.h>
#include <yantra/core/survey/points.h>
#include <yantra/core/survey/recordings.h>
#include <yantra/core/survey/stats.h>
#include <yantra/core/survey/viewer.h>
#include <yantra/core/survey/walk_test.h>

#include "measure_rig.h"
#include "model_scenes.hpp"

using namespace mh;
namespace yb = yantra::building;
namespace ypj = yantra::project;
namespace ys = yantra::survey;
namespace rec = yantra::recording;
using nlohmann::json;

namespace {

constexpr std::uint64_t kCarrier = 390'000'000;  // D0's carrier: the plan cell replaces D0 (RAD-61)
constexpr std::int64_t kNs = 1'000'000'000;

// The office of the 7.3 wall test: 20 x 10 m (0.05 m per unit), the
// DAS antenna at (3, 5) m with 10 dBm EIRP on cell-1 (390.0 MHz), a
// concrete wall at x = 8 m.
std::shared_ptr<ypj::Project> office(const fs::path& file, std::string& floor_id) {
    auto p = ypj::Project::create(file, ypj::ProjectInfo{"Simulated office", "Office", "", ""});
    yb::Floor f;
    f.name = "Ground floor";
    f.plan_width = 400;
    f.plan_height = 200;
    floor_id = p->add_floor(f);
    p->set_scale(floor_id, *yb::two_point_scale({0, 190}, {400, 190}, 20.0));
    p->set_reference_point(floor_id, yb::Point2{0, 0});
    const std::string cell = p->add_cell(yb::Cell{"", "Indoor DAS", "tetra", "tetra_cell", kCarrier,
                                                  {{"mcc", 901}, {"mnc", 1}, {"la", 100}, {"colour_code", 1}}});
    yb::Antenna a;
    a.name = "DAS 1";
    a.position = {60, 100};
    a.height_m = 2.8;
    a.eirp_dbm = 10.0;
    a.cell = cell;
    p->add_antenna(floor_id, a);
    p->add_wall(floor_id, yb::Wall{"", "concrete", {{160, 0}, {160, 200}}, std::nullopt});
    return p;
}

fs::path scratch_dir(const std::string& name) {
    const fs::path p = fs::path(MEASURE_SCRATCH_DIR) / "survey" / (name + "." + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

yb::Point2 lerp(yb::Point2 a, yb::Point2 b, double f) { return {a.x + f * (b.x - a.x), a.y + f * (b.y - a.y)}; }

// The drawn cell's carrier in a point's sample (serving once decoded).
const json* carrier_of(const json& sample) {
    const json* found = nullptr;
    for (const json& c : sample["carriers"])
        if (c.value("f", std::uint64_t{0}) == kCarrier && (found == nullptr || c.value("role", "") == "serving")) found = &c;
    return found;
}

// The points' value: the level on the drawn cell's carrier.
ys::Metric drawn_cell() {
    ys::Metric m;
    m.carrier = ys::Metric::Carrier::Frequency;
    m.frequency_hz = kCarrier;
    return m;
}

}  // namespace

TEST_CASE("TST-10 simulator walk test: clicked segments give points at the interpolated positions with the levels the multi-wall model predicts; the recorded walk replays to the same points and statistics and loads into the viewer (IND-20, IND-30, GEO-02, RAD-19, REC-07, TST-34, PP-01)") {
    const fs::path dir = scratch_dir("walk");
    // Removed after every object below is gone: the projects, the walk
    // test and the simulator rig hold the project files open (Windows
    // refuses to remove an open file).
    struct RemoveAtEnd {
        fs::path dir;
        ~RemoveAtEnd() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } remove_at_end{dir};
    std::string floor_id;
    auto project = office(dir / "live.yantra", floor_id);
    const yb::Building b = project->building();

    // The simulator (records at 10 Hz, gain 0) and the default scenario's
    // calibration; it is told the plan, not the cursor.
    struct Sim {
        Rig rig{true};
        TetraPlugin tetra{rig};
    } sim;
    const fs::path so = fs::path(RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(so)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    yc::BuildingContext context(sim.rig.publisher, yantra::test::model_scenes());
    context.set_building(b);
    context.show_floor(floor_id);
    const yr::RadioPluginEntry e = sim.rig.host->load_file(so);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const std::string key = sim.rig.host->enumerate(e.id).devices.front().key;
    sim.rig.state->set_device_setting(e.id, key, "record_rate_hz", 10.0);
    sim.rig.state->set_device_setting(e.id, key, "gain_db", 0.0);
    std::string error;
    auto dev = sim.rig.host->open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    auto table = std::make_shared<ycal::Table>();
    table->put(flat_entry(*dev->calibration_key(), -20.0, true));
    sim.rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table, ycal::AntennaSettings{}));

    rec::RecorderConfig cfg;
    cfg.folder = dir / "rec";
    cfg.keyframe_s = 2;
    auto recorder = rec::Recorder::create(sim.rig.pipeline, cfg);

    ys::WalkTestOptions o;
    o.project = project;
    o.floor_id = floor_id;
    o.mode = ys::WalkMode::Continuous;
    o.name = "Office walk";
    o.recorder = recorder;
    o.walker = [&](const std::optional<yc::FloorPoint>& at) { context.set_walk(at); };
    auto walk = std::make_shared<ys::WalkTest>(o);
    sim.rig.pipeline->add_observer(walk);

    // The route: A (5, 5) m -> B (7, 5) m before the wall -> C (12, 5) m
    // behind it, 4 s per segment, the walker moved every 20 ms.
    const yb::Point2 A{100, 100}, B{140, 100}, C{240, 100};
    const auto now = [&] { return sim.rig.pipeline->clock().monotonic_ns(); };
    std::vector<std::int64_t> clicks;
    std::string recording_session;
    {
        ym::CycleRunner runner(*sim.rig.pipeline);
        REQUIRE(dev->start(serving(kCarrier)) == YR_OK);
        // Monitoring until the drawn cell is decoded and serving.
        REQUIRE(sim.rig.out->wait([&] {
            if (sim.rig.out->cycles.empty()) return false;
            const ym::CarrierSample* s = sim.rig.out->cycles.back().serving(key);
            return s != nullptr && s->latest.frequency_hz == kCarrier;
        }));
        REQUIRE_MESSAGE(recorder->start("walk", error), error);
        recording_session = recorder->status().session_id;
        walk->start(now());
        walk->walker(A);
        // The receiver is at A before the walk starts (observable: a record there).
        REQUIRE(sim.rig.out->wait([&] {
            for (const auto& r : sim.rig.out->records)
                if (r.frequency_hz == kCarrier && r.schema->id == "tetra_cell") {
                    const auto i = r.schema->field_index("sim_x_m");
                    if (i && std::get_if<double>(&r.record.values[*i]) && *std::get_if<double>(&r.record.values[*i]) == 5.0)
                        return true;
                }
            return false;
        }));
        for (const yb::Point2& to : {B, C}) {
            const yb::Point2 from = clicks.empty() ? A : B;
            if (clicks.empty()) {
                clicks.push_back(now());
                walk->click(clicks.back(), A);
            }
            const std::int64_t t0 = clicks.back();
            const std::int64_t span = 4 * kNs;
            for (std::int64_t t = now(); t < t0 + span; t = now()) {
                walk->walker(lerp(from, to, static_cast<double>(t - t0) / static_cast<double>(span)));
                std::this_thread::sleep_for(milliseconds(20));
            }
            clicks.push_back(now());
            walk->click(clicks.back(), to);
        }
        // Until a cycle after the last click has been built.
        const std::int64_t last = clicks.back();
        REQUIRE(sim.rig.out->wait([&] { return !sim.rig.out->cycles.empty() && sim.rig.out->cycles.back().time.monotonic_ns > last; }));
        walk->finish(now());
        CHECK(dev->stop() == YR_OK);
    }
    recorder->stop();
    recorder->detach();
    dev->close();
    REQUIRE(clicks.size() == 3);

    // The points: one per cycle from the first to the last click, each at
    // p(T) on its segment (exactly), numbered in time order.
    const ys::PointList live = ys::point_list(*project, ys::PointQuery{floor_id, {}, drawn_cell(), std::nullopt});
    std::size_t expected = 0;
    for (const auto& c : sim.rig.out->cycles)
        if (c.time.monotonic_ns >= clicks[0] && c.time.monotonic_ns <= clicks[2]) ++expected;
    MESSAGE(live.count() << " points of " << expected << " cycles in the walk");
    REQUIRE(live.count() == expected);
    REQUIRE(live.count() >= 6);
    const auto fspl = [](double horizontal_m) {
        const double d = std::hypot(horizontal_m, 2.8 - 1.5);
        return 20.0 * std::log10(4.0 * std::numbers::pi * d * static_cast<double>(kCarrier) / 299'792'458.0);
    };
    const double wall_db = *b.wall_attenuation_db(b.floors[0].walls[0], static_cast<double>(kCarrier) / 1e6);
    double worst = 0.0, sum = 0.0;
    std::size_t measured = 0;
    const auto stored = project->points(walk->session_id());
    for (std::size_t i = 0; i < live.points.size(); ++i) {
        const ys::SurveyPoint& p = live.points[i];
        CAPTURE(i);
        CHECK(p.sequence == static_cast<std::int64_t>(i) + 1);
        const bool first = p.time_ns <= clicks[1];
        const std::int64_t ta = first ? clicks[0] : clicks[1];
        const std::int64_t tb = first ? clicks[1] : clicks[2];
        const yb::Point2 expect = lerp(first ? A : B, first ? B : C, static_cast<double>(p.time_ns - ta) / static_cast<double>(tb - ta));
        CHECK(p.group == (first ? 1u : 2u));
        CHECK(p.plan.x == doctest::Approx(expect.x).epsilon(1e-12));
        CHECK(p.plan.y == doctest::Approx(expect.y).epsilon(1e-12));
        REQUIRE(p.metres);
        CHECK(p.metres->x == doctest::Approx(expect.x * 0.05).epsilon(1e-12));
        // The simulator measured where the walker was: within the walker's
        // speed (at most 1.25 m/s) times the value's age plus the
        // position's delay (at most one record interval and the 20 ms
        // walker step), and on this floor.
        const json sample = json::parse(stored[i].sample);
        const json* c = carrier_of(sample);
        INFO(sample.dump());
        REQUIRE(c != nullptr);
        REQUIRE(c->contains("sim"));
        CHECK((*c)["sim"]["floor"] == floor_id);
        const double sx = (*c)["sim"]["x_m"].get<double>();
        const double sy = (*c)["sim"]["y_m"].get<double>();
        const double lag_s = (*c)["age_ms"].get<double>() / 1e3 + 1.0;
        CHECK(std::hypot(sx - p.metres->x, sy - p.metres->y) <= 1.25 * lag_s + 0.05);
        // Multi-wall model (RAD-73): free space from the antenna at (3, 5) m
        // plus the wall when behind it. A record's level is measured over
        // its interval while the walker moves, and under load an interval
        // lasts up to a second: the level is compared with the model over
        // the last 1.25 m walked before the record's position (1 s at the
        // fastest 1.25 m/s; the route only goes in +x from 5 m). Seen
        // under ASan with ctest -j: +1 dB just behind the wall, +1.3 dB
        // against the model at the point itself.
        const auto model = [&](double x) {
            return 10.0 - fspl(std::hypot(x - 3.0, sy - 5.0)) - (x > 8.0 ? wall_db : 0.0);
        };
        double lo = model(sx), hi = lo;
        for (double x = std::max(5.0, sx - 1.25); x < sx; x += 0.01) {
            lo = std::min(lo, model(x));
            hi = std::max(hi, model(x));
        }
        REQUIRE(p.value);
        CHECK(p.unit == "dBm");
        const double d = *p.value < lo ? *p.value - lo : *p.value > hi ? *p.value - hi : 0.0;
        worst = std::max(worst, std::fabs(d));
        sum += d;
        ++measured;
    }
    MESSAGE("levels outside the multi-wall model's range: worst " << worst << " dB, mean " << sum / static_cast<double>(measured)
                                                          << " dB over " << measured << " points");
    // The simulator's channel has no fading on this own-plan cell: the
    // receiver's level estimate is within a few tenths of the model.
    CHECK(worst <= 0.5);
    CHECK(std::fabs(sum / static_cast<double>(measured)) <= 0.25);

    // The session (IND-20) and the recording list (REC-03).
    const auto session = project->session(walk->session_id());
    CHECK(session->walk_mode == "continuous");
    CHECK(session->recording_session == recording_session);
    CHECK(recording_session.size() == 16);
    CHECK(json::parse(session->plugins).contains("yantra.tetra"));
    CHECK(!session->ended_utc.empty());
    std::vector<fs::path> files;
    for (const auto& f : fs::directory_iterator(cfg.folder)) files.push_back(f.path());
    std::sort(files.begin(), files.end());
    REQUIRE(!files.empty());
    for (const auto& f : files) ys::add_recording_file(*project, f);
    CHECK(project->recordings().size() == files.size());
    CHECK(project->recordings()[0].session_name == "walk");
    CHECK(project->recordings()[0].size == fs::file_size(files[0]));

    // The "window" and the "report": one API, the same list and figures.
    const ys::StatsOptions so_opts{{-60, -70}, {10, 50, 90}};
    const auto window = [&] { return ys::point_list(*project, ys::PointQuery{floor_id, {}, drawn_cell(), std::nullopt}); };
    const auto report = [&] {
        const ys::PointList l = ys::point_list(*project, ys::PointQuery{floor_id, {}, drawn_cell(), std::nullopt});
        return std::pair{l, ys::floor_stats(l, so_opts)};
    };
    const auto [printed, figures] = report();
    CHECK(window() == printed);
    CHECK(printed == live);
    CHECK(figures.all.points == live.count());
    CHECK(ys::point_counts(*project)[0].second == live.count());

    // The replay of the recording, into a project of its own: the same
    // points (positions, values, colours) and statistics.
    std::string floor2;
    auto replayed = office(dir / "replay.yantra", floor2);
    REQUIRE(floor2 == floor_id);
    rec::ReplayOptions ro;
    ro.catalog = sim.rig.catalog;
    auto replay = rec::Replay::open(files, ro, error);
    REQUIRE_MESSAGE(replay, error);
    ys::WalkTestOptions o2;
    o2.project = replayed;
    o2.floor_id = floor_id;
    auto again = std::make_shared<ys::WalkTest>(o2);
    replay->add_observer(again);
    replay->add_listener(std::make_shared<ys::WalkReplay>(again));
    replay->advance_to(replay->duration_ns());
    CHECK(replay->mismatches() == 0);
    CHECK_FALSE(again->running());
    const ys::PointList back = ys::point_list(*replayed, ys::PointQuery{floor_id, {}, drawn_cell(), std::nullopt});
    REQUIRE(back.count() == live.count());
    CHECK(back == live);
    CHECK(ys::floor_stats(back, so_opts) == figures);
    const auto stored_back = replayed->points(again->session_id());
    for (std::size_t i = 0; i < stored.size(); ++i) {
        CAPTURE(i);
        CHECK(stored_back[i] == stored[i]);
    }
    CHECK(replayed->session(again->session_id())->walk_mode == "continuous");

    // PP-01: the recording loaded into the viewer (a project of its own):
    // the same points, a session tagged with the recording, the files
    // listed; loading it again is refused (no doubled points).
    std::string floor3;
    auto viewed = office(dir / "viewer.yantra", floor3);
    REQUIRE(floor3 == floor_id);
    ys::ImportOptions io;
    io.replay.catalog = sim.rig.catalog;
    std::vector<fs::path> reversed(files.rbegin(), files.rend());  // any order: sorted by file number
    const auto imported = ys::import_recordings(viewed, reversed, io);
    REQUIRE(imported.size() == 1);
    CHECK(imported[0].recording_session == recording_session);
    CHECK(imported[0].files == files);
    CHECK(imported[0].mismatches == 0);
    CHECK(imported[0].points == live.count());
    CHECK(imported[0].recording_ids.size() == files.size());
    CHECK(viewed->recordings().size() == files.size());
    REQUIRE(imported[0].sessions.size() == 1);
    const auto vs = viewed->session(imported[0].sessions[0]);
    CHECK(vs->recording_session == recording_session);
    CHECK(vs->name == "Office walk");
    CHECK(vs->walk_mode == "continuous");
    const ys::PointList shown = ys::point_list(*viewed, ys::PointQuery{floor_id, {}, drawn_cell(), std::nullopt});
    CHECK(shown == live);
    CHECK_THROWS_AS(ys::import_recordings(viewed, files, io), ypj::ProjectError);
    CHECK(viewed->point_count(floor_id) == live.count());
    // The hover readout of a point in the middle of the walk: its serving
    // cell is the drawn cell; every neighbour carrier of its sample is listed.
    const std::size_t mid = shown.count() / 2;
    const auto hit = ys::nearest_point(shown.points, shown.points[mid].plan, 0.5);
    REQUIRE(hit);
    const ys::PointDetail d = ys::point_detail(*viewed, shown.points[*hit]);
    CHECK(d.point.plan == shown.points[mid].plan);
    REQUIRE(d.serving);
    CHECK(d.serving->frequency_hz == kCarrier);
    CHECK(d.serving->cell.find("la=100") != std::string::npos);
    REQUIRE(d.strongest);
    CHECK(d.strongest->level >= d.serving->level);
    const json sample = json::parse(viewed->point(shown.points[*hit].session_id, shown.points[*hit].sequence)->sample);
    std::size_t neighbours = 0;
    for (const json& c : sample["carriers"])
        neighbours += c["role"] == "neighbour" || c["role"] == "neighbour_entry" ? 1 : 0;
    CHECK(d.neighbours.size() == neighbours);
}
