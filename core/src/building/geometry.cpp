// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wall geometry (IND-40, IND-44), see geometry.h. Copied from yantra-v2
// 68ea20a: core/src/building/geometry.cpp, adapted (plan-frame metres).

#include "yantra/core/building/geometry.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace yantra::building {

namespace {

double cross(Point2 a, Point2 b) { return a.x * b.y - a.y * b.x; }
Point2 sub(Point2 a, Point2 b) { return {a.x - b.x, a.y - b.y}; }

}  // namespace

std::optional<double> segment_intersection(Point2 p0, Point2 p1, Point2 q0, Point2 q1) {
    const Point2 r = sub(p1, p0);
    const Point2 s = sub(q1, q0);
    const double denom = cross(r, s);
    const double scale = std::max({std::fabs(r.x), std::fabs(r.y), std::fabs(s.x), std::fabs(s.y), 1e-300});
    if (std::fabs(denom) <= 1e-12 * scale * scale) return std::nullopt;  // parallel or collinear
    const Point2 qp = sub(q0, p0);
    const double t = cross(qp, s) / denom;
    const double u = cross(qp, r) / denom;
    constexpr double eps = 1e-12;
    if (t < -eps || t > 1.0 + eps || u < -eps || u > 1.0 + eps) return std::nullopt;
    return std::clamp(t, 0.0, 1.0);
}

std::optional<std::vector<WallSegment>> wall_segments_m(const Building& b, const Floor& f, double mhz) {
    const auto mpu = b.metres_per_unit(f);
    if (!mpu) return std::nullopt;
    std::vector<WallSegment> out;
    for (std::size_t wi = 0; wi < f.walls.size(); ++wi) {
        const Wall& w = f.walls[wi];
        const auto att = b.wall_attenuation_db(w, mhz);
        if (!att) return std::nullopt;
        for (std::size_t si = 0; si + 1 < w.points.size(); ++si) {
            const Point2 a{w.points[si].x * *mpu, w.points[si].y * *mpu};
            const Point2 c{w.points[si + 1].x * *mpu, w.points[si + 1].y * *mpu};
            out.push_back({a, c, wi, si, *att});
        }
    }
    return out;
}

std::optional<WallCrossings> walls_crossed(const Building& b, const Floor& f, Point2 a, Point2 bm, double mhz) {
    const auto segments = wall_segments_m(b, f, mhz);
    if (!segments) return std::nullopt;
    WallCrossings result;
    // Per wall: crossing parameters already counted (a vertex hit gives two
    // segment hits at the same t).
    std::map<std::size_t, std::vector<double>> seen;
    for (const WallSegment& s : *segments) {
        const auto t = segment_intersection(a, bm, s.a, s.b);
        if (!t || *t <= kCrossingEndEps || *t >= 1.0 - kCrossingEndEps) continue;
        auto& ts = seen[s.wall];
        if (std::any_of(ts.begin(), ts.end(), [&](double x) { return std::fabs(x - *t) <= kCrossingEndEps; }))
            continue;
        ts.push_back(*t);
        const Point2 p{a.x + (bm.x - a.x) * *t, a.y + (bm.y - a.y) * *t};
        result.crossings.push_back({s.wall, s.segment, *t, p, s.attenuation_db});
    }
    std::stable_sort(result.crossings.begin(), result.crossings.end(),
                     [](const WallCrossing& x, const WallCrossing& y) { return x.t < y.t; });
    for (const auto& c : result.crossings) result.total_db += c.attenuation_db;
    return result;
}

}  // namespace yantra::building
