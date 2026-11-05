// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Recording and replay end to end (REC-03, REC-07, REC-10, TST-10): a
// walk of the radios' simulator through RadioHost, the measurement
// pipeline (real clock, CycleRunner, calibration table) and the TETRA
// technology plugin is recorded; its replay through a pipeline of its own
// with the same TETRA plugin gives the same cycles (levels, identities,
// C1, flags) and statistics. Also the fake radio on a ManualClock.

#include <filesystem>

#include <yantra/core/measure/sample.h>
#include <yantra/core/recording/reader.h>
#include <yantra/core/recording/replay.h>
#include <yantra/core/recording/writer.h>

#include "measure_rig.h"

using namespace mh;
namespace rec = yantra::recording;
namespace fs = std::filesystem;

namespace {

fs::path folder(const std::string& name) {
    const fs::path p = fs::path(MEASURE_SCRATCH_DIR) / "recording" / (name + "." + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

std::vector<fs::path> files_in(const fs::path& dir) {
    std::vector<fs::path> f;
    for (const auto& e : fs::directory_iterator(dir)) f.push_back(e.path());
    std::sort(f.begin(), f.end());
    return f;
}

std::vector<ym::MeasurementCycle> cycles_from(Rig& rig, std::uint64_t sequence) {
    const std::lock_guard lock(rig.out->m);
    std::vector<ym::MeasurementCycle> c;
    for (const auto& x : rig.out->cycles)
        if (x.sequence >= sequence) c.push_back(x);
    return c;
}

// Replays `files` with `catalog` to the end and compares with `live`.
void replay_matches(const std::vector<fs::path>& files, const std::shared_ptr<ym::TechnologyCatalog>& catalog,
                    const std::vector<ym::MeasurementCycle>& live, const ym::PipelineStats& stats) {
    std::string error;
    rec::ReplayOptions o;
    o.catalog = catalog;
    auto r = rec::Replay::open(files, o, error);
    REQUIRE_MESSAGE(r, error);
    auto out = std::make_shared<Collector>();
    r->add_observer(out);
    r->advance_to(r->duration_ns());
    INFO((r->mismatch_messages().empty() ? std::string() : r->mismatch_messages().front()));
    CHECK(r->mismatches() == 0);
    const std::lock_guard lock(out->m);
    REQUIRE(out->cycles.size() == live.size());
    for (std::size_t i = 0; i < live.size(); ++i) {
        const std::string d = ym::cycle_difference(live[i], out->cycles[i]);
        INFO("cycle " << live[i].sequence << ": " << d);
        REQUIRE(d.empty());
    }
    CHECK(r->pipeline().stats() == stats);
}

}  // namespace

TEST_CASE("REC-07 simulator: a recorded TETRA walk replays to the same cycles, C1 and statistics (REC-03, TST-10)") {
    Rig rig{true};
    TetraPlugin tetra{rig};
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator.so";
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(!en.devices.empty());
    const std::string key = en.devices.front().key;
    rig.state->set_device_setting(e.id, key, "record_rate_hz", 4.0);
    rig.state->set_device_setting(e.id, key, "gain_db", 0.0);
    std::string error;
    auto dev = rig.host->open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    auto table = std::make_shared<ycal::Table>();
    table->put(flat_entry(*dev->calibration_key(), -20.0, true));
    rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table, ycal::AntennaSettings{}));

    const fs::path dir = folder("sim_walk");
    rec::RecorderConfig cfg;
    cfg.folder = dir;
    cfg.compression = rec::Compression::Zstd;
    cfg.keyframe_s = 2;
    cfg.split_messages = 120;  // a sequence of files, each starting from a keyframe
    auto recorder = rec::Recorder::create(rig.pipeline, cfg);
    std::uint64_t first = 0;
    {
        ym::CycleRunner runner(*rig.pipeline);
        REQUIRE(dev->start(serving(0)) == YR_OK);
        // Monitoring until the serving cell is decoded, then record.
        REQUIRE(rig.out->wait([&] { return rig.out->cycles.size() >= 2; }));
        REQUIRE_MESSAGE(recorder->start("sim", error), error);
        first = rig.pipeline->snapshot().sequence;
        REQUIRE(rig.out->wait([&] {
            std::size_t n = 0;
            for (const auto& c : rig.out->cycles)
                if (c.sequence >= first && c.serving(key) != nullptr) ++n;
            return n >= 8;
        }));
        CHECK(dev->stop() == YR_OK);
    }
    // After the runner: every cycle that was built has been recorded.
    recorder->stop();
    recorder->detach();
    const ym::PipelineStats stats = rig.pipeline->stats();
    const auto files = files_in(dir);
    REQUIRE(files.size() >= 2);
    const auto live = cycles_from(rig, first);
    // The live run had TETRA C1 values and calibrated levels to compare.
    bool c1 = false;
    for (const auto& c : live)
        for (const auto& s : c.samples) c1 = c1 || s.latest.c1_source != ym::C1Source::None;
    CHECK(c1);
    replay_matches(files, rig.catalog, live, stats);
    dev->close();
    fs::remove_all(dir);
}

TEST_CASE("REC-07 fake radio: records delivered by RadioHost on a ManualClock replay to the same cycles (REC-03)") {
    Fake fake;
    Rig rig{false, 4096, std::int64_t{1'000} * kSecond};
    const fs::path dir = folder("fake");
    rec::RecorderConfig cfg;
    cfg.folder = dir;
    cfg.keyframe_s = 3;
    cfg.flush_thread = false;
    auto recorder = rec::Recorder::create(rig.pipeline, cfg);
    auto dev = start_fake(rig);
    REQUIRE(fake.push_one(rig.manual->monotonic_ns(), kHz, -60.0, 0) == YR_OK);
    REQUIRE(rig.wait_accepted(1));
    rig.next();
    std::string error;
    REQUIRE(recorder->start("fake", error));
    const std::uint64_t first = rig.pipeline->snapshot().sequence;
    std::uint64_t pushed = 1;
    for (int i = 0; i < 12; ++i) {
        // Each second: records of two carriers (one now and then out of
        // order, a duplicate or late), delivered, then the tick.
        const std::int64_t t = rig.manual->monotonic_ns();
        REQUIRE(fake.push_one(t + 100'000'000, kHz, -60.0 - i, 0) == YR_OK);
        REQUIRE(fake.push_one(t + 300'000'000, kHz + 25'000, -75.0 + i, 0) == YR_OK);
        pushed += 2;
        if (i % 3 == 0) {
            REQUIRE(fake.push_one(t + 50'000'000, kHz, -61.0, 0) == YR_OK);
            REQUIRE(fake.push_one(t + 300'000'000, kHz + 25'000, -75.0 + i, 0) == YR_OK);
            REQUIRE(fake.push_one(t - 1'500'000'000, kHz + 50'000, -90.0, YR_RECORD_NO_SYNC) == YR_OK);
            pushed += 3;
        }
        REQUIRE(rig.wait_accepted(pushed));
        rig.manual->set(t + 500'000'000);
        rig.next();
    }
    CHECK(dev->stop() == YR_OK);
    REQUIRE(rig.pipeline->wait_stats([](const ym::PipelineStats& s) { return s.events > 0; }, kWait));
    rig.next();
    recorder->stop();
    const ym::PipelineStats stats = rig.pipeline->stats();
    replay_matches(files_in(dir), rig.catalog, cycles_from(rig, first), stats);
    dev->close();
    fs::remove_all(dir);
}
