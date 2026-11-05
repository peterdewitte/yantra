// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/survey/stats.cpp (bin_points)
// and views.cpp (area_coverage), adapted for IND-30 and EXP-06.

#include "yantra/core/survey/bins.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <utility>

#include "yantra/core/survey/stats.h"

namespace yantra::survey {

namespace {

std::string number_text(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

bool good(double value, double threshold, bool higher_better) {
    return higher_better ? value >= threshold : value <= threshold;
}

}  // namespace

bool higher_is_better(const std::string& unit) { return unit != "%"; }

BinnedView bin_points(const std::vector<SurveyPoint>& points, const std::string& unit, const ColourScale& scale,
                      double size_m, double pct) {
    if (!(size_m > 0.0) || !std::isfinite(size_m)) throw std::invalid_argument("bin_points: size must be > 0");
    if (!(pct >= 0.0 && pct <= 100.0)) throw std::invalid_argument("bin_points: percentile must be 0..100");
    BinnedView view;
    view.size_m = size_m;
    view.percentile = pct;
    view.unit = unit;
    view.label = "Binned view: " + number_text(size_m) + " m squares, P" + number_text(pct) +
                 " per square (not the measured points)";
    struct Acc {
        std::size_t count = 0;
        std::vector<double> values;
    };
    std::map<std::pair<std::int64_t, std::int64_t>, Acc> squares;  // (j, i)
    for (const SurveyPoint& p : points) {
        if (!p.metres) {
            ++view.left_out;
            continue;
        }
        ++view.points;
        const auto i = static_cast<std::int64_t>(std::floor(p.metres->x / size_m));
        const auto j = static_cast<std::int64_t>(std::floor(p.metres->y / size_m));
        Acc& a = squares[{j, i}];
        ++a.count;
        if (p.value && p.unit == unit) a.values.push_back(*p.value);
    }
    view.to_scale = view.left_out == 0;
    for (auto& [key, a] : squares) {
        Bin b;
        b.j = key.first;
        b.i = key.second;
        b.centre_m = {(static_cast<double>(b.i) + 0.5) * size_m, (static_cast<double>(b.j) + 0.5) * size_m};
        b.count = a.count;
        b.with_value = a.values.size();
        b.value = percentile(std::move(a.values), pct);
        b.rgb = scale.colour_of(scale.unit == unit ? b.value : std::nullopt);
        view.bins.push_back(b);
    }
    return view;
}

BinnedView bin_points(const PointList& list, double size_m, double pct) {
    BinnedView v = bin_points(list.points, list.unit, list.scale, size_m, pct);
    v.floor_id = list.floor_id;
    return v;
}

AreaCoverage area_coverage(const BinnedView& view, double threshold, std::optional<bool> higher_better) {
    if (!std::isfinite(threshold)) throw std::invalid_argument("area_coverage: threshold must be finite");
    AreaCoverage a;
    a.threshold = threshold;
    a.higher_better = higher_better.value_or(higher_is_better(view.unit));
    a.label = "Coverage of the tested area (" + number_text(view.size_m) + " m squares, P" +
              number_text(view.percentile) + "): " + (a.higher_better ? ">= " : "<= ") + number_text(threshold) +
              (view.unit.empty() ? "" : " " + view.unit);
    const double square = view.size_m * view.size_m;
    for (const Bin& b : view.bins) {
        if (b.count == 0) continue;
        ++a.squares;
        if (b.value && good(*b.value, threshold, a.higher_better)) ++a.covered;
    }
    a.tested_m2 = static_cast<double>(a.squares) * square;
    a.covered_m2 = static_cast<double>(a.covered) * square;
    if (a.squares > 0) a.percent = 100.0 * static_cast<double>(a.covered) / static_cast<double>(a.squares);
    return a;
}

}  // namespace yantra::survey
