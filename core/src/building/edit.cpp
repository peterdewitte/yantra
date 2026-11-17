// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Editing the building model (IND-08, IND-09), see edit.h.

#include "yantra/core/building/edit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace yantra::building {

namespace {

std::optional<Scale> measured(Scale::Method method, Point2 a, Point2 b, double distance_m) {
    Scale s;
    s.method = method;
    s.a = a;
    s.b = b;
    s.distance_m = distance_m;
    if (!own_metres_per_unit(s)) return std::nullopt;
    return s;
}

}  // namespace

// --- scale ------------------------------------------------------------------------------

std::optional<Scale> two_point_scale(Point2 a, Point2 b, double distance_m) {
    return measured(Scale::Method::TwoPoints, a, b, distance_m);
}

std::optional<Scale> scale_bar(Point2 a, Point2 b, double length_m) {
    return measured(Scale::Method::ScaleBar, a, b, length_m);
}

std::optional<Scale> factor_scale(double metres_per_unit) {
    Scale s;
    s.method = Scale::Method::Factor;
    s.metres_per_unit = metres_per_unit;
    if (!own_metres_per_unit(s)) return std::nullopt;
    return s;
}

Scale shared_scale(const std::string& floor_id) {
    Scale s;
    s.method = Scale::Method::Shared;
    s.shared_floor = floor_id;
    return s;
}

bool same_sheet(const Floor& a, const Floor& b) {
    return !a.source.empty() && a.source == b.source && a.page == b.page;
}

std::vector<std::string> floors_sharing_scale(const Building& b, const std::string& floor_id) {
    std::vector<std::string> out;
    for (std::size_t i : b.floors_by_level()) {
        const Floor& f = b.floors[i];
        if (f.scale.method == Scale::Method::Shared && f.scale.shared_floor == floor_id) out.push_back(f.id);
    }
    return out;
}

double distance_units(Point2 a, Point2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

std::optional<double> distance_m(const Building& b, const Floor& f, Point2 p, Point2 q) {
    const auto m = b.metres_per_unit(f);
    if (!m) return std::nullopt;
    return distance_units(p, q) * *m;
}

std::optional<double> length_m(const Building& b, const Floor& f, const std::vector<Point2>& polyline) {
    const auto m = b.metres_per_unit(f);
    if (!m) return std::nullopt;
    double units = 0.0;
    for (std::size_t i = 1; i < polyline.size(); ++i) units += distance_units(polyline[i - 1], polyline[i]);
    return units * *m;
}

std::optional<double> area_m2(const Building& b, const Floor& f, const std::vector<Point2>& polygon) {
    const auto m = b.metres_per_unit(f);
    if (!m) return std::nullopt;
    double twice = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point2& p = polygon[i];
        const Point2& q = polygon[(i + 1) % polygon.size()];
        twice += p.x * q.y - q.x * p.y;
    }
    return std::fabs(twice) * 0.5 * *m * *m;
}

std::string format_length(std::optional<double> metres) {
    if (!metres || !std::isfinite(*metres)) return "not to scale";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.2f m", *metres + 0.0);  // + 0.0: no "-0.00"
    return buf;
}

std::string format_area(std::optional<double> square_metres) {
    if (!square_metres || !std::isfinite(*square_metres)) return "not to scale";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f m²", *square_metres + 0.0);
    return buf;
}

// --- alignment -----------------------------------------------------------------------------

bool aligned(const Building& b, const Floor& f) { return f.reference_point.has_value() && b.has_scale(f); }

std::optional<Point2> point_on_floor(const Building& b, const Floor& from, Point2 p, const Floor& to) {
    if (!aligned(b, from) || !aligned(b, to)) return std::nullopt;
    const auto m = b.to_building_metres(from, p);
    if (!m) return std::nullopt;
    return b.from_building_metres(to, *m);
}

}  // namespace yantra::building
