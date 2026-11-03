// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TETRA measurement engine (RAD-102, RAD-110..114): wideband IQ of one
// capture (centre, rate) in, measurement records (RAD-21..23, schemas of
// yantra/radios/tetra_schemas.hpp) and stream events (RAD-25) out. Shared
// by the TETRA plugins (phase 5) and the test host's decode-iq (RAD-63).
//
// Per capture: the carrier search (carrier_search.hpp) finds and follows
// the carriers in the span (25 kHz raster, offsets, TETRA bands) with one
// CarrierDecoder each, run continuously; carriers given in the
// configuration are pinned (decoded even when weak, never dropped). Per
// carrier the engine measures:
//   - level: mean power of the 25 kHz channel (Kaiser lowpass 14 kHz on
//     the 72 kHz channel) over the record interval, in dBFS, plus the
//     calibration offset (unit dBm when calibrated);
//   - quality (RAD-113): the decoder's quality window (message erasure
//     rate, CRC per logical channel, BER, MER/EVM, SNR, frequency error);
//   - C1 inputs and broadcast parameters (DEC-03, DEC-10), C1 when the
//     level is calibrated;
//   - the frame number (DEC-11) and channel allocation counts (DEC-06).
// Every record interval (default 1 s, >= 1 Hz) it emits one tetra_cell
// record per carrier, the serving cell's neighbour list as
// tetra_neighbour records (on change, on a serving change and every
// neighbour_interval_s), and with scan_records one tetra_scan record per
// 25 kHz channel of the span plus SCAN_DONE. Events: SYNC_FOUND /
// SYNC_LOST per carrier, SERVING_CHANGED.
//
// Time (RAD-27): records carry the host monotonic time of the middle of
// their interval, from the time base the caller gives with the blocks
// (BlockInfo), and UTC when the caller has a UTC source. No IQ, symbols or
// PDUs are in any record (RAD-26).
//
// Threads: process() is synchronous. With threads > 1 the channelizers and
// decoders of the carriers run on a worker pool, joined before process()
// returns; results do not depend on the thread count.
#pragma once

#include "yantra/receiver/carrier_decoder.hpp"
#include "yantra/receiver/carrier_search.hpp"
#include "yantra/receiver/gsmtap.hpp"

#include <yantra/radios/record_file.hpp>
#include <yantra/radios/tetra_schemas.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace yantra::receiver::tetra {

struct EngineConfig {
    /// Record interval: one tetra_cell record per carrier this often.
    double record_interval_s = 1.0;
    /// Carriers to decode whatever the search finds (CARRIERS plan or a
    /// given serving carrier), Hz.
    std::vector<double> carriers;
    /// The serving carrier when the plan names one (SERVING plan); empty:
    /// the strongest locked carrier, with serving_hysteresis_db.
    std::optional<double> serving_hz;
    double serving_hysteresis_db = 3.0;
    /// Search the span for further carriers (false: the pinned ones only).
    bool search = true;
    /// Emit tetra_scan records of every channel each interval (RAD-23).
    bool scan_records = false;
    /// Neighbour list records at least this often while a serving cell is
    /// known (and whenever the list or the serving cell changes).
    double neighbour_interval_s = 10.0;
    /// Calibration: level = channel power in dBFS + level_offset_db; with
    /// calibrated the unit is dBm (SELF_CALIBRATED) and C1 is given.
    double level_offset_db = 0.0;
    bool calibrated = false;
    /// MS maximum transmit power for C1, dBm.
    double ms_max_power_dbm = radios::kTetraC1MsMaxPowerDbm;
    /// Records of a SIMULATED producer (flag and schemas with sim fields).
    bool simulated = false;
    /// Worker threads for the carriers (1: none, all on the caller).
    unsigned threads = 1;
    Isa isa = Isa::Simd;
    SearchConfig search_config;
    /// GSMTAP copy of the decoded broadcast blocks for Wireshark (RAD-115,
    /// DEC-12; debugging only, off by default, never part of the records).
    GsmtapOptions gsmtap;
};

/// What the caller knows about a block of samples.
struct BlockInfo {
    /// The samples do not follow the previous block (dropped samples,
    /// retune): every carrier starts over.
    bool discontinuity = false;
    /// The front end clipped in this block (integer full scale or ADC
    /// overload): the records of the interval get OVERLOAD.
    bool overload = false;
    /// Host monotonic time of the first sample, ns (< 0: continues from
    /// the previous block at the sample rate).
    int64_t monotonic_ns = -1;
    /// UTC of the first sample and its source (YR_TIME_SOURCE_*); NONE:
    /// keeps the previous UTC base, if any.
    int64_t utc_ns = 0;
    uint32_t utc_source = YR_TIME_SOURCE_NONE;
};

struct EngineOutput {
    std::vector<radios::Record> records;
    std::vector<radios::Event> events;
    void clear()
    {
        records.clear();
        events.clear();
    }
};

/// Load and health (RAD-114).
struct EngineStats {
    uint64_t samples = 0;        ///< capture samples processed
    double signal_s = 0.0;       ///< their duration at the sample rate
    double processing_s = 0.0;   ///< wall-clock time spent in process()
    uint64_t non_finite = 0;     ///< NaN/Inf samples replaced by 0
    uint64_t overload_blocks = 0;
    uint64_t records = 0;
    uint64_t intervals = 0;
    /// processing_s / signal_s: below 1 is faster than real time.
    double load() const { return signal_s > 0.0 ? processing_s / signal_s : 0.0; }
};

/// Index of the engine's schemas in schemas().
inline constexpr uint32_t kEngineCellSchema = 0;
inline constexpr uint32_t kEngineNeighbourSchema = 1;
inline constexpr uint32_t kEngineScanSchema = 2;

class MeasurementEngine {
public:
    /// std::invalid_argument for a bad interval; std::runtime_error when
    /// GSMTAP is enabled with an address that is not numeric.
    explicit MeasurementEngine(EngineConfig config = {});
    ~MeasurementEngine();
    MeasurementEngine(const MeasurementEngine&) = delete;
    MeasurementEngine& operator=(const MeasurementEngine&) = delete;

    /// The record schemas (tetra_cell, tetra_neighbour, tetra_scan with
    /// scan_records), for the plugin descriptor or a record file header.
    const std::vector<radios::SchemaDef>& schemas() const { return schemas_; }
    radios::TetraSchemaOptions schema_options() const;

    /// Feeds `n` capture samples at `centre_hz` / `rate_hz`; appends the
    /// records of every record interval that ended and the events.
    void process(const cf* x, std::size_t n, double centre_hz, double rate_hz, const BlockInfo& info,
                 EngineOutput& out);
    /// Ends the stream: the records of the last, partial interval (if at
    /// least half an interval long).
    void finish(EngineOutput& out);

    /// The serving carrier, Hz.
    std::optional<double> serving_hz() const;
    const CarrierSearch& search() const { return *search_; }
    const EngineConfig& config() const { return cfg_; }
    EngineStats stats() const { return stats_; }
    /// GSMTAP totals of all carriers so far (all zero when it is off).
    GsmtapCounters gsmtap_counters() const;
    /// Datagrams the GSMTAP socket could not send (buffer full, no route).
    uint64_t gsmtap_send_failures() const;

private:
    class Handler;
    class Pool;
    struct Gsmtap;
    void run(const cf* x, std::size_t n, double centre_hz, double rate_hz, bool discontinuity, EngineOutput& out);
    void emit(EngineOutput& out, bool partial);
    void take_events(EngineOutput& out);
    Handler* handler_of(const Carrier& c) const;
    int64_t monotonic_at(double sample) const;
    yr_time time_at(double sample) const;

    EngineConfig cfg_;
    std::vector<radios::SchemaDef> schemas_;
    std::unique_ptr<radios::RecordFiller> cell_;
    std::unique_ptr<radios::RecordFiller> neighbour_;
    std::unique_ptr<radios::RecordFiller> scan_;
    std::shared_ptr<Gsmtap> gsmtap_;
    std::unique_ptr<Pool> pool_;
    std::unique_ptr<CarrierSearch> search_;
    std::vector<CarrierEvent> carrier_events_;
    std::vector<cf> clean_;

    double centre_hz_ = 0.0;
    double rate_hz_ = 0.0;
    uint64_t pos_ = 0;             ///< capture samples since the start
    uint64_t interval_start_ = 0;  ///< capture sample
    uint64_t interval_len_ = 0;    ///< samples per record interval at rate_hz_
    bool interval_overload_ = false;
    double mono_base_ns_ = 0.0;  ///< monotonic ns of capture sample base_sample_
    uint64_t base_sample_ = 0;
    std::optional<double> utc_base_ns_;
    uint32_t utc_source_ = YR_TIME_SOURCE_NONE;

    std::set<uint64_t> synced_ids_;  ///< carriers with SYNC_FOUND sent
    std::optional<uint64_t> serving_id_;
    double serving_hz_value_ = 0.0;
    std::optional<NetworkInfo> last_list_;
    double last_list_s_ = -1e300;
    EngineStats stats_;
};

}  // namespace yantra::receiver::tetra
