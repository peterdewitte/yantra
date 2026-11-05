// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Post-processing views of a floor's points (PP-02, PP-03, PP-04;
// docs/SURVEY.md "Post-processing"). Every view is computed from the
// shared point list (points.h, IND-30) and never replaces it: a view
// labels the list's own points (good/bad, serving-cell changes) or is a
// labelled copy (level offset simulation; the fail simulation is a point
// list with Metric::failed_cells). Binning and the coverage rate of the
// tested area are in bins.h. Copied in part from yantra-v2 68ea20a:
// core/include/yantra/core/survey/views.h (classify, with_offset),
// adapted for IND-30, PP-02, PP-04.
//
// - RSSI (dBm), MER/BER (%) are metrics of the point list: Metric::Value
//   Level, or Quality with the technology's quality field (the core names
//   none, PLG-01; the UI lists the schema's QUALITY fields).
// - Direction: a value in "%" (error and erasure rates) is better when
//   lower, every other unit when higher (bins.h higher_is_better).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/survey/bins.h"
#include "yantra/core/survey/points.h"
#include "yantra/core/survey/stats.h"

namespace yantra::survey {

/// Good/bad against a threshold (PP-02). Good: value >= threshold when
/// higher is better, <= when lower is better. A point without a value in
/// the list's unit is NoValue and counts as not good.
enum class Verdict : std::uint8_t { Good, Bad, NoValue };
struct Classified {
    std::string label;  ///< "Good: >= -95 dBm"
    double threshold = 0.0;
    bool higher_better = true;
    std::vector<Verdict> verdicts;  ///< one per point, in list order
    std::size_t good = 0, bad = 0, no_value = 0;
    double percent_good = 0.0;  ///< of all points
    std::uint32_t good_rgb = 0x43a047, bad_rgb = 0xe53935, no_value_rgb = 0x9e9e9e;
    bool operator==(const Classified&) const = default;
};
/// `higher_better` nullopt: from the list's unit. std::invalid_argument
/// for a threshold that is not finite.
Classified classify(const PointList& list, double threshold, std::optional<bool> higher_better = std::nullopt);

/// Level offset simulation (PP-04, e.g. building penetration or body
/// loss): a copy of the list whose values in the list's unit are moved by
/// `offset_db` and recoloured with the list's scale; positions, order and
/// count unchanged. Only for Level and C1 (C1 moves with the level, dB
/// for dB): std::invalid_argument for a quality metric or an offset that
/// is not finite. The coverage is recomputed from the copy
/// (coverage_stats, floor_stats, bin_points, classify).
struct OffsetList {
    std::string label;  ///< "Simulation: level -10 dB (not measured)"
    double offset_db = 0.0;
    PointList list;
    bool operator==(const OffsetList&) const = default;
};
OffsetList with_offset(const PointList& list, double offset_db);

/// Serving-cell changes (PP-02) as markers: in list order within each
/// session, a point whose serving cell differs from the previous point's
/// that had one (the rule of CoverageStats::serving_cell_changes).
struct ServingChange {
    std::size_t index = 0;  ///< into the list's points
    std::int64_t session_id = 0;
    std::string from, to;   ///< cell identity texts
    bool operator==(const ServingChange&) const = default;
};
std::vector<ServingChange> serving_changes(const std::vector<SurveyPoint>& points);

/// The cells the points of a list name (serving and metric cells), with
/// their frequency, by frequency then cell text: the choice for the fail
/// simulation (PP-03).
std::vector<CellRef> cells_seen(const project::Project& project, const PointQuery& query);

}  // namespace yantra::survey
