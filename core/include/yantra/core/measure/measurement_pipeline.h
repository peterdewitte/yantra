// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The measurement pipeline (OA-13; RF-01, RF-03..05, RF-07 data side,
// RF-08, TST-23, TST-31). Stages, in order (docs/MEASUREMENT_PIPELINE.md):
//  1. input: on_radio_record / on_radio_event from the radio host's
//     delivery thread of each stream (measure/pipeline.h MeasurementInput);
//     the radio host's sink queue in front of it is bounded and drops and
//     counts (RAD-24), so a slow pipeline never blocks a radio;
//  2. schema mapping (PLG-05): the radio record becomes a record of the
//     technology plugin's schema of the same id (TechnologyCatalog), or of
//     the generic schema derived from the radio schema when no technology
//     plugin is loaded (PLG-12 generic views);
//  3. level correction (RF-02, HW-09): LevelCorrector (correction.h:
//     calibration::TableCorrector in the app, IdentityCorrector by
//     default); then C1 (RF-06): a record of a technology plugin's cell
//     schema whose C1 field (the radio API's c1_db) the radio left empty
//     gets the C1 the plugin's compute_c1 gives for the calibrated level
//     and PipelineConfig::ms_power_dbm, flagged kC1Estimated; a radio's C1
//     is kept as delivered; an uncalibrated (dBFS) level gets none;
//  4. checks and per-carrier state: implausible levels are refused,
//     out-of-order and duplicate records ignored, late ones flagged, all
//     counted (TST-23);
//  5. observers: on_record for every accepted record (recording, feature
//     plugins), on_event for stream events;
//  6. the measurement cycle: tick() closes every period that has ended on
//     the clock (default 1 s, at least 1 Hz) and gives the observers one
//     MeasurementCycle with a sample per known carrier and its flags
//     (held, stale, late, device lost, data loss besides the record flags).
//
// Clock (TST-31): the pipeline's platform::Clock (SystemClock by default,
// ManualClock in tests and replay) gives the cycle grid: cycle k ends at
// start + k * period, start = the clock at construction. Record times are
// the radio host's monotonic clock (RAD-27), the same epoch.
//
// Threading: on_radio_record/on_radio_event may come from several delivery
// threads at once; tick() from one thread at a time (CycleRunner or the
// caller). The state lock is held only for short updates; observers are
// called without it (on_record/on_event on the delivery thread, on_cycle
// on the ticking thread) and must not block. Each delivery thread runs as
// a measurement thread (PLG-07, plugin::MeasurementThreadScope).
// The core knows no technology (PLG-01): only the radio API's core fields
// (frequency_hz, role, level; entry_index of neighbour lists) and the
// schema's field categories are read.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "yantra/core/measure/catalog.h"
#include "yantra/core/measure/correction.h"
#include "yantra/core/measure/pipeline.h"
#include "yantra/core/measure/sample.h"
#include "yantra/core/measure/technology_map.h"
#include "yantra/core/platform/clock.h"

namespace yantra::measure {

// Receiver of the pipeline's output. Every method is optional.
class MeasurementObserver {
public:
    virtual ~MeasurementObserver() = default;
    // Each accepted record, on the delivery thread of its stream.
    virtual void on_record(const MappedRecord& record) { (void)record; }
    // Each radio event, on the delivery thread (or the host's thread).
    virtual void on_event(const RadioEvent& event) { (void)event; }
    // Each cycle, in sequence, on the ticking thread.
    virtual void on_cycle(const MeasurementCycle& cycle) { (void)cycle; }
};

struct PipelineConfig {
    std::int64_t period_ns = 1'000'000'000;        // cycle period: <= 1 s (RF-01, TST-31)
    std::int64_t stale_after_ns = 3'000'000'000;   // older values: kStale
    std::int64_t drop_after_ns = 10'000'000'000;   // older carriers leave the cycle
    std::int64_t scan_stale_after_ns = 60'000'000'000;  // scan channels (a pass takes long)
    std::int64_t scan_drop_after_ns = 300'000'000'000;
    std::int64_t late_after_ns = 1'000'000'000;    // arrival - measurement time: kLate
    std::uint32_t max_catch_up = 600;              // cycles emitted by one tick at most
    double min_level = -250.0;                     // plausible levels (dBFS or dBm)
    double max_level = 50.0;
    // MS class power for C1 estimated by Yantra (RF-06): 30 dBm portable
    // (default), 35 dBm mobile; between 10 and 50 dBm.
    double ms_power_dbm = 30.0;
    // "" when valid, else what is wrong.
    std::string validate() const;
};

struct PipelineStats {
    std::uint64_t records = 0;         // handed in
    std::uint64_t accepted = 0;        // mapped and in the carrier state
    std::uint64_t generic = 0;         // of them mapped to the generic schema
    std::uint64_t unmapped = 0;        // no radio schema or no frequency: refused
    std::uint64_t schema_mismatch = 0; // technology schema did not match: generic used
    std::uint64_t implausible = 0;     // level not finite or out of range: refused
    std::uint64_t out_of_order = 0;    // older than the carrier's newest: ignored
    std::uint64_t duplicates = 0;      // same time and values as the newest: ignored
    std::uint64_t late = 0;            // accepted with kLate
    std::uint64_t events = 0;
    std::uint64_t cycles = 0;
    std::uint64_t carrier_samples = 0;
    std::uint64_t held = 0;            // samples with kHeld
    std::uint64_t stale = 0;           // samples with kStale
    std::uint64_t expired = 0;         // carriers that left after drop_after
    std::uint64_t skipped_cycles = 0;  // beyond max_catch_up in one tick
    std::uint64_t c1_radio = 0;        // accepted records with the radio's C1 (RF-06)
    std::uint64_t c1_estimated = 0;    // accepted records with C1 estimated by Yantra
    std::uint64_t c1_failed = 0;       // compute_c1 answered an error (not UNSUPPORTED)
};

class MeasurementPipeline final : public MeasurementInput {
public:
    struct Options {
        PipelineConfig config;
        std::shared_ptr<TechnologyCatalog> catalog;   // nullptr: every record generic
        std::shared_ptr<const LevelCorrector> corrector;  // nullptr: IdentityCorrector
        std::shared_ptr<const platform::Clock> clock;     // nullptr: SystemClock
        // Problems worth a log line (schema mismatch), once each. Any thread.
        std::function<void(const std::string& message)> log;
    };
    explicit MeasurementPipeline(Options options);
    ~MeasurementPipeline() override;
    MeasurementPipeline(const MeasurementPipeline&) = delete;
    MeasurementPipeline& operator=(const MeasurementPipeline&) = delete;

    // MeasurementInput (radio host delivery threads).
    void on_radio_record(const RadioRecord& record) override;
    void on_radio_event(const RadioEvent& event) override;

    // Emits every cycle whose period has ended on the clock; returns how
    // many. One thread at a time.
    std::size_t tick();
    // Monotonic time the next cycle ends.
    std::int64_t next_cycle_ns() const;
    const PipelineConfig& config() const noexcept { return config_; }
    const platform::Clock& clock() const noexcept { return *clock_; }

    // A new level correction (6.2: calibration table or antenna setting
    // changed); later records use it.
    void set_corrector(std::shared_ptr<const LevelCorrector> corrector);

    // Observers are kept until removed; a call already running may still
    // finish after remove_observer returns.
    void add_observer(std::shared_ptr<MeasurementObserver> observer);
    void remove_observer(const std::shared_ptr<MeasurementObserver>& observer);

    PipelineStats stats() const;
    // Waits until `pred(stats())` holds (tests: an observable condition).
    bool wait_stats(const std::function<bool(const PipelineStats&)>& pred,
                    std::chrono::milliseconds timeout) const;

private:
    // Per (radio plugin, radio schema): the mapping to its technology schema.
    struct SchemaEntry {
        const yr_schema* radio = nullptr;
        std::uint64_t generation = 0;
        std::shared_ptr<const plugin::Schema> schema;
        std::shared_ptr<const SchemaMap> map;
        plugin::Provenance provenance;
        std::uint32_t index = 0;
        bool generic = false;
        std::uint32_t kind = 0;
        // Radio field indexes of the core fields (by name), -1 when absent.
        int frequency = -1, role = -1, level = -1, entry_index = -1;
        bool radio_dbfs = false;
        int tech_level = -1;  // technology field of the level
        int tech_c1 = -1;     // technology field c1_db (DOUBLE) of a cell schema, -1 none
        std::shared_ptr<const TechnologyRules> rules;  // with compute_c1, for tech_c1
        std::vector<std::size_t> quality;  // technology QUALITY fields
    };
    using CarrierKey = std::tuple<std::string, std::string, std::string, std::uint64_t, std::uint64_t>;
    using DeviceKey = std::pair<std::string, std::string>;
    struct CarrierState {
        MappedRecord latest;
        std::uint32_t fresh = 0;
        bool late = false;
        bool sync_lost = false;
    };
    struct DeviceState {
        bool lost = false;
        bool stopped = false;
        bool data_loss = false;
    };

    std::shared_ptr<const SchemaEntry> schema_entry(const RadioRecord& record);
    void build_cycle(std::int64_t end_ns, std::int64_t now_ns, MeasurementCycle& out);
    std::vector<std::shared_ptr<MeasurementObserver>> observers() const;
    void bump();

    const PipelineConfig config_;
    const std::shared_ptr<TechnologyCatalog> catalog_;
    const std::shared_ptr<const platform::Clock> clock_;
    const std::function<void(const std::string&)> log_;

    mutable std::mutex corrector_mutex_;
    std::shared_ptr<const LevelCorrector> corrector_;

    mutable std::mutex observer_mutex_;
    std::vector<std::shared_ptr<MeasurementObserver>> observers_;

    mutable std::mutex schema_mutex_;
    std::map<std::pair<std::string, std::uint32_t>, std::shared_ptr<const SchemaEntry>> schemas_;
    std::set<std::string> logged_;

    std::mutex tick_mutex_;
    mutable std::mutex mutex_;  // state and stats
    mutable std::condition_variable stats_cv_;
    std::map<CarrierKey, CarrierState> carriers_;
    std::map<DeviceKey, DeviceState> devices_;
    std::int64_t next_end_ns_ = 0;
    std::uint64_t sequence_ = 0;
    PipelineStats stats_;
};

// Drives a pipeline on the real clock: a thread that calls tick() at each
// period end until destroyed. With a ManualClock call tick() yourself.
class CycleRunner {
public:
    explicit CycleRunner(MeasurementPipeline& pipeline);
    ~CycleRunner();
    CycleRunner(const CycleRunner&) = delete;
    CycleRunner& operator=(const CycleRunner&) = delete;

private:
    MeasurementPipeline& pipeline_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace yantra::measure
