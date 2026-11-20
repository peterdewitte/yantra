// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared glue of the TETRA radio plugins (the simulator RAD-61, and the
// SoapySDR and HackRF plugins RAD-60/62): everything between the
// measurement engine (yantra/receiver/measurement_engine.hpp) and the C
// radio API that does not depend on where the samples come from.
//   - TetraSchemaTable: the engine's record schemas as yr_schema for the
//     descriptor (RAD-12), in static storage;
//   - plan_setup / apply_plan: a yr_plan checked against the device's
//     span and turned into the engine's carriers, serving carrier and
//     search (RAD-32);
//   - push_output: engine records and events to the host sink, never
//     waiting (RAD-20, RAD-24, RAD-25);
//   - spectra: the plan's spectrum band within the device's range, bin
//     widths and the spectrum sink (radio API 1.2, RAD-140);
//   - set_c1: C1 (EN 300 392-2 clause 23.3, RAD-22) from a record's own
//     level and broadcast fields when the plugin knows the level in dBm
//     (the simulator; a calibrated radio);
//   - settings as data: setting/sensor builders, lookup, raster rounding,
//     string values (RAD-40..45), self-test items (RAD-46);
//   - the GSMTAP receiver settings (RAD-115, DEC-12).
// Namespace yantra::plugins. Nothing here crosses the C boundary with a
// C++ exception: the plugins wrap every entry in YANTRA_PLUGIN_GUARD.
#pragma once

#include <yantra/radio.h>
#include <yantra/radio.hpp>
#include <yantra/radios/record_file.hpp>
#include <yantra/radios/tetra_schemas.hpp>
#include <yantra/receiver/measurement_engine.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

/// Body of a C entry point that returns a yr_status: no C++ exception
/// crosses the boundary (RAD-10).
#define YANTRA_PLUGIN_GUARD(...)                                                              \
    try {                                                                                     \
        __VA_ARGS__                                                                           \
    } catch (const std::bad_alloc&) {                                                         \
        return YR_ERR_NO_MEMORY;                                                              \
    } catch (...) {                                                                           \
        return YR_ERR_FAILED;                                                                 \
    }

namespace yantra::plugins {

// --- schemas (RAD-12) ---------------------------------------------------------------

/// The TETRA schemas (tetra_cell, tetra_neighbour[, tetra_scan]) as C
/// structs for a descriptor. Keep one static instance per plugin: the
/// descriptor points into it until the library is unloaded.
class TetraSchemaTable {
public:
    explicit TetraSchemaTable(const radios::TetraSchemaOptions& options);
    TetraSchemaTable(const TetraSchemaTable&) = delete;
    TetraSchemaTable& operator=(const TetraSchemaTable&) = delete;
    const yr_schema* const* schemas() const { return ptrs_.data(); }
    uint32_t count() const { return static_cast<uint32_t>(ptrs_.size()); }
    const std::vector<radios::SchemaDef>& defs() const { return defs_; }
    const radios::TetraSchemaOptions& options() const { return options_; }

private:
    radios::TetraSchemaOptions options_;
    std::vector<radios::SchemaDef> defs_;
    std::vector<std::unique_ptr<radios::SchemaView>> views_;
    std::vector<const yr_schema*> ptrs_;
};

// --- plans (RAD-32) -----------------------------------------------------------------

/// What a plan asks the engine to do.
struct PlanSetup {
    uint32_t type = YR_PLAN_SERVING;
    std::vector<double> carriers;     ///< pinned carriers, Hz
    std::optional<double> serving_hz; ///< SERVING with a given carrier
    bool search = true;               ///< search the span for more carriers
};

/// Checks `plan` (the carrier list given separately, as the plan's pointer
/// is valid during the call only) against the frequencies the device can
/// measure now, [first_hz, last_hz], and at most `max_carriers` carriers.
/// SCAN plans are UNSUPPORTED unless `scan` is set. YR_OK, or the status
/// with a one-line reason in `why`.
yr_status plan_setup(const yr_plan& plan, const std::vector<uint64_t>& carriers, double first_hz,
                     double last_hz, uint32_t max_carriers, bool scan, PlanSetup& out,
                     std::string& why);

/// Copies the plan into the engine configuration.
void apply_plan(const PlanSetup& plan, receiver::tetra::EngineConfig& config);

// --- records to the host (RAD-20, RAD-24, RAD-25) ----------------------------------

/// Stream counters a plugin reports in stream_stats (RAD-24).
struct SinkCounters {
    std::atomic<uint64_t> delivered{0}; ///< push_record returned OK
    std::atomic<uint64_t> refused{0};   ///< push_record refused it (a plugin bug or BUSY)
    void reset()
    {
        delivered = 0;
        refused = 0;
    }
};

/// Pushes one record (RecordView) to `sink`; true when the host took it
/// (it may still drop and count it, RAD-24).
bool push_record(const yr_host_api& host, yr_stream* sink, const radios::Record& record,
                 SinkCounters& counters);
/// Pushes the records of `out` in order, then its events.
void push_output(const yr_host_api& host, yr_stream* sink,
                 const receiver::tetra::EngineOutput& out, SinkCounters& counters);

// --- spectra (radio API 1.2, RAD-140) ----------------------------------------------

/// What a stream delivers as spectra.
struct SpectrumPlan {
    bool on = false;       ///< the plan asks (YR_PLAN_SPECTRUM) and the host has a sink
    double first_hz = 0.0; ///< the band asked for (default 380-400 MHz)
    double last_hz = 0.0;
};
/// The spectrum part of a plan kept with yr_plan_read.
SpectrumPlan spectrum_plan(const yr_host_api& host, const yr_plan& plan);
/// The band [first, last] clipped to what the device measures, [lo, hi];
/// when they do not overlap, [lo, hi] itself. false when lo >= hi.
bool clip_band(double first, double last, double lo, double hi, double& out_first,
               double& out_last);
/// The finest bin width 25 kHz * 2^k (k >= -6) that covers `span_hz` in
/// at most YR_SPECTRUM_MAX_BINS bins.
double spectrum_bin_width(double span_hz);

/// One spectrum to push (power per bin over [start_hz, start_hz + n * width)).
struct SpectrumOut {
    double start_hz = 0.0;
    double bin_width_hz = 0.0;
    std::vector<double> power_db;
    yr_time time{};
    uint32_t flags = 0; ///< YR_RECORD_* within YR_SPECTRUM_FLAGS_MASK
};
/// Hands one spectrum to the host's spectrum sink, never waiting (RAD-24);
/// true when the host took it. No sink, no bins: false.
bool push_spectrum(const yr_host_api& host, yr_stream* sink, const SpectrumOut& s);

// --- C1 (RAD-22) --------------------------------------------------------------------

/// Sets c1_db of a tetra_cell record from its own level (plus
/// `level_to_dbm_db`, the true calibration: dBm = level + this) and its
/// broadcast fields rxlev_access_min_dbm and ms_txpwr_max_cell_dbm, for an
/// MS of `ms_max_power_dbm`. False (record unchanged) when the record has
/// no broadcast parameters or no level.
bool set_c1(radios::Record& record, const radios::RecordFiller& cell, double level_to_dbm_db,
            double ms_max_power_dbm);

/// The C1 terminal-class setting of the TETRA plugins (key
/// kMsMaxPowerKey): MS maximum power, 15..45 dBm in 0.25 dB steps (the
/// power classes of EN 300 392-2 clause 6), default 30 dBm
/// (radios::kTetraC1MsMaxPowerDbm, class 4, a typical hand-portable).
inline constexpr const char* kMsMaxPowerKey = "ms_max_power_dbm";
yr_setting_def ms_max_power_setting(const char* group, int32_t order);

// --- settings as data (RAD-40..46) --------------------------------------------------

/// A setting definition with struct_size, texts and order set, numbers
/// open (NaN), no flags and no default.
yr_setting_def make_setting(const char* key, uint32_t type, const char* label, const char* unit,
                            const char* group, const char* help, int32_t order);
/// A read-only sensor definition (RAD-45).
yr_setting_def make_sensor(const char* key, uint32_t type, const char* label, const char* unit,
                           const char* group, const char* help, int32_t order);
/// The definition with `key` in `defs`, or nullptr.
const yr_setting_def* find_setting(const std::vector<yr_setting_def>& defs, const char* key);
/// A numeric value of a setting as double (INT, UINT, DOUBLE).
double number_of(const yr_value& v);
/// `x` rounded to the setting's raster (minimum + k * step) and clamped
/// to its range (the value the device really uses, RAD-41).
double on_raster(const yr_setting_def& def, double x);
/// A STRING value for get_setting/read_sensor: copies `text` into the
/// caller's buffer and points `out` at it (BUFFER_TOO_SMALL when it does
/// not fit).
yr_status string_value(const std::string& text, yr_value* out, char* buffer, uint32_t size);
/// A self-test item (RAD-46).
yr_selftest_item selftest_item(uint32_t result, const char* name, const char* label,
                               const char* detail, const char* remedy);

// --- GSMTAP (RAD-115, DEC-12) -------------------------------------------------------

/// The GSMTAP receiver settings (keys receiver::tetra::kGsmtap*Key):
/// off by default; ADVANCED and RESTART.
struct GsmtapSettings {
    bool enabled = false;
    std::string host = "127.0.0.1";
    uint64_t port = receiver::tetra::kGsmtapDefaultPort;
    bool operator==(const GsmtapSettings&) const = default;
};
/// Appends the three definitions (orders first_order..first_order + 2).
void add_gsmtap_settings(std::vector<yr_setting_def>& defs, const char* group,
                         int32_t first_order);
/// get_setting of a GSMTAP key: nullopt when `key` is not one.
std::optional<yr_status> gsmtap_get(const GsmtapSettings& s, const char* key, yr_value* out,
                                    char* text, uint32_t text_size);
/// set_setting of a GSMTAP key (value already checked against its
/// definition): false when `key` is not one.
bool gsmtap_set(GsmtapSettings& s, const char* key, const yr_value& value);
/// The engine options; a host that is not a numeric address turns the
/// output off with a reason in `warning`.
receiver::tetra::GsmtapOptions gsmtap_options(const GsmtapSettings& s, std::string& warning);

} // namespace yantra::plugins
