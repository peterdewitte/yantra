// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Residual interpolation and confidence mask (IND-41, IND-43); equations
// in residuals.h and docs/PROPAGATION.md. The wall test of a cell-to-point
// line is the ray tracer's: the box test of kernels.h (SIMD or scalar,
// same selection) and the shared exact crossing test.
//
// Copied from yantra-v2 68ea20a: core/src/raytrace/residuals.cpp, adapted
// (plan-frame metres of the building model).

#include "yantra/core/raytrace/residuals.h"

#include "kernels.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace yantra::raytrace {

namespace {

std::string fmt(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

/// The wall segments of one floor in plan-frame metres, boxes padded to
/// kLanes.
struct Walls {
    std::vector<double> ax, ay, bx, by, minx, miny, maxx, maxy;
    std::size_t n = 0;
    detail::SelectFn select = detail::select_scalar;

    Walls(const building::Building& b, const building::Floor& f, Isa isa) {
#if YANTRA_RAYTRACE_SIMD
        if (isa == Isa::Simd) select = detail::select_simd;
#else
        (void)isa;
#endif
        for (const auto& w : f.walls)
            for (std::size_t i = 0; i + 1 < w.points.size(); ++i) {
                const Point2 a = *b.to_plan_metres(f, w.points[i]);
                const Point2 c = *b.to_plan_metres(f, w.points[i + 1]);
                ax.push_back(a.x);
                ay.push_back(a.y);
                bx.push_back(c.x);
                by.push_back(c.y);
                minx.push_back(std::min(a.x, c.x));
                maxx.push_back(std::max(a.x, c.x));
                miny.push_back(std::min(a.y, c.y));
                maxy.push_back(std::max(a.y, c.y));
            }
        n = ax.size();
        const double inf = std::numeric_limits<double>::infinity();
        while (minx.size() % detail::kLanes != 0) {
            minx.push_back(inf);
            miny.push_back(inf);
            maxx.push_back(-inf);
            maxy.push_back(-inf);
        }
    }

    /// True when the line p-q crosses a wall (the walls_crossed rules).
    bool blocked(Point2 p, Point2 q, std::vector<std::uint32_t>& cand) const {
        if (n == 0) return false;
        cand.resize(minx.size() + detail::kLanes);
        const std::size_t k = select(p.x, p.y, q.x, q.y, minx.data(), miny.data(), maxx.data(), maxy.data(),
                                     minx.size(), cand.data());
        for (std::size_t j = 0; j < k; ++j) {
            const std::size_t i = cand[j];
            if (detail::crossing(p, q, {ax[i], ay[i]}, {bx[i], by[i]}) >= 0.0) return true;
        }
        return false;
    }
};

/// Points in square buckets of side `size`.
struct Buckets {
    double size = 1.0;
    std::unordered_map<std::int64_t, std::vector<std::size_t>> cells;

    static std::int64_t key(std::int64_t i, std::int64_t j) { return i * 4'000'003 + j; }
    std::int64_t idx(double v) const { return static_cast<std::int64_t>(std::floor(v / size)); }

    Buckets(const std::vector<Residual>& r, double s) : size(s) {
        for (std::size_t i = 0; i < r.size(); ++i)
            cells[key(idx(r[i].position_m.x), idx(r[i].position_m.y))].push_back(i);
    }
};

struct Evaluator {
    const std::vector<Residual>& res;
    const ResidualOptions& o;
    const Walls& walls;
    const Buckets& buckets;
    double reach;

    ResidualAt at(Point2 x, std::vector<std::uint32_t>& cand) const {
        double sw = 0.0;
        double swr = 0.0;
        double coincident = 0.0;
        int coincident_n = 0;
        double nearest = std::numeric_limits<double>::infinity();
        const std::int64_t bi = buckets.idx(x.x);
        const std::int64_t bj = buckets.idx(x.y);
        const std::int64_t span = static_cast<std::int64_t>(std::ceil(reach / buckets.size));
        // Fixed order (bucket rows, then columns, then point order) so the
        // sums are the same on every run.
        for (std::int64_t j = bj - span; j <= bj + span; ++j)
            for (std::int64_t i = bi - span; i <= bi + span; ++i) {
                auto it = buckets.cells.find(Buckets::key(i, j));
                if (it == buckets.cells.end()) continue;
                for (std::size_t k : it->second) {
                    const Residual& r = res[k];
                    const double dx = r.position_m.x - x.x;
                    const double dy = r.position_m.y - x.y;
                    const double d = std::sqrt(dx * dx + dy * dy);
                    if (d > reach) continue;
                    if (o.walls_limit && d > 1e-9 && walls.blocked(x, r.position_m, cand)) continue;
                    nearest = std::min(nearest, d);
                    if (d > o.radius_m) continue;
                    if (d <= 1e-9) {
                        coincident += r.residual_db;
                        ++coincident_n;
                        continue;
                    }
                    const double w = std::pow(d, -o.power);
                    sw += w;
                    swr += w * r.residual_db;
                }
            }
        ResidualAt out;
        if (coincident_n > 0)
            out.correction_db = coincident / coincident_n;
        else if (sw > 0.0)
            out.correction_db = swr / (std::pow(o.radius_m, -o.power) + sw);
        if (nearest <= o.full_confidence_m)
            out.confidence = 1.0;
        else if (nearest < o.zero_confidence_m)
            out.confidence = (o.zero_confidence_m - nearest) / (o.zero_confidence_m - o.full_confidence_m);
        return out;
    }
};

const building::Floor& scaled_floor(const building::Building& b, const std::string& floor_id) {
    const building::Floor* f = b.find_floor(floor_id);
    if (f == nullptr) throw std::invalid_argument("no floor '" + floor_id + "'");
    if (!b.has_scale(*f)) throw std::logic_error("floor '" + floor_id + "' has no scale");
    return *f;
}

void check(const ResidualOptions& o) {
    auto p = o.validate();
    if (!p.empty()) throw std::invalid_argument("invalid residual options: " + p.front());
}

}  // namespace

std::vector<std::string> ResidualOptions::validate() const {
    std::vector<std::string> out;
    if (!std::isfinite(radius_m) || radius_m < 0.5 || radius_m > 1000.0)
        out.push_back("radius_m: " + fmt(radius_m) + " is outside 0.5 .. 1000");
    if (!std::isfinite(power) || power < 0.5 || power > 4.0)
        out.push_back("power: " + fmt(power) + " is outside 0.5 .. 4");
    if (!std::isfinite(zero_confidence_m) || zero_confidence_m <= 0.0 || zero_confidence_m > 1000.0)
        out.push_back("zero_confidence_m: " + fmt(zero_confidence_m) + " is outside (0, 1000]");
    if (!std::isfinite(full_confidence_m) || full_confidence_m < 0.0 || full_confidence_m >= zero_confidence_m)
        out.push_back("full_confidence_m: " + fmt(full_confidence_m) + " is outside 0 .. zero_confidence_m");
    return out;
}

ResidualField interpolate_residuals(const building::Building& b, const std::string& floor_id, const Grid& grid,
                                    const std::vector<Residual>& residuals, const ResidualOptions& options,
                                    const RunControl& run) {
    check(options);
    const building::Floor& f = scaled_floor(b, floor_id);
    const Walls walls(b, f, run.isa);
    const double reach = std::max(options.radius_m, options.zero_confidence_m);
    const Buckets buckets(residuals, reach);
    const Evaluator ev{residuals, options, walls, buckets, reach};
    ResidualField out;
    out.grid = grid;
    out.correction_db.assign(grid.size(), 0.0);
    out.confidence.assign(grid.size(), 0.0f);
    const bool complete = detail::run_parallel(run, static_cast<std::size_t>(grid.rows), [&](std::size_t row) {
        std::vector<std::uint32_t> cand;
        for (int col = 0; col < grid.cols; ++col) {
            const auto r = ev.at(grid.centre(col, static_cast<int>(row)), cand);
            const std::size_t i = grid.index(col, static_cast<int>(row));
            out.correction_db[i] = r.correction_db;
            out.confidence[i] = static_cast<float>(r.confidence);
        }
    });
    out.cancelled = !complete;
    return out;
}

ResidualAt residual_at(const building::Building& b, const std::string& floor_id, Point2 position_m,
                       const std::vector<Residual>& residuals, const ResidualOptions& options) {
    check(options);
    const building::Floor& f = scaled_floor(b, floor_id);
    const Walls walls(b, f, Isa::Scalar);
    const double reach = std::max(options.radius_m, options.zero_confidence_m);
    const Buckets buckets(residuals, reach);
    const Evaluator ev{residuals, options, walls, buckets, reach};
    std::vector<std::uint32_t> cand;
    return ev.at(position_m, cand);
}

}  // namespace yantra::raytrace
