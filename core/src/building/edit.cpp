// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Editing the building model (IND-08, IND-09, IND-11, IND-12), see edit.h.

#include "yantra/core/building/edit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace yantra::building {

namespace {

bool finite(Point2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }

double wrap360(double deg) {
    double d = std::fmod(deg, 360.0);
    if (d < 0.0) d += 360.0;
    return d >= 360.0 ? 0.0 : d;
}

std::optional<Scale> measured(Scale::Method method, Point2 a, Point2 b, double distance_m) {
    Scale s;
    s.method = method;
    s.a = a;
    s.b = b;
    s.distance_m = distance_m;
    if (!own_metres_per_unit(s)) return std::nullopt;
    return s;
}

// Nearest point of segment a-b to p: parameter t in [0, 1].
double closest_t(Point2 a, Point2 b, Point2 p) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    if (!(len2 > 0.0)) return 0.0;
    return std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0);
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

// --- walls ---------------------------------------------------------------------------------

std::optional<WallHit> nearest_wall(const Floor& f, Point2 p, double tolerance) {
    std::optional<WallHit> best;
    for (std::size_t wi = 0; wi < f.walls.size(); ++wi) {
        const Wall& w = f.walls[wi];
        for (std::size_t s = 0; s + 1 < w.points.size(); ++s) {
            const Point2 a = w.points[s];
            const Point2 c = w.points[s + 1];
            const double t = closest_t(a, c, p);
            const Point2 n{a.x + (c.x - a.x) * t, a.y + (c.y - a.y) * t};
            const double d = distance_units(n, p);
            // <=: a later wall (drawn on top) wins a tie.
            if (d <= tolerance && (!best || d <= best->distance)) best = WallHit{wi, s, t, n, d};
        }
    }
    return best;
}

std::optional<std::size_t> nearest_vertex(const Wall& w, Point2 p, double tolerance) {
    std::optional<std::size_t> best;
    double best_d = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < w.points.size(); ++i) {
        const double d = distance_units(w.points[i], p);
        if (d <= tolerance && d < best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

bool move_vertex(Wall& w, std::size_t index, Point2 to) {
    if (index >= w.points.size()) return false;
    w.points[index] = to;
    return true;
}

bool insert_vertex(Wall& w, std::size_t index, Point2 p) {
    if (index > w.points.size()) return false;
    w.points.insert(w.points.begin() + static_cast<std::ptrdiff_t>(index), p);
    return true;
}

bool remove_vertex(Wall& w, std::size_t index) {
    if (index >= w.points.size() || w.points.size() <= 2) return false;
    w.points.erase(w.points.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void move_wall(Wall& w, Point2 by) {
    for (Point2& p : w.points) {
        p.x += by.x;
        p.y += by.y;
    }
}

std::optional<std::pair<Wall, Wall>> split_wall(const Wall& w, std::size_t index, const std::string& second_id) {
    if (index == 0 || index + 1 >= w.points.size()) return std::nullopt;
    Wall first = w;
    Wall second = w;
    first.points.assign(w.points.begin(), w.points.begin() + static_cast<std::ptrdiff_t>(index) + 1);
    second.points.assign(w.points.begin() + static_cast<std::ptrdiff_t>(index), w.points.end());
    second.id = second_id;
    return std::pair<Wall, Wall>{std::move(first), std::move(second)};
}

double wall_length_units(const Wall& w) {
    double units = 0.0;
    for (std::size_t i = 1; i < w.points.size(); ++i) units += distance_units(w.points[i - 1], w.points[i]);
    return units;
}

void put_wall_class(std::vector<WallClass>& table, const WallClass& c) {
    for (WallClass& have : table)
        if (have.id == c.id) {
            have = c;
            return;
        }
    table.push_back(c);
}

std::size_t walls_of_class(const Building& b, const std::string& id) {
    std::size_t n = 0;
    for (const Floor& f : b.floors)
        for (const Wall& w : f.walls)
            if (w.wall_class == id) ++n;
    return n;
}

void reset_default_wall_classes(std::vector<WallClass>& table) {
    for (const WallClass& c : default_wall_classes()) put_wall_class(table, c);
}

// --- antennas ------------------------------------------------------------------------------

AntennaPattern directional_pattern(double azimuth_deg, double beamwidth_deg, double front_to_back_db) {
    AntennaPattern p;
    p.type = AntennaPattern::Type::Sector;
    p.azimuth_deg = wrap360(azimuth_deg);
    p.beamwidth_deg = beamwidth_deg;
    p.front_to_back_db = front_to_back_db;
    return p;
}

bool aim_antenna(Antenna& a, Point2 target) {
    if (!finite(target) || distance_units(a.position, target) <= 0.0) return false;
    const double bearing = bearing_deg(a.position, target);
    if (a.type == AntennaType::OutdoorDonor) a.direction_deg = bearing;
    if (a.pattern.type != AntennaPattern::Type::Omni) a.pattern.azimuth_deg = bearing;
    return true;
}

std::optional<std::size_t> nearest_antenna(const Floor& f, Point2 p, double tolerance) {
    std::optional<std::size_t> best;
    double best_d = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < f.antennas.size(); ++i) {
        const double d = distance_units(f.antennas[i].position, p);
        if (d <= tolerance && d <= best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

}  // namespace yantra::building
