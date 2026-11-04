// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the measurement pipeline (OA-13, measurement_pipeline.h) produces:
// mapped records (one per accepted radio record, in the technology
// plugin's schema, calibrated) and measurement cycles (one per period, at
// least 1 Hz, TST-31) with one carrier sample per carrier the pipeline
// knows: the serving/selected carrier (RF-01), neighbours (RF-03), scan
// channels (RF-04), with their quality values (RF-05, RF-07) and validity
// flags (RF-08). Every view, recording, export and report keeps the flags
// (RF-08) and the provenance (PLG-12). Technology-neutral (PLG-01): the
// roles and core fields are those of the radio API (RAD-21..23), all other
// meaning comes from the technology schema.
#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "yantra/core/measure/correction.h"
#include "yantra/core/plugin/record.h"
#include "yantra/plugin.h"

namespace yantra::measure {

// Validity flags of a sample (RF-08). The low bits are the record flags of
// the radio API and plugin API (RAD-21; YANTRA_RECORD_*), with the same
// values; the cycle adds the high bits. A flagged value is kept and shown;
// the flags say how far it can be trusted.
namespace sample_flag {
inline constexpr std::uint32_t kNoSync = YANTRA_RECORD_NO_SYNC;            // level only, no identity; or SYNC_LOST since the record
inline constexpr std::uint32_t kOverload = YANTRA_RECORD_OVERLOAD;         // clipping or saturation
inline constexpr std::uint32_t kUncalibrated = YANTRA_RECORD_UNCALIBRATED; // level not calibrated to dBm (HW-01b)
inline constexpr std::uint32_t kSimulated = YANTRA_RECORD_SIMULATED;       // simulated data (DEMO-07)
inline constexpr std::uint32_t kRecordMask = YANTRA_RECORD_ALL_MASK;
// No new record for the carrier in this cycle: the last value is repeated.
inline constexpr std::uint32_t kHeld = 1u << 8;
// The value is older than the stale limit (PipelineConfig) at the cycle time.
inline constexpr std::uint32_t kStale = 1u << 9;
// The record reached the pipeline later than the late limit after it was measured.
inline constexpr std::uint32_t kLate = 1u << 10;
// The device was unplugged and has not come back (NFR-04).
inline constexpr std::uint32_t kDeviceLost = 1u << 11;
// The radio reported dropped records or sample overflows in this cycle (RAD-24, RAD-25).
inline constexpr std::uint32_t kDataLoss = 1u << 12;
inline constexpr std::uint32_t kCycleMask = kHeld | kStale | kLate | kDeviceLost | kDataLoss;
inline constexpr std::uint32_t kAllMask = kRecordMask | kCycleMask;
}  // namespace sample_flag

// "no_sync,overload,uncalibrated,simulated,held,stale,late,device_lost,
// data_loss" for the bits set, in bit order; "" for none.
std::string sample_flags_text(std::uint32_t flags);

// The part a record plays, from the radio API's schema kind and core role.
enum class CarrierRole {
    Serving,        // MEASUREMENT, role SERVING: the serving/selected carrier (RF-01)
    Neighbour,      // MEASUREMENT, role NEIGHBOUR: a neighbour's measured level (RF-03)
    Other,          // MEASUREMENT, role OTHER (carrier list)
    ScanChannel,    // SCAN: one channel of a band scan (RF-04)
    NeighbourEntry, // NEIGHBOUR: one entry of the serving cell's neighbour list (RF-03)
};
const char* role_text(CarrierRole role) noexcept;

// One quality value of a record: a QUALITY field of the technology schema
// that is present (RF-05; modulation quality such as EVM/MER or frequency
// error, RF-07, when the radio delivers it).
struct QualityValue {
    std::string field;
    std::string unit;
    double value = 0.0;
};

// One accepted radio record after schema mapping (PLG-05) and level
// correction (RF-02, HW-09).
struct MappedRecord {
    // Provenance (PLG-12): technology id, technology plugin id and version
    // ("" for the generic schema), schema id and version.
    plugin::Provenance provenance;
    bool generic = false;  // no technology plugin schema: derived from the radio schema
    std::string radio_plugin_id;
    std::string radio_plugin_version;
    std::string device_key;
    std::uint64_t stream_id = 0;
    std::shared_ptr<const plugin::Schema> schema;  // technology schema of `record`
    std::uint32_t schema_index = 0;                // in the technology plugin's list
    plugin::Record record;                         // values in `schema` order, calibrated

    CarrierRole role = CarrierRole::Other;
    std::uint64_t frequency_hz = 0;
    std::uint64_t entry_index = 0;     // NeighbourEntry: entry_index of the list
    double level = std::nan("");       // calibrated level, dBm (or dBFS when uncalibrated); NaN none
    double radio_level = std::nan(""); // the level as the radio delivered it
    bool radio_dbfs = false;           // radio_level is in dBFS
    LevelCorrection correction;        // what was applied
    std::vector<QualityValue> quality;
    std::uint32_t flags = 0;           // sample_flag record bits (+ kSimulated for SIMULATED radios)
    std::int64_t arrival_ns = 0;       // pipeline clock when it arrived
};

// One carrier in a cycle.
struct CarrierSample {
    MappedRecord latest;              // the newest record of the carrier
    std::int64_t age_ns = 0;          // cycle time - latest.record.time.monotonic_ns (>= 0)
    std::uint32_t records_in_cycle = 0;  // records accepted for it since the previous cycle
    std::uint32_t flags = 0;          // sample_flag bits
};

// One measurement cycle (OA-13, TST-31): `time` is the end of the period
// (monotonic grid time; UTC from the clock), samples sorted by radio
// plugin, device, schema, frequency, entry. Cycles come in sequence
// without gaps (skipped ones are counted, PipelineStats::skipped_cycles).
struct MeasurementCycle {
    std::uint64_t sequence = 0;
    yantra_time time{};
    std::int64_t period_ns = 0;
    std::uint32_t flags = 0;  // union of the samples' flags
    std::vector<CarrierSample> samples;

    // The serving carrier of a device (RF-01); nullptr when none. With an
    // empty key: the first serving carrier of any device.
    const CarrierSample* serving(const std::string& device_key = {}) const noexcept;
    // Every sample of one role (RF-03 neighbours, RF-04 scan channels).
    std::vector<const CarrierSample*> with_role(CarrierRole role) const;
};

}  // namespace yantra::measure
