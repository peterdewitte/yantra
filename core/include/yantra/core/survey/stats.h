// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Coverage statistics (IND-30, EXP-05, EXP-06, PP-02, TST-32) of the
// shared point list (points.h), per floor, per area (a polygon on the
// plan) and for the building. docs/SURVEY.md "Coverage statistics" has
// the definitions. Copied in part from yantra-v2 68ea20a:
// core/include/yantra/core/survey/stats.h, adapted for IND-30, EXP-06.
//
// - Which points count: every point of the selection. A point without a
//   value for the metric, or with a value in another unit, counts as a
//   point but is in no share: for a coverage figure it is not covered.
// - Shares: at_or_above = points with value >= threshold, in percent of
//   ALL points of the selection.
// - Percentiles: linear interpolation between closest ranks (R-7,
//   spreadsheet PERCENTILE.INC): sorted x[0..n-1], h = (n-1) p / 100,
//   P = x[floor h] + (h - floor h)(x[floor h + 1] - x[floor h]).
// - mean: arithmetic mean of the values as given; power_mean: 10 log10
//   of the mean power, for dB units only.
// - Acceptance (EXP-06): a criterion "at least X % of the points >= Y"
//   passes when percent_at_or_above >= X; a profile passes when all its
//   criteria pass, and fails on a selection without points.
// - Serving-cell changes: in list order within each session, a point
//   whose serving cell (identity text and carrier frequency) differs
//   from the previous point's that had one.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yantra/core/survey/points.h"

namespace yantra::survey {

/// The p-th percentile (0..100), rule above; nullopt for no values;
/// std::invalid_argument for p outside 0..100.
std::optional<double> percentile(std::vector<double> values, double p);

struct ThresholdShare {
    double threshold = 0.0;
    std::size_t at_or_above = 0;
    std::size_t below = 0;             ///< with a value below
    double percent_at_or_above = 0.0;  ///< of all points
    double percent_below = 0.0;        ///< of all points
    bool operator==(const ThresholdShare&) const = default;
};

struct StatsOptions {
    std::vector<double> thresholds;  ///< e.g. {-95, -85} dBm
    std::vector<double> percentiles{5.0, 10.0, 50.0, 90.0, 95.0};
};

struct CellShare {
    std::string cell;  ///< serving cell identity text ("" = none)
    std::size_t points = 0;
    double percent = 0.0;  ///< of all points
    std::uint64_t frequency_hz = 0;  ///< the serving carrier (cells on two carriers are two cells)
    bool operator==(const CellShare&) const = default;
};

struct CoverageStats {
    std::string unit;
    std::size_t points = 0;      ///< all points of the selection
    std::size_t with_value = 0;  ///< values in `unit`
    std::size_t no_value = 0;
    std::size_t other_unit = 0;
    std::optional<double> min, max, mean, power_mean, median;
    std::vector<std::pair<double, double>> percentiles;  ///< (p, value)
    std::vector<ThresholdShare> thresholds;
    std::size_t serving_cell_changes = 0;
    std::vector<CellShare> serving_cells;  ///< by points, descending; ties by cell text
    bool operator==(const CoverageStats&) const = default;
};

/// Statistics of `points` with values in `unit`.
CoverageStats coverage_stats(const std::vector<SurveyPoint>& points, const std::string& unit,
                             const StatsOptions& options = {});
/// Of a whole floor's list (its unit).
CoverageStats coverage_stats(const PointList& list, const StatsOptions& options = {});

/// Even-odd rule; a point exactly on an edge may fall either way.
bool point_in_polygon(Point2 p, const std::vector<Point2>& polygon);

/// An area drawn on a floor (plan units), e.g. a room or a wing.
struct Area {
    std::string name;
    std::vector<Point2> polygon;
};
/// The points inside an area, in list order (a selection, not another set).
std::vector<SurveyPoint> points_in_area(const std::vector<SurveyPoint>& points, const std::vector<Point2>& polygon);

/// An acceptance profile (EXP-06), e.g. "95 % of the points >= -95 dBm
/// and 99 % >= -100 dBm". Thresholds in `unit`. The basis says what the
/// percentages are of:
/// - Points: the measured points (every point of the selection).
/// - TestedArea: the tested area of the binned view (bins.h): squares of
///   `square_m` holding a point, a square counting when its percentile
///   `square_percentile` is >= the threshold. Per floor (squares of
///   different floors never merge); without a scale a floor has no
///   squares. This is the "% of area" of EXP-06 until the interpolated
///   map (IND-40..45) gives the floor-area basis.
struct AcceptanceCriterion {
    double threshold = 0.0;
    double min_percent = 0.0;
    bool operator==(const AcceptanceCriterion&) const = default;
};
struct AcceptanceProfile {
    enum class Basis { Points, TestedArea };
    std::string name;
    std::string unit = "dBm";
    std::vector<AcceptanceCriterion> criteria;
    Basis basis = Basis::Points;
    double square_m = 2.0;             ///< TestedArea: square size
    double square_percentile = 50.0;   ///< TestedArea: value of a square
};
struct CriterionResult {
    AcceptanceCriterion criterion;
    double percent = 0.0;  ///< of all points (of the tested area) at or above the threshold
    bool pass = false;
    bool operator==(const CriterionResult&) const = default;
};
struct AcceptanceResult {
    std::string profile;
    AcceptanceProfile::Basis basis = AcceptanceProfile::Basis::Points;
    std::size_t points = 0;
    std::size_t squares = 0;  ///< TestedArea: tested squares
    double area_m2 = 0.0;     ///< TestedArea: tested area
    bool pass = false;        ///< false without points (squares)
    std::vector<CriterionResult> criteria;
    bool operator==(const AcceptanceResult&) const = default;
};
/// `points` of one floor (for TestedArea).
AcceptanceResult evaluate(const AcceptanceProfile& profile, const std::vector<SurveyPoint>& points);
/// Over several floors (the building summary): points together, squares per floor.
AcceptanceResult evaluate_floors(const AcceptanceProfile& profile,
                                 const std::vector<const std::vector<SurveyPoint>*>& floors);

/// The statistics of one floor (EXP-05 floor section): the whole floor,
/// each area and each acceptance profile.
struct FloorStats {
    std::string floor_id;
    std::string unit;
    CoverageStats all;
    std::vector<std::pair<std::string, CoverageStats>> areas;  ///< by area, in the given order
    std::vector<AcceptanceResult> acceptance;
    bool operator==(const FloorStats&) const = default;
};
FloorStats floor_stats(const PointList& list, const StatsOptions& options = {}, const std::vector<Area>& areas = {},
                       const std::vector<AcceptanceProfile>& profiles = {});

/// The building summary: every floor's points together (the unit of the
/// first floor list with a unit), and the profiles over all of them.
FloorStats building_stats(const std::vector<PointList>& floors, const StatsOptions& options = {},
                          const std::vector<AcceptanceProfile>& profiles = {});

}  // namespace yantra::survey
