// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Binning (PP-05) and the coverage rate of the tested area (PP-02, EXP-06
// area basis without interpolation). docs/SURVEY.md "Post-processing".
// Copied from yantra-v2 68ea20a: core/include/yantra/core/survey/stats.h
// (Bin, BinnedView, bin_points) and views.h (AreaCoverage), adapted for
// IND-30 (labelled extra view) and EXP-06.
//
// - A binned view is an EXTRA, labelled view built from the shared point
//   list (points.h, IND-30); it never replaces the measured points.
// - Squares of `size_m` in plan-frame metres: square (i, j) covers
//   [i size, (i+1) size) x [j size, (j+1) size), so square edges are on
//   multiples of the size from the plan's origin (top-left corner of the
//   floor). Every point with metres is in exactly one square.
// - The value of a square is the given percentile (stats.h percentile,
//   R-7; default 50 = median) of its values in the view's unit; a square
//   whose points have no value in that unit has no value.
// - Tested area: the squares holding at least one point. Coverage rate of
//   the tested area: covered squares / tested squares, a square being
//   covered when its value is good against the threshold (>= when higher
//   is better, <= for error rates in %); a square without a value is not
//   covered. Area in m2 = squares x size^2.
// - A floor without a scale has no metres: no squares (to_scale false).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/survey/points.h"

namespace yantra::survey {

struct Bin {
    std::int64_t i = 0, j = 0;    ///< square column (x) and row (y)
    Point2 centre_m;              ///< plan-frame metres
    std::size_t count = 0;        ///< points in the square
    std::size_t with_value = 0;   ///< of which with a value in the view's unit
    std::optional<double> value;  ///< the percentile; nullopt when no point has a value
    std::uint32_t rgb = 0;        ///< the scale's colour of `value`
    bool operator==(const Bin&) const = default;
};

struct BinnedView {
    std::string label;      ///< "Binned view: 2 m squares, P50 per square (not the measured points)"
    std::string floor_id;
    std::string unit;
    double size_m = 0.0;
    double percentile = 50.0;
    bool to_scale = true;   ///< false: points without metres (no scale) were left out
    std::size_t points = 0;       ///< points binned (with metres)
    std::size_t left_out = 0;     ///< points without metres
    std::vector<Bin> bins;  ///< by row j, then column i
    bool operator==(const BinnedView&) const = default;
};

/// The binned view of a list (its unit and colour scale).
/// std::invalid_argument for size_m <= 0 (or not finite) or a
/// percentile outside 0..100.
BinnedView bin_points(const PointList& list, double size_m, double percentile = 50.0);
/// The same for a selection of points (e.g. an area's).
BinnedView bin_points(const std::vector<SurveyPoint>& points, const std::string& unit, const ColourScale& scale,
                      double size_m, double percentile = 50.0);

/// Coverage rate of the tested area (definitions above).
struct AreaCoverage {
    std::string label;      ///< "Coverage of the tested area (2 m squares, P50): >= -95 dBm"
    double threshold = 0.0;
    bool higher_better = true;
    std::size_t squares = 0;  ///< tested squares
    std::size_t covered = 0;
    double tested_m2 = 0.0;
    double covered_m2 = 0.0;
    double percent = 0.0;     ///< covered of tested; 0 without squares
    bool operator==(const AreaCoverage&) const = default;
};
/// `higher_better` nullopt: from the view's unit (points.h ColourScale
/// rule: "%" is lower-is-better). std::invalid_argument for a threshold
/// that is not finite.
AreaCoverage area_coverage(const BinnedView& view, double threshold, std::optional<bool> higher_better = std::nullopt);

/// True when a higher value is better: every unit but "%" (error and
/// erasure rates).
bool higher_is_better(const std::string& unit);

}  // namespace yantra::survey
