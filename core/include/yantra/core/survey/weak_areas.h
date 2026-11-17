// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Weak areas of a floor (EXP-05, EXP-06, DEMO-16): the measured points
// below an acceptance threshold, grouped by distance, so a report that
// fails can name where the coverage is too low.
//
// Contract:
//   - only points with plan-frame metres and a value in `unit` count;
//     a point is weak when its value is below `threshold`;
//   - two weak points are in the same area when they are at most
//     `link_m` apart, directly or through other weak points (single
//     linkage);
//   - each area gives its point count, lowest value, bounding box in
//     plan-frame metres and the most frequent non-empty point label
//     (manual points' labels; ties: the first in point order);
//   - areas come largest first (points), then lowest value, then box.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "yantra/core/survey/points.h"

namespace yantra::survey {

struct WeakAreaOptions {
    double threshold = -95.0;
    std::string unit = "dBm";
    double link_m = 3.0;
};

struct WeakArea {
    std::size_t points = 0;
    double lowest = 0.0;
    Point2 min_m;  ///< bounding box, plan-frame metres
    Point2 max_m;
    std::string label;
    bool operator==(const WeakArea&) const = default;
};

std::vector<WeakArea> weak_areas(const std::vector<SurveyPoint>& points, const WeakAreaOptions& options);

/// Where a box lies on a plan of `plan_size_m`, in thirds of its width and
/// height (y down, as drawn): "top-left", "top", "top-right", "left",
/// "centre", "right", "bottom-left", "bottom" or "bottom-right" of the
/// box's centre.
std::string plan_part(Point2 min_m, Point2 max_m, Point2 plan_size_m);

}  // namespace yantra::survey
