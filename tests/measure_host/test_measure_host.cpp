// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The measurement pipeline behind the radio host (OA-13, RF-01, RF-08,
// TST-23, TST-31, PLG-05, PLG-12): records pushed by radio plugins go
// through the host's sink into the pipeline and come out as cycles. The
// fake radio plugin (tests/radio_host) pushes late, out-of-order,
// duplicate and burst records with a ManualClock driving the cycles; the
// simulator runs end to end on the real clock with a CycleRunner; the
// beacon example radio is mapped onto the example beacon technology
// plugin's schemas. Every wait is on an observable condition.

#include <doctest/doctest.h>

#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <yantra/core/context/context.h>
#include <yantra/core/measure/measurement_pipeline.h>
#include <yantra/core/platform/clock.h>
#include <yantra/core/platform/dynlib.h>
#include <yantra/core/plugin/loader.h>
#include <yantra/core/radio/radio_host.h>

#include "fake_control.h"

namespace fs = std::filesystem;
namespace ym = yantra::measure;
namespace yp = yantra::plugin;
namespace yr = yantra::radio;
namespace yc = yantra::context;
namespace sf = yantra::measure::sample_flag;
using Kind = ym::RadioEvent::Kind;
using std::chrono::milliseconds;

namespace {

const milliseconds kWait{30'000 * YANTRA_TEST_TIME_SCALE};
constexpr std::int64_t kSecond = 1'000'000'000;
constexpr std::uint64_t kHz = 390'012'500;

// What the pipeline gave out, with waits on it.
struct Collector final : ym::MeasurementObserver {
    std::mutex m;
    std::condition_variable cv;
    std::vector<ym::MappedRecord> records;
    std::vector<ym::MeasurementCycle> cycles;
    std::vector<ym::RadioEvent> events;
    // While set, on_record waits (a pipeline stage that does not keep up).
    bool gate = false;
    int waiting = 0;

    void on_record(const ym::MappedRecord& r) override {
        std::unique_lock lock(m);
        records.push_back(r);
        ++waiting;
        cv.notify_all();
        cv.wait(lock, [this] { return !gate; });
        --waiting;
    }
    void on_cycle(const ym::MeasurementCycle& c) override {
        const std::lock_guard lock(m);
        cycles.push_back(c);
        cv.notify_all();
    }
    void on_event(const ym::RadioEvent& e) override {
        const std::lock_guard lock(m);
        events.push_back(e);
        cv.notify_all();
    }
    bool wait(const std::function<bool()>& pred) {
        std::unique_lock lock(m);
        return cv.wait_for(lock, kWait, pred);
    }
    bool has_event(Kind k) {
        for (const auto& e : events)
            if (e.kind == k) return true;
        return false;
    }
    void open_gate() {
        {
            const std::lock_guard lock(m);
            gate = false;
        }
        cv.notify_all();
    }
};

// The fake radio plugin's test functions (the module the host loaded).
struct Fake {
    std::unique_ptr<yantra::platform::DynamicLibrary> lib;
    fake::Control* ctl = nullptr;
    fake::PushFn push = nullptr;
    fake::PushOneFn push_one = nullptr;
    fake::UnplugFn unplug = nullptr;
    fake::ReplugFn replug = nullptr;

    Fake() {
        std::string error;
        lib = yantra::platform::DynamicLibrary::open(FAKE_RADIO_PATH, error);
        REQUIRE_MESSAGE(lib, error);
        ctl = reinterpret_cast<fake::ControlFn>(lib->symbol("fake_control", error))();
        push = reinterpret_cast<fake::PushFn>(lib->symbol("fake_push", error));
        push_one = reinterpret_cast<fake::PushOneFn>(lib->symbol("fake_push_one", error));
        unplug = reinterpret_cast<fake::UnplugFn>(lib->symbol("fake_unplug", error));
        replug = reinterpret_cast<fake::ReplugFn>(lib->symbol("fake_replug", error));
        REQUIRE((ctl && push && push_one && unplug && replug));
        // The module stays loaded between test cases: start from scratch.
        const std::lock_guard lock(ctl->m);
        ctl->present = true;
        for (int i = 0; i < fake::kCallCount; ++i) {
            ctl->hang[i] = false;
            ctl->result[i] = 0;
        }
        ctl->released = false;
        ctl->block_context = false;
    }
};

// A radio host feeding a pipeline (ManualClock unless `real_clock`).
struct Rig {
    std::shared_ptr<yantra::platform::ManualClock> manual;
    std::shared_ptr<Collector> out = std::make_shared<Collector>();
    std::shared_ptr<ym::TechnologyCatalog> catalog = std::make_shared<ym::TechnologyCatalog>();
    std::shared_ptr<ym::MeasurementPipeline> pipeline;
    std::shared_ptr<yp::MemoryStateStore> state = std::make_shared<yp::MemoryStateStore>();
    std::unique_ptr<yr::RadioHost> host;

    explicit Rig(bool real_clock = false, std::size_t sink_capacity = 4096) {
        ym::MeasurementPipeline::Options o;
        o.catalog = catalog;
        if (!real_clock) {
            manual = std::make_shared<yantra::platform::ManualClock>(
                yantra::platform::monotonic_ns());
            o.clock = manual;
        }
        pipeline = std::make_shared<ym::MeasurementPipeline>(std::move(o));
        pipeline->add_observer(out);
        yr::RadioHostConfig c;
        c.state = state;
        c.pipeline = pipeline;
        c.context = std::make_shared<yc::ContextPublisher>();
        c.sink_capacity = sink_capacity;
        host = std::make_unique<yr::RadioHost>(std::move(c));
    }
    // ManualClock: to the end of the next cycle, tick, the cycle.
    ym::MeasurementCycle next() {
        manual->set(pipeline->next_cycle_ns());
        REQUIRE(pipeline->tick() == 1);
        const std::lock_guard lock(out->m);
        return out->cycles.back();
    }
    bool wait_accepted(std::uint64_t n) {
        return pipeline->wait_stats([n](const ym::PipelineStats& s) { return s.records >= n; }, kWait);
    }
};

yr::Plan serving(std::uint64_t hz = kHz) {
    yr::Plan p;
    p.type = YR_PLAN_SERVING;
    p.serving_hz = hz;
    return p;
}

std::shared_ptr<yr::RadioDevice> start_fake(Rig& rig) {
    REQUIRE(rig.host->load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    std::string error;
    auto dev = rig.host->open("test.fake", "fake:0", error);
    REQUIRE_MESSAGE(dev, error);
    REQUIRE(dev->start(serving()) == YR_OK);
    return dev;
}

}  // namespace

TEST_CASE("measurement host: radio records become cycle samples with provenance and flags (RF-01, RF-08, PLG-12)") {
    Fake fake;
    Rig rig;
    auto dev = start_fake(rig);
    REQUIRE(fake.push_one(0, kHz, -47.0, 0) == YR_OK);
    REQUIRE(rig.wait_accepted(1));
    const ym::MeasurementCycle c = rig.next();
    const ym::CarrierSample* s = c.serving("fake:0");
    REQUIRE(s != nullptr);
    CHECK(s->flags == (sf::kSimulated | sf::kUncalibrated));
    CHECK(s->latest.level == doctest::Approx(-47.0));
    CHECK(s->latest.radio_plugin_id == "test.fake");
    CHECK(s->latest.device_key == "fake:0");
    CHECK(s->latest.stream_id == dev->stream_id());
    CHECK(s->latest.provenance.schema_id == "fake_cell");
    CHECK(s->latest.generic);
    CHECK(s->records_in_cycle == 1);
    CHECK(dev->stop() == YR_OK);
    // The stop removes the device's carriers from the next cycle.
    REQUIRE(rig.out->wait([&] { return rig.out->has_event(Kind::Stopped); }));
    CHECK(rig.next().samples.empty());
}

TEST_CASE("measurement host: late, out-of-order and duplicate records have defined results (TST-23)") {
    Fake fake;
    Rig rig;
    auto dev = start_fake(rig);
    const std::int64_t base = rig.manual->monotonic_ns();
    REQUIRE(fake.push_one(base + 500'000'000, kHz, -50.0, 0) == YR_OK);
    REQUIRE(fake.push_one(base + 200'000'000, kHz, -70.0, 0) == YR_OK);  // out of order
    REQUIRE(fake.push_one(base + 500'000'000, kHz, -50.0, 0) == YR_OK);  // duplicate
    REQUIRE(fake.push_one(base - 1'500'000'000, kHz + 25'000, -80.0, 0) == YR_OK);  // late
    REQUIRE(fake.push_one(base + 600'000'000, kHz + 50'000, -90.0, YR_RECORD_NO_SYNC | YR_RECORD_OVERLOAD) ==
            YR_OK);
    REQUIRE(rig.wait_accepted(5));
    const ym::PipelineStats st = rig.pipeline->stats();
    CHECK(st.accepted == 3);
    CHECK(st.out_of_order == 1);
    CHECK(st.duplicates == 1);
    CHECK(st.late == 1);
    const ym::MeasurementCycle c = rig.next();
    REQUIRE(c.samples.size() == 3);
    CHECK(c.samples[0].latest.level == doctest::Approx(-50.0));
    CHECK(c.samples[0].flags == (sf::kSimulated | sf::kUncalibrated));
    CHECK(c.samples[1].flags == (sf::kSimulated | sf::kUncalibrated | sf::kLate));
    CHECK(c.samples[2].flags ==
          (sf::kSimulated | sf::kUncalibrated | sf::kNoSync | sf::kOverload));
    CHECK(dev->stop() == YR_OK);
}

TEST_CASE("measurement host: a burst the pipeline cannot keep up with is dropped and counted, never blocking (TST-23, RAD-24)") {
    Fake fake;
    Rig rig(false, 16);
    auto dev = start_fake(rig);
    {
        const std::lock_guard lock(rig.out->m);
        rig.out->gate = true;
    }
    // The first record holds the delivery thread in the observer.
    REQUIRE(fake.push_one(0, kHz, -40.0, 0) == YR_OK);
    REQUIRE(rig.out->wait([&] { return rig.out->waiting == 1; }));
    // The plugin pushes on and is never made to wait (fake_push returns).
    CHECK(fake.push(2000, 0) == 2000);
    const yr::DeviceStats held = dev->stats();
    CHECK(held.sink.dropped >= 2000 - 16);
    rig.out->open_gate();
    CHECK(dev->stop() == YR_OK);
    const yr::DeviceStats st = dev->stats();
    CHECK(st.sink.accepted + st.sink.dropped == 2001);
    CHECK(rig.pipeline->stats().records == st.sink.delivered);
    CHECK(st.sink.delivered == st.sink.accepted);
}

TEST_CASE("measurement host: an unplugged device's samples are flagged until it is back (NFR-04, RF-08)") {
    Fake fake;
    Rig rig;
    auto dev = start_fake(rig);
    REQUIRE(fake.push_one(0, kHz, -45.0, 0) == YR_OK);
    REQUIRE(rig.wait_accepted(1));
    CHECK((rig.next().flags & sf::kDeviceLost) == 0);
    fake.unplug();
    REQUIRE(rig.out->wait([&] { return rig.out->has_event(Kind::Lost); }));
    ym::MeasurementCycle c = rig.next();
    REQUIRE(c.samples.size() == 1);
    CHECK((c.samples[0].flags & (sf::kDeviceLost | sf::kHeld)) == (sf::kDeviceLost | sf::kHeld));
    fake.replug(1);
    REQUIRE(rig.out->wait([&] { return rig.out->has_event(Kind::Reopened); }));
    // Measured now on the pipeline's (manual) clock, which ran ahead of the real one.
    REQUIRE(fake.push_one(rig.manual->monotonic_ns(), kHz, -44.0, 0) == YR_OK);
    REQUIRE(rig.wait_accepted(2));
    c = rig.next();
    REQUIRE(c.samples.size() == 1);
    CHECK(c.samples[0].flags == (sf::kSimulated | sf::kUncalibrated));
    CHECK(c.samples[0].latest.level == doctest::Approx(-44.0));
    CHECK(dev->stop() == YR_OK);
}

TEST_CASE("measurement host: the simulator end to end gives a serving sample every second (RF-01, TST-31)") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator.so";
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    Rig rig(true);
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(en.status == YR_OK);
    REQUIRE(!en.devices.empty());
    const std::string key = en.devices.front().key;
    rig.state->set_device_setting(e.id, key, "record_rate_hz", 4.0);
    std::string error;
    auto dev = rig.host->open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    {
        ym::CycleRunner runner(*rig.pipeline);
        REQUIRE(dev->start(serving(0)) == YR_OK);
        // Three cycles with a fresh serving sample (the sanitizer builds run
        // the simulator several times slower than real time: wait for them).
        std::size_t first = 0;
        auto fresh_cycles = [&] {
            std::size_t n = 0;
            for (const auto& c : rig.out->cycles) {
                const ym::CarrierSample* s = c.serving(key);
                n += s != nullptr && (s->flags & sf::kHeld) == 0 ? 1 : 0;
            }
            return n;
        };
        REQUIRE(rig.out->wait([&] { return fresh_cycles() >= 3; }));
        CHECK(dev->stop() == YR_OK);
        const std::lock_guard lock(rig.out->m);
        const auto& cs = rig.out->cycles;
        while (cs[first].serving(key) == nullptr) ++first;
        // From its first sample on, the serving carrier is in every cycle (no gaps).
        bool gone = false;  // after the stop the device's carriers leave for good
        for (std::size_t i = first; i < cs.size(); ++i) {
            const ym::CarrierSample* s = cs[i].serving(key);
            CHECK_MESSAGE(!(gone && s != nullptr), "cycle " << i << ": serving carrier after a gap");
            if (s == nullptr) {
                gone = true;
                continue;
            }
            CHECK((s->flags & (sf::kSimulated | sf::kUncalibrated)) == (sf::kSimulated | sf::kUncalibrated));
            CHECK((s->flags & sf::kDeviceLost) == 0);
            CHECK(std::isfinite(s->latest.level));
            CHECK(s->latest.provenance.technology_id == "tetra");
            CHECK(s->latest.radio_plugin_id == e.id);
            CHECK(s->latest.generic);  // no TETRA technology plugin loaded here
        }
        // TST-31: timestamps on the 1 s grid, sequence without gaps.
        for (std::size_t i = 1; i < cs.size(); ++i) {
            CHECK(cs[i].sequence == cs[i - 1].sequence + 1);
            CHECK(cs[i].time.monotonic_ns - cs[i - 1].time.monotonic_ns == kSecond);
            CHECK(cs[i].time.utc_ns > cs[i - 1].time.utc_ns);
        }
    }
    CHECK(rig.pipeline->stats().skipped_cycles == 0);
}

TEST_CASE("measurement host: beacon radio records map onto the example technology plugin's schema (PLG-05)") {
    const fs::path path = fs::path(RADIO_TEST_PLUGIN_DIR) / "beacon.so";
    if (!fs::exists(path)) {
        MESSAGE("no beacon plugin (prebuilt radio plugins): skipped");
        return;
    }
    yp::PluginLoader loader(yp::PluginLoaderConfig{});
    REQUIRE(loader.load_file(EXAMPLE_BEACON_PATH).status == yp::PluginStatus::Enabled);
    const yp::PluginInfo* info = loader.info("yantra.example.beacon");
    REQUIRE(info != nullptr);
    Rig rig;
    REQUIRE(rig.catalog->add(*info));
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(!en.devices.empty());
    std::string error;
    auto dev = rig.host->open(e.id, en.devices.front().key, error);
    REQUIRE_MESSAGE(dev, error);
    REQUIRE(dev->start(serving(0)) == YR_OK);
    REQUIRE(rig.out->wait([&] { return !rig.out->records.empty(); }));
    CHECK(dev->stop() == YR_OK);
    const ym::PipelineStats st = rig.pipeline->stats();
    CHECK(st.schema_mismatch == 0);
    CHECK(st.generic == 0);
    const std::lock_guard lock(rig.out->m);
    const ym::MappedRecord& r = rig.out->records.front();
    CHECK_FALSE(r.generic);
    CHECK(r.provenance.technology_id == "beacon");
    CHECK(r.provenance.plugin_id == "yantra.example.beacon");
    CHECK(r.provenance.plugin_version == "1.0.0");
    REQUIRE(r.schema);
    CHECK(r.schema->fields.size() == r.record.values.size());
    // The record is valid in the technology schema (plugin API checks).
    const ym::RecordView view(r.record, r.schema_index);
    const yp::RecordCheck check = yp::parse_record(view.get(), *r.schema);
    CHECK(check.parsed.ok());
}
