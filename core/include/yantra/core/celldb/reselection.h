// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cell reselection simulator (DB-08): given the serving and neighbour
// cells' parameters (database entries or any field maps) and simulated or
// measured levels, the serving-cell state and the reselection that would
// occur. The rule is the technology plugin's (yantra_technology_api
// reselect, compute_c1); the core only puts the question and, for a walk,
// follows the reselections (PLG-01).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/core/celldb/database.h"
#include "yantra/core/celldb/technology.h"
#include "yantra/core/measure/sample.h"

namespace yantra::celldb {

/// The serving MS class power by default (dBm, a portable; plugin.h).
inline constexpr double kDefaultMsPowerDbm = 30.0;
/// The level a cell without a level is given (dBm): far below any C1 > 0.
inline constexpr double kAbsentLevelDbm = -200.0;

struct SimCell {
    std::string id;  ///< for the caller (an entry id); not given to the plugin
    FieldMap fields; ///< its cell-schema fields (parameters)
    double level_dbm = kAbsentLevelDbm;
};

struct ReselectionOutcome {
    /// The plugin answered; false: `error` says why (no reselect rule, it
    /// cannot decide with these parameters, ...).
    bool ok = false;
    std::string error;
    std::int64_t serving_state = 0;
    std::string state;                   ///< the state's machine name, e.g. "improvable"
    std::optional<std::size_t> chosen;   ///< index of the neighbour reselected to
    std::string reason;                  ///< the plugin's line
    std::optional<double> serving_c1;    ///< by compute_c1, when the plugin has it
    std::vector<std::optional<double>> neighbour_c1;
};

/// One question to the plugin.
ReselectionOutcome simulate(const Technology& technology, const SimCell& serving,
                            const std::vector<SimCell>& neighbours, double ms_power_dbm = kDefaultMsPowerDbm);

/// The neighbours of a database entry: its list resolved against the
/// database (network.h resolve; entries named by none or several are left
/// out), in list order, without the entry itself.
std::vector<std::string> neighbour_entries(const CellDatabase& db, std::string_view id);

/// One question with database entries: `serving` and its neighbour_entries,
/// levels by entry id (an entry without a level gets `absent_level_dbm`).
ReselectionOutcome simulate(const CellDatabase& db, const Technologies& technologies, std::string_view serving,
                            const std::map<std::string, double>& levels, double ms_power_dbm = kDefaultMsPowerDbm,
                            double absent_level_dbm = kAbsentLevelDbm);

// ---------------------------------------------------------------------------
// A walk: levels over time, the serving cell following the reselections.

struct WalkStep {
    std::int64_t time_ns = 0;
    std::map<std::string, double> levels;  ///< entry id -> level, dBm
};

struct WalkOptions {
    double ms_power_dbm = kDefaultMsPowerDbm;
    /// A reselection happens when the plugin chose the same neighbour for
    /// this long (0: at once).
    double hold_s = 0.0;
    /// The first serving entry; "": the strongest of the first step.
    std::string start;
    double absent_level_dbm = kAbsentLevelDbm;
};

struct WalkEvent {
    std::int64_t time_ns = 0;
    std::string serving;      ///< serving entry during the step
    ReselectionOutcome outcome;
    std::string chosen;       ///< entry id the plugin chose, "" none
    bool reselected = false;  ///< the serving cell changes to `chosen` after this step
};

std::vector<WalkEvent> simulate_walk(const CellDatabase& db, const Technologies& technologies,
                                     const std::vector<WalkStep>& steps, const WalkOptions& options = {});

/// The levels of measurement cycles by database entry: the fresh samples
/// (records in the cycle, not NO_SYNC, STALE or HELD) of calibrated
/// (dBm) cell-schema records whose cell resolves to one entry by the
/// database's match fields (else the KEY fields) present in the record.
std::vector<WalkStep> walk_steps(const std::vector<measure::MeasurementCycle>& cycles, const CellDatabase& db,
                                 const Technologies& technologies);

}  // namespace yantra::celldb
