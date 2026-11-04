// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared rig of the measurement-host tests (test_*.cpp of this folder):
// a radio host feeding a measurement pipeline, the collector of its
// output with waits on observable conditions, the fake radio plugin's
// test functions and the TETRA technology plugin in the catalog.
#pragma once

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <cstdlib>

#include <unistd.h>

#include <yantra/core/calibration/corrector.h>
#include <yantra/core/calibration/wizard.h>
#include <yantra/core/context/context.h>
#include <yantra/core/measure/measurement_pipeline.h>
#include <yantra/core/platform/clock.h>
#include <yantra/core/platform/dynlib.h>
#include <yantra/core/plugin/loader.h>
#include <yantra/core/radio/radio_host.h>

#include "fake_control.h"

namespace mh {

namespace fs = std::filesystem;
namespace ym = yantra::measure;
namespace yp = yantra::plugin;
namespace yr = yantra::radio;
namespace yc = yantra::context;
namespace ycal = yantra::calibration;
namespace sf = yantra::measure::sample_flag;
using Kind = ym::RadioEvent::Kind;
using std::chrono::milliseconds;


inline const milliseconds kWait{30'000 * YANTRA_TEST_TIME_SCALE};
inline constexpr std::int64_t kSecond = 1'000'000'000;
inline constexpr std::uint64_t kHz = 390'012'500;

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
    std::shared_ptr<yc::ContextPublisher> publisher = std::make_shared<yc::ContextPublisher>();
    std::unique_ptr<yr::RadioHost> host;

    // manual_start: the ManualClock's first time (default: now), e.g. 0
    // for record files whose times start at 0.
    explicit Rig(bool real_clock = false, std::size_t sink_capacity = 4096,
                 std::optional<std::int64_t> manual_start = std::nullopt) {
        ym::MeasurementPipeline::Options o;
        o.catalog = catalog;
        if (!real_clock) {
            manual = std::make_shared<yantra::platform::ManualClock>(
                manual_start.value_or(yantra::platform::monotonic_ns()));
            o.clock = manual;
        }
        pipeline = std::make_shared<ym::MeasurementPipeline>(std::move(o));
        pipeline->add_observer(out);
        yr::RadioHostConfig c;
        c.state = state;
        c.pipeline = pipeline;
        c.context = publisher;
        c.sink_capacity = sink_capacity;
        host = std::make_unique<yr::RadioHost>(std::move(c));
    }
    // ManualClock: emit every cycle that ends by `t`, then stand at `t`
    // (ReplayOptions::advance of a record file replay).
    void advance_to(std::int64_t t) {
        while (pipeline->next_cycle_ns() <= t) {
            manual->set(pipeline->next_cycle_ns());
            pipeline->tick();
        }
        if (t > manual->monotonic_ns()) manual->set(t);
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

inline yr::Plan serving(std::uint64_t hz = kHz) {
    yr::Plan p;
    p.type = YR_PLAN_SERVING;
    p.serving_hz = hz;
    return p;
}

inline std::shared_ptr<yr::RadioDevice> start_fake(Rig& rig) {
    REQUIRE(rig.host->load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    std::string error;
    auto dev = rig.host->open("test.fake", "fake:0", error);
    REQUIRE_MESSAGE(dev, error);
    REQUIRE(dev->start(serving()) == YR_OK);
    return dev;
}



// Records of a device that came out of the pipeline after `from`.
inline std::vector<ym::MappedRecord> records_since(Rig& rig, std::size_t from) {
    const std::lock_guard lock(rig.out->m);
    return {rig.out->records.begin() + static_cast<std::ptrdiff_t>(std::min(from, rig.out->records.size())),
            rig.out->records.end()};
}

inline ycal::Entry flat_entry(const ycal::Key& key, double offset_db, bool simulated = false) {
    ycal::Entry e;
    e.key = key;
    e.created_unix = ycal::now_unix();
    e.simulated = simulated;
    e.method = simulated ? ycal::kMethodPractice : ycal::kMethodGenerator;
    for (double hz : {350e6, 480e6}) {
        ycal::Point p;
        p.frequency_hz = hz;
        p.offset_db = offset_db;
        e.points.push_back(p);
    }
    return e;
}



// The TETRA technology plugin, enabled, in the rig's catalog with its rules.
struct TetraPlugin {
    yp::PluginLoader loader{yp::PluginLoaderConfig{}};
    const yp::PluginInfo* info = nullptr;
    explicit TetraPlugin(Rig& rig) {
        const yp::PluginEntry e = loader.load_file(TETRA_PLUGIN_PATH);
        INFO(e.message);
        REQUIRE(e.status == yp::PluginStatus::Enabled);
        info = loader.info("yantra.tetra");
        REQUIRE(info != nullptr);
        REQUIRE(rig.catalog->add(*info, loader.descriptor("yantra.tetra"), loader.instance("yantra.tetra")));
    }
    // compute_c1 of the plugin on a mapped record.
    std::optional<double> c1(const ym::MappedRecord& r, double level_dbm, double ms_dbm = 30.0) const {
        const ym::RecordView view(r.record, r.schema_index);
        yantra_c1_input in{};
        in.struct_size = sizeof in;
        in.record = view.get();
        in.level_dbm = level_dbm;
        in.ms_power_dbm = ms_dbm;
        double out = 0.0;
        const auto* api = loader.descriptor("yantra.tetra")->technology;
        if (api->compute_c1(loader.instance("yantra.tetra"), &in, &out) != YANTRA_OK) return std::nullopt;
        return out;
    }
};



}  // namespace mh
