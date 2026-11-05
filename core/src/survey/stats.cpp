// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied in part from yantra-v2 68ea20a: core/src/survey/stats.cpp
// (percentile, coverage_stats, point_in_polygon), adapted for IND-30,
// EXP-05, EXP-06.

#include "yantra/core/survey/stats.h"

#include "yantra/core/survey/bins.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "yantra/core/survey/sample.h"

namespace yantra::survey {

std::optional<double> percentile(std::vector<double> values, double p) {
    if (!(p >= 0.0 && p <= 100.0)) throw std::invalid_argument("percentile: p must be 0..100");
    if (values.empty()) return std::nullopt;
    std::sort(values.begin(), values.end());
    const double h = static_cast<double>(values.size() - 1) * p / 100.0;
    const auto lo = static_cast<std::size_t>(std::floor(h));
    if (lo + 1 >= values.size()) return values.back();
    return values[lo] + (h - static_cast<double>(lo)) * (values[lo + 1] - values[lo]);
}

CoverageStats coverage_stats(const std::vector<SurveyPoint>& points, const std::string& unit,
                             const StatsOptions& options) {
    CoverageStats s;
    s.unit = unit;
    s.points = points.size();
    std::vector<double> v;
    using Cell = std::pair<std::string, std::uint64_t>;  // identity text, frequency
    std::map<Cell, std::size_t> cells;
    std::map<std::int64_t, Cell> last_cell;  // per session
    for (const SurveyPoint& p : points) {
        if (!p.value) ++s.no_value;
        else if (p.unit != unit) ++s.other_unit;
        else v.push_back(*p.value);
        const Cell cell{p.serving_cell, p.serving_hz};
        ++cells[cell];
        if (!p.serving_cell.empty() || p.serving_hz != 0) {
            auto [it, fresh] = last_cell.try_emplace(p.session_id, cell);
            if (!fresh && it->second != cell) {
                ++s.serving_cell_changes;
                it->second = cell;
            }
        }
    }
    s.with_value = v.size();
    for (double t : options.thresholds) {
        ThresholdShare share;
        share.threshold = t;
        for (double x : v) (x >= t ? share.at_or_above : share.below) += 1;
        if (s.points > 0) {
            share.percent_at_or_above = 100.0 * static_cast<double>(share.at_or_above) / static_cast<double>(s.points);
            share.percent_below = 100.0 * static_cast<double>(share.below) / static_cast<double>(s.points);
        }
        s.thresholds.push_back(share);
    }
    for (const auto& [cell, n] : cells)
        s.serving_cells.push_back(
            {cell.first, n, 100.0 * static_cast<double>(n) / static_cast<double>(s.points), cell.second});
    std::stable_sort(s.serving_cells.begin(), s.serving_cells.end(),
                     [](const CellShare& a, const CellShare& b) { return a.points > b.points; });
    if (v.empty()) return s;
    s.min = *std::min_element(v.begin(), v.end());
    s.max = *std::max_element(v.begin(), v.end());
    s.mean = arithmetic_mean(v);
    if (average_rule(unit) == AverageRule::Power) s.power_mean = power_mean_db(v);
    s.median = percentile(v, 50.0);
    for (double p : options.percentiles) s.percentiles.emplace_back(p, *percentile(v, p));
    return s;
}

CoverageStats coverage_stats(const PointList& list, const StatsOptions& options) {
    return coverage_stats(list.points, list.unit, options);
}

bool point_in_polygon(Point2 p, const std::vector<Point2>& poly) {
    bool inside = false;
    const std::size_t n = poly.size();
    if (n < 3) return false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point2& a = poly[i];
        const Point2& b = poly[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            const double x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (p.x < x) inside = !inside;
        }
    }
    return inside;
}

std::vector<SurveyPoint> points_in_area(const std::vector<SurveyPoint>& points, const std::vector<Point2>& polygon) {
    std::vector<SurveyPoint> out;
    for (const SurveyPoint& p : points)
        if (point_in_polygon(p.plan, polygon)) out.push_back(p);
    return out;
}

AcceptanceResult evaluate_floors(const AcceptanceProfile& profile, const std::vector<const std::vector<SurveyPoint>*>& floors) {
    AcceptanceResult r;
    r.profile = profile.name;
    r.basis = profile.basis;
    if (profile.basis == AcceptanceProfile::Basis::TestedArea) {
        // Squares per floor, each judged by its percentile (bins.h).
        std::vector<BinnedView> views;
        const ColourScale scale = default_colour_scale(profile.unit);
        for (const auto* f : floors) {
            views.push_back(bin_points(*f, profile.unit, scale, profile.square_m, profile.square_percentile));
            r.points += f->size();
            for (const Bin& b : views.back().bins) r.squares += b.count > 0 ? 1 : 0;
        }
        r.area_m2 = static_cast<double>(r.squares) * profile.square_m * profile.square_m;
        r.pass = r.squares > 0;
        for (const AcceptanceCriterion& crit : profile.criteria) {
            std::size_t covered = 0;
            for (const BinnedView& v : views) covered += area_coverage(v, crit.threshold, true).covered;
            CriterionResult c;
            c.criterion = crit;
            c.percent = r.squares > 0 ? 100.0 * static_cast<double>(covered) / static_cast<double>(r.squares) : 0.0;
            c.pass = r.squares > 0 && c.percent >= crit.min_percent;
            r.pass = r.pass && c.pass;
            r.criteria.push_back(c);
        }
        return r;
    }
    StatsOptions o;
    o.percentiles.clear();
    for (const AcceptanceCriterion& c : profile.criteria) o.thresholds.push_back(c.threshold);
    std::vector<SurveyPoint> all;
    for (const auto* f : floors) all.insert(all.end(), f->begin(), f->end());
    const CoverageStats s = coverage_stats(all, profile.unit, o);
    r.points = s.points;
    r.pass = s.points > 0;
    for (std::size_t i = 0; i < profile.criteria.size(); ++i) {
        CriterionResult c;
        c.criterion = profile.criteria[i];
        c.percent = s.thresholds[i].percent_at_or_above;
        c.pass = s.points > 0 && c.percent >= c.criterion.min_percent;
        r.pass = r.pass && c.pass;
        r.criteria.push_back(c);
    }
    return r;
}

AcceptanceResult evaluate(const AcceptanceProfile& profile, const std::vector<SurveyPoint>& points) {
    return evaluate_floors(profile, {&points});
}

namespace {

FloorStats stats_of(const std::string& floor_id, const std::string& unit, const std::vector<SurveyPoint>& points,
                    const StatsOptions& options, const std::vector<Area>& areas,
                    const std::vector<AcceptanceProfile>& profiles,
                    const std::vector<const std::vector<SurveyPoint>*>& floors) {
    FloorStats f;
    f.floor_id = floor_id;
    f.unit = unit;
    f.all = coverage_stats(points, unit, options);
    for (const Area& a : areas) f.areas.emplace_back(a.name, coverage_stats(points_in_area(points, a.polygon), unit, options));
    for (const AcceptanceProfile& p : profiles) f.acceptance.push_back(evaluate_floors(p, floors));
    return f;
}

}  // namespace

FloorStats floor_stats(const PointList& list, const StatsOptions& options, const std::vector<Area>& areas,
                       const std::vector<AcceptanceProfile>& profiles) {
    return stats_of(list.floor_id, list.unit, list.points, options, areas, profiles, {&list.points});
}

FloorStats building_stats(const std::vector<PointList>& floors, const StatsOptions& options,
                          const std::vector<AcceptanceProfile>& profiles) {
    std::string unit;
    std::vector<SurveyPoint> all;
    std::vector<const std::vector<SurveyPoint>*> groups;
    for (const PointList& l : floors) {
        if (unit.empty()) unit = l.unit;
        all.insert(all.end(), l.points.begin(), l.points.end());
        groups.push_back(&l.points);
    }
    return stats_of("", unit, all, options, {}, profiles, groups);
}

}  // namespace yantra::survey
