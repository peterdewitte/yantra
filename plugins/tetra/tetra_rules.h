// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA technology rules (PLG-02): path loss criterion C1/C2 (RF-06), cell
// reselection (DB-08) and parameter verification (DB-04), as pure
// functions without state, allocation or blocking (PLG-07). The plugin
// glue (tetra_technology.cpp) reads their inputs from the records of the
// technology schemas; the meaning of the parameters is EN 300 392-2
// (docs/plugins/TETRA.md).
//
// C1 (EN 300 392-2 clause 23; the formula of yantra-radios'
// tetra_c1_db, common/include/yantra/radios/tetra_schemas.hpp at 205ca7a,
// with the same operations in the same order, so Yantra's estimate and a
// radio's C1 are the same number for the same inputs):
//
//   C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS_MAX)
//
// all in dBm / dB; MS_MAX is the MS class power (RF-06: 30 dBm portable,
// 35 dBm mobile). C2 of a neighbour is the same formula with the
// neighbour's level and access parameters.
//
// Cell reselection: our formulation of the re-selection criteria of
// EN 300 392-2 (clause 18.3.4) for static levels (the 5 s time
// conditions are not simulated). Copied from yantra-v2 68ea20a:
// core/src/celldb/reselection.cpp (simulate_reselection), adapted: in the
// technology plugin instead of the core (PLG-01), without allocation, and
// "no thresholds" is not a state (the caller answers UNSUPPORTED).
// Checked top to bottom (thresholds and hystereses in dB):
//
//   C1 < 0                    radio link failure: reselect to the best
//                             neighbour with C2 > 0
//   no neighbour with C2 > 0  exclusive (no other cell is suitable)
//   C1 >= every C2            superior (no neighbour is better)
//   C1 < FRT                  relinquishable: fast reselection to the best
//                             neighbour if its C2 > C1 + FRH
//   C1 < SRT                  improvable: slow reselection to the best
//                             neighbour if its C2 > C1 + SRH
//   otherwise                 usable (a neighbour is better, but the
//                             serving cell is good enough to keep)
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace yantra::tetra {

// MS class power of RF-06: portable (power class 4, 1 W) and mobile.
inline constexpr double kPortableMsPowerDbm = 30.0;
inline constexpr double kMobileMsPowerDbm = 35.0;

// The access parameters of a cell (MAC-SYSINFO; for a neighbour also its
// D-NWRK-BROADCAST entry), dBm.
struct AccessParameters {
    double rxlev_access_min_dbm = 0.0;
    double ms_txpwr_max_cell_dbm = 0.0;
};

// C1 (or C2) in dB.
inline double c1_db(double level_dbm, const AccessParameters& a, double ms_power_dbm) noexcept
{
    const double excess = a.ms_txpwr_max_cell_dbm - ms_power_dbm;
    return level_dbm - a.rxlev_access_min_dbm - (excess > 0.0 ? excess : 0.0);
}

// Cell re-select parameters of the serving cell's D-NWRK-BROADCAST, dB.
struct ReselectionThresholds {
    double srt_db = 0.0;  // SLOW_RESELECT_THRESHOLD
    double srh_db = 0.0;  // SLOW_RESELECT_HYSTERESIS
    double frt_db = 0.0;  // FAST_RESELECT_THRESHOLD
    double frh_db = 0.0;  // FAST_RESELECT_HYSTERESIS
};

// Serving-cell states (DB-08); the values are the plugin's serving_states.
enum class ServingState : std::int64_t {
    Exclusive = 0,
    Superior = 1,
    Usable = 2,
    Improvable = 3,
    Relinquishable = 4,
    RadioLinkFailure = 5,
};
// Machine name of a state ("exclusive", ..., "radio_link_failure").
const char* serving_state_name(ServingState s) noexcept;

enum class Reselection { None, Slow, Fast, LinkFailure };
const char* reselection_name(Reselection r) noexcept;  // "none", "slow", "fast", "link_failure"

// C2 of neighbour i (empty when its access parameters are not known).
using NeighbourC2 = std::optional<double> (*)(const void* context, std::size_t i);

inline constexpr std::size_t kReasonBytes = 256;

struct ReselectionResult {
    ServingState state = ServingState::Usable;
    Reselection reselection = Reselection::None;
    int target = -1;          // neighbour reselected to, -1 none
    double serving_c1 = 0.0;
    int best = -1;            // neighbour with the highest C2 > 0, -1 none
    char reason[kReasonBytes] = {};  // one line, e.g. "C1 5.0 dB < FRT 10.0 dB; ..."
};

// The serving-cell state and the reselection that would occur. Empty
// when the serving cell's C1 is not known (no access parameters) or a
// neighbour is better and the thresholds are not known.
std::optional<ReselectionResult> reselect(double serving_c1_db,
                                          const std::optional<ReselectionThresholds>& thresholds,
                                          std::size_t neighbour_count, NeighbourC2 c2,
                                          const void* context) noexcept;

// A dBm parameter broadcast as a code: value = base + step x code, codes
// first..last (MAC-SYSINFO).
struct CodeGrid {
    std::int64_t base;
    std::int64_t step;
    std::int64_t first_code;
    std::int64_t last_code;
};
inline constexpr CodeGrid kRxlevAccessMinGrid{-125, 5, 0, 15};  // -125..-50 dBm
inline constexpr CodeGrid kMsTxpwrMaxCellGrid{10, 5, 1, 7};     // 15..45 dBm (code 1 = 15)
inline constexpr CodeGrid kAccessParameterGrid{-53, 2, 0, 15};  // -53..-23 dBm

// The broadcast value a stored value stands for: the grid value nearest to
// it (ties to the higher code), clamped to the code range.
std::int64_t snap_to_grid(std::int64_t value, const CodeGrid& grid) noexcept;
// Parameter verification (DB-04) of a dBm parameter broadcast as a code:
// a received value matches a stored one when the stored value snaps to it
// (a database entry such as -113 dBm stands for the broadcast -115 dBm).
inline bool grid_values_match(std::int64_t received, std::int64_t stored, const CodeGrid& grid) noexcept
{
    return received == snap_to_grid(stored, grid);
}

}  // namespace yantra::tetra
