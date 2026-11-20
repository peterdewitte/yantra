// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ground truth of a scenario run as radio records (RAD-65, RAD-71): what
// an ideal, calibrated TETRA radio plugin would deliver for the scenario's
// cells at each truth time step. yantra-gen writes them to a record file
// next to its IQ file; Yantra's tests feed such files into its record sink
// (Yantra TST-10). The simulator plugin (RAD-61) can use the same schemas.
//
// The schemas are the shared TETRA schemas of the radio parts
// (yantra/radios/tetra_schemas.hpp; level in dBm, simulated), so ground
// truth and the measurement engine's records compare field by field:
// - "tetra_cell" (MEASUREMENT): the 8 core fields (level in dBm: the local
//   mean level at the antenna connector), quality (snr_db, freq_error_hz,
//   freq_error_ppm), C1 (with kTetraC1MsMaxPowerDbm), broadcast
//   (encryption, MS_TXPWR_MAX_CELL and RXLEV_ACCESS_MIN as codes and dBm,
//   main carrier, band), sim_position (receiver floor and position) and
//   other (scenario cell id, instantaneous level, level source, strongest
//   antenna, walls). One record per on-air cell per truth row; role
//   SERVING for the strongest on-air cell, NEIGHBOUR for the cells in its
//   broadcast list, OTHER for the rest.
// - "tetra_neighbour" (NEIGHBOUR): the serving cell's broadcast neighbour
//   list, one record per entry, as broadcast (planted values included):
//   at the first row, whenever the serving cell changes and every second.
#pragma once

#include "scenario.hpp"
#include "scenario_engine.hpp"

#include <yantra/radios/record_file.hpp>
#include <yantra/radios/tetra_schemas.hpp>

#include <optional>
#include <string>
#include <vector>

namespace yantra::gen {

inline constexpr const char* kTruthCellSchema = radios::kTetraCellSchema;
inline constexpr const char* kTruthNeighbourSchema = radios::kTetraNeighbourSchema;
/// Interval of the neighbour-list records while the serving cell stays.
inline constexpr double kTruthNeighbourIntervalS = 1.0;

/// The two schemas, cell schema first.
std::vector<radios::SchemaDef> truth_schemas();

/// Header of a ground-truth record file: plugin "yantra-gen" (technology
/// "tetra", capabilities SIMULATED and SELF_CALIBRATED), a CARRIERS plan
/// with every cell's frequency, the two schemas. `producer` e.g.
/// "yantra-gen 0.1.0"; created_utc_ns 0 (deterministic files).
radios::RecordFileHeader truth_record_header(const Scenario& s, const std::string& version);

/// What one truth row turns into.
struct TruthRecords {
    std::vector<radios::Record> records;
    /// Set when the serving cell changed at this row (a SERVING_CHANGED
    /// event line goes before the records): the new serving frequency.
    std::optional<uint64_t> serving_changed_hz;
};

/// Converts truth rows to records, keeping the serving cell between rows.
class TruthRecorder {
public:
    explicit TruthRecorder(const Scenario& s);
    TruthRecords convert(const TruthRow& row);
    /// Monotonic time of a truth row: its signal time in ns.
    static int64_t monotonic_ns(const TruthRow& row);

private:
    const Scenario& s_;
    radios::RecordFiller cell_;
    radios::RecordFiller neighbour_;
    std::optional<size_t> serving_;  ///< index in s_.cells
    double last_list_t_ = -1e300;
};

}  // namespace yantra::gen
