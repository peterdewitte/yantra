// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TETRA record schemas (RAD-12, RAD-21..23), shared by everything in
// yantra-radios that produces TETRA records, so records of different
// producers compare field by field:
//   - the measurement engine (receiver/, used by the TETRA plugins and the
//     test host's decode-iq, RAD-63);
//   - yantra-gen's ground-truth records (RAD-71);
//   - the simulator plugin (RAD-61).
// Three schemas:
//   "tetra_cell"      MEASUREMENT: one per measured carrier and record
//                     interval (RAD-21) with the optional groups quality
//                     (RAD-113), C1, frame number (DEC-11), broadcast
//                     parameters (DEC-03, DEC-10), sim_position (RAD-19)
//                     and other values;
//   "tetra_neighbour" NEIGHBOUR: one per entry of a serving cell's
//                     D-NWRK-BROADCAST neighbour list (DEC-02);
//   "tetra_scan"      SCAN: one per 25 kHz channel of a band scan (RAD-23).
// The field lists depend on two options only: the level unit (dBFS, or
// dBm for a SELF_CALIBRATED producer) and `simulated` (adds the
// sim_position group and the generator's values: scenario cell id,
// instantaneous level, level source, strongest antenna, walls). Field
// names, types, units and groups never depend on the producer; a producer
// leaves out (absent) what it does not know. docs/TETRA_RECORDS.md lists
// every field.
#pragma once

#include "yantra/radios/record_file.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::radios {

inline constexpr const char* kTetraCellSchema = "tetra_cell";
inline constexpr const char* kTetraNeighbourSchema = "tetra_neighbour";
inline constexpr const char* kTetraScanSchema = "tetra_scan";
/// Schema version of all three (RAD-65 versioning).
inline constexpr uint32_t kTetraSchemaVersion = 1;

struct TetraSchemaOptions {
    bool level_dbm = false;  ///< level unit "dBm" (SELF_CALIBRATED), else "dBFS"
    bool simulated = false;  ///< sim_position and generator values (SIMULATED producers)
    bool scan = false;       ///< include "tetra_scan" (SCAN capability)
};

/// "tetra_cell" (MEASUREMENT).
SchemaDef tetra_cell_schema(const TetraSchemaOptions& options);
/// "tetra_neighbour" (NEIGHBOUR).
SchemaDef tetra_neighbour_schema(const TetraSchemaOptions& options);
/// "tetra_scan" (SCAN): the core fields.
SchemaDef tetra_scan_schema(const TetraSchemaOptions& options);
/// Cell, neighbour and (with options.scan) scan schema, in that order.
std::vector<SchemaDef> tetra_schemas(const TetraSchemaOptions& options);

/// Logical channels with a slot CRC error rate field
/// "crc_error_pct_<name>" (RAD-113), in field order.
inline constexpr std::string_view kTetraCrcChannels[] = {"bsch", "bnch", "sch_f", "sch_hd", "stch", "aach"};

/// MS maximum transmit power assumed for C1 when the producer has no
/// terminal class: 30 dBm (power class 4, 1 W, a typical hand-portable).
inline constexpr double kTetraC1MsMaxPowerDbm = 30.0;
/// Path loss criterion C1 (EN 300 392-2 clause 23.3):
/// C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS_MAX), all
/// in dBm / dB.
inline double tetra_c1_db(double level_dbm, double rxlev_access_min_dbm, double ms_txpwr_max_cell_dbm,
                          double ms_max_power_dbm = kTetraC1MsMaxPowerDbm)
{
    const double excess = ms_txpwr_max_cell_dbm - ms_max_power_dbm;
    return level_dbm - rxlev_access_min_dbm - (excess > 0.0 ? excess : 0.0);
}

/// \name Typed values (absent for non-finite doubles)
/// @{
Value value_uint(uint64_t x);
Value value_int(int64_t x);
Value value_double(double x);
Value value_bool(bool x);
Value value_enum(int64_t x);
Value value_string(std::string s);
/// @}

/// Fills a record of one schema by field name.
class RecordFiller {
public:
    RecordFiller(const SchemaDef& schema, uint32_t schema_index);
    /// Starts a record: all values absent, no group present.
    Record start(const yr_time& time, uint32_t flags) const;
    /// Sets field `name`; marks its optional group present. Throws
    /// std::invalid_argument for an unknown name (a programming error).
    void set(Record& r, std::string_view name, Value v) const;
    /// Marks a group present without setting a value (a group whose
    /// values are all unknown in this record).
    static void present(Record& r, uint32_t group) { r.present |= group; }
    bool has(std::string_view name) const { return schema_.field_index(name) >= 0; }
    const SchemaDef& schema() const { return schema_; }

private:
    SchemaDef schema_;
    uint32_t index_;
};

}  // namespace yantra::radios
