// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ray tracer (IND-40, IND-44): image method for the reflections, beam
// apertures to prune images that can never reach the floor, a row-wise
// scan of each image's beam over the grid, and the wall-candidate kernel
// (kernels.h) for the walls along every leg. docs/PROPAGATION.md.
//
// Copied from yantra-v2 68ea20a: core/src/raytrace/model.cpp, adapted
// (plan-frame metres of the map's floor, groups per Cell with their own
// carrier frequency, per-class reflection losses, free space below the
// reference distance, outdoor donor directions not traced).

#include "yantra/core/raytrace/model.h"

#include "kernels.h"
#include "parallel.h"
#include "yantra/core/building/geometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace yantra::raytrace {

using building::AntennaPattern;
using building::Building;
using building::Floor;

using detail::run_parallel;

namespace {

constexpr double kSpeedOfLight = 299'792'458.0;

double cross(Point2 a, Point2 b) { return a.x * b.y - a.y * b.x; }
Point2 sub(Point2 a, Point2 b) { return {a.x - b.x, a.y - b.y}; }
Point2 add(Point2 a, Point2 b) { return {a.x + b.x, a.y + b.y}; }
Point2 mul(Point2 a, double s) { return {a.x * s, a.y * s}; }
double dot(Point2 a, Point2 b) { return a.x * b.x + a.y * b.y; }
double norm(Point2 a) { return std::sqrt(dot(a, a)); }

std::string fmt(double v) {
    std::ostringstream o;
    o << v;
    return o.str();
}

void check_range(std::vector<std::string>& out, const std::string& name, double v, double lo, double hi) {
    if (!std::isfinite(v) || v < lo || v > hi)
        out.push_back(name + ": " + fmt(v) + " is outside " + fmt(lo) + " .. " + fmt(hi));
}

double free_space_db(double d_m, double frequency_hz) {
    return 20.0 * std::log10(4.0 * std::numbers::pi * std::max(d_m, kMinDistanceM) * frequency_hz / kSpeedOfLight);
}

/// An image source: the mirror of its parent across the line of wall
/// segment `seg`, and the part of that segment (aperture a-b) the parent
/// reaches through its own aperture.
struct Image {
    Point2 p;
    Point2 a, b;
    int seg = 0;
    int parent = -1;  ///< index of the parent image; -1: the antenna
    int order = 1;
};

struct Ant {
    std::string id;
    Point2 pos;      ///< plan-frame metres of the map's floor
    double z = 0.0;  ///< height above the map's floor
    AntennaPattern pattern;
    double eirp_dbm = 0.0;
    int slabs = 0;
    double slab_db = 0.0;
    int group = 0;
    std::vector<Image> images;
};

struct WallInfo {
    int cls = -1;                ///< class index; -1: own attenuation
    double fixed_db = 0.0;       ///< own attenuation per crossing
    std::optional<double> reflection_db;  ///< the class's own reflection loss
};

/// Everything a run needs, built once and read by all threads.
struct Scene {
    std::string floor_id;
    std::vector<double> ax, ay, bx, by;          ///< segment ends
    std::vector<double> minx, miny, maxx, maxy;  ///< boxes, padded to kLanes
    std::vector<int> seg_wall;                   ///< per segment
    std::size_t nseg = 0;                        ///< real segments
    std::vector<WallInfo> walls;
    std::vector<std::string> classes;
    std::vector<Ant> ants;
    std::vector<SourceGroup> groups;
    std::vector<std::string> used, skipped, default_eirp;
    detail::SelectFn select = detail::select_scalar;
    double rx_z = 0.0;
};

/// Per-group values that depend on the frequency.
struct GroupLoss {
    std::vector<double> class_db;  ///< loss per crossing, per class index
};

// Clips segment p-q to the beam of image `im` (beyond its aperture line,
// inside the cone from the image through the aperture). False when
// nothing (or a sliver under 1 um) is left.
bool clip_to_beam(Point2 im, Point2 a, Point2 b, Point2& p, Point2& q) {
    const Point2 ab = sub(b, a);
    const double side_i = cross(ab, sub(im, a));
    const double o = cross(sub(a, im), sub(b, im));
    if (side_i == 0.0 || o == 0.0) return false;
    // Linear functions along p + u (q - p), each must be >= 0.
    auto far_side = [&](Point2 x) { return -(side_i > 0 ? 1.0 : -1.0) * cross(ab, sub(x, a)); };
    auto cone_a = [&](Point2 x) { return (o > 0 ? 1.0 : -1.0) * cross(sub(a, im), sub(x, im)); };
    auto cone_b = [&](Point2 x) { return -(o > 0 ? 1.0 : -1.0) * cross(sub(b, im), sub(x, im)); };
    double u0 = 0.0;
    double u1 = 1.0;
    auto clip = [&](double h0, double h1) {
        if (h0 >= 0.0 && h1 >= 0.0) return;
        if (h0 < 0.0 && h1 < 0.0) {
            u0 = 1.0;
            u1 = 0.0;
            return;
        }
        const double u = h0 / (h0 - h1);
        if (h0 < 0.0)
            u0 = std::max(u0, u);
        else
            u1 = std::min(u1, u);
    };
    clip(far_side(p), far_side(q));
    clip(cone_a(p), cone_a(q));
    clip(cone_b(p), cone_b(q));
    if (!(u1 > u0)) return false;
    const Point2 d = sub(q, p);
    if ((u1 - u0) * norm(d) < 1e-6) return false;
    const Point2 np = add(p, mul(d, u0));
    const Point2 nq = add(p, mul(d, u1));
    p = np;
    q = nq;
    return true;
}

Point2 mirror(Point2 x, Point2 p, Point2 q) {
    const Point2 d = sub(q, p);
    const double t = dot(sub(x, p), d) / dot(d, d);
    const Point2 foot = add(p, mul(d, t));
    return sub(mul(foot, 2.0), x);
}

void build_images(const Scene& s, Ant& ant, int max_order) {
    if (max_order < 1) return;
    auto seg_p = [&](std::size_t i) { return Point2{s.ax[i], s.ay[i]}; };
    auto seg_q = [&](std::size_t i) { return Point2{s.bx[i], s.by[i]}; };
    // Order 1: every segment the antenna is not on.
    for (std::size_t i = 0; i < s.nseg; ++i) {
        const Point2 p = seg_p(i);
        const Point2 q = seg_q(i);
        const Point2 d = sub(q, p);
        const double len = norm(d);
        if (len < 1e-9) continue;
        if (std::fabs(cross(d, sub(ant.pos, p))) / len < 1e-9) continue;
        ant.images.push_back({mirror(ant.pos, p, q), p, q, static_cast<int>(i), -1, 1});
    }
    // Higher orders: segments inside the beam of an image of order k - 1.
    std::size_t begin = 0;
    for (int order = 2; order <= max_order; ++order) {
        const std::size_t end = ant.images.size();
        for (std::size_t k = begin; k < end; ++k) {
            const Image parent = ant.images[k];
            for (std::size_t i = 0; i < s.nseg; ++i) {
                if (static_cast<int>(i) == parent.seg) continue;
                Point2 p = seg_p(i);
                Point2 q = seg_q(i);
                const Point2 d = sub(q, p);
                const double len = norm(d);
                if (len < 1e-9) continue;
                if (std::fabs(cross(d, sub(parent.p, p))) / len < 1e-9) continue;
                if (!clip_to_beam(parent.p, parent.a, parent.b, p, q)) continue;
                ant.images.push_back(
                    {mirror(parent.p, seg_p(i), seg_q(i)), p, q, static_cast<int>(i), static_cast<int>(k), order});
            }
        }
        begin = end;
    }
}

/// True when the reflection of image `im` reaches `r`: r strictly beyond
/// the aperture line and the segment image-r through the aperture.
bool in_beam(const Image& im, Point2 r) {
    const Point2 ab = sub(im.b, im.a);
    const double side_i = cross(ab, sub(im.p, im.a));
    const double side_r = cross(ab, sub(r, im.a));
    if (!((side_i > 0.0 && side_r < 0.0) || (side_i < 0.0 && side_r > 0.0))) return false;
    const double ca = cross(sub(im.a, im.p), sub(r, im.p));
    const double cb = cross(sub(im.b, im.p), sub(r, im.p));
    return (ca >= 0.0 && cb <= 0.0) || (ca <= 0.0 && cb >= 0.0);
}

/// Where the line from `from` to `to` meets the line of segment p-q.
Point2 on_line(Point2 from, Point2 to, Point2 p, Point2 q) {
    const Point2 d = sub(q, p);
    const Point2 r = sub(to, from);
    const double den = cross(r, d);
    if (den == 0.0) return to;
    const double s = cross(sub(p, from), d) / den;
    return add(from, mul(r, s));
}

/// Per-thread scratch space.
struct Work {
    std::vector<std::uint32_t> cand;
    std::vector<std::pair<int, double>> hits;  ///< (wall, t) of one leg
    std::vector<Point2> pts;
    std::vector<int> chain;
};

/// Calls on_wall(wall index) once per wall crossing of the leg p0-p1
/// (building::walls_crossed rules: not at the leg's ends, a polyline
/// once per crossing point).
template <class F>
void leg_walls(const Scene& s, Work& w, Point2 p0, Point2 p1, F&& on_wall) {
    if (s.nseg == 0) return;
    const std::size_t n = s.select(p0.x, p0.y, p1.x, p1.y, s.minx.data(), s.miny.data(), s.maxx.data(),
                                   s.maxy.data(), s.minx.size(), w.cand.data());
    w.hits.clear();
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t i = w.cand[k];
        const double t = detail::crossing(p0, p1, {s.ax[i], s.ay[i]}, {s.bx[i], s.by[i]});
        if (t < 0.0) continue;
        const int wall = s.seg_wall[i];
        bool dup = false;
        for (const auto& h : w.hits)
            if (h.first == wall && std::fabs(h.second - t) <= building::kCrossingEndEps) {
                dup = true;
                break;
            }
        if (dup) continue;
        w.hits.emplace_back(wall, t);
        on_wall(wall);
    }
}

/// The geometry of one ray to r: 2D points from the antenna to r through
/// the reflection points (w.pts), the walls it reflects off (w.chain,
/// last first), the unfolded 3D length and the pattern loss. `image` < 0:
/// the direct ray. False when the image does not reach r.
bool ray_points(const Scene& s, const Ant& ant, int image, Point2 r, Work& w, double& length_m, double& pattern_db) {
    w.pts.clear();
    w.chain.clear();
    Point2 virt = ant.pos;
    if (image >= 0) {
        const Image& im = ant.images[static_cast<std::size_t>(image)];
        if (!in_beam(im, r)) return false;
        virt = im.p;
        for (int k = image; k >= 0; k = ant.images[static_cast<std::size_t>(k)].parent) w.chain.push_back(k);
        // chain: last reflection first. Walk back from r.
        w.pts.resize(w.chain.size() + 2);
        w.pts.back() = r;
        Point2 to = r;
        for (std::size_t j = 0; j < w.chain.size(); ++j) {
            const Image& c = ant.images[static_cast<std::size_t>(w.chain[j])];
            const std::size_t seg = static_cast<std::size_t>(c.seg);
            to = on_line(c.p, to, {s.ax[seg], s.ay[seg]}, {s.bx[seg], s.by[seg]});
            w.pts[w.chain.size() - j] = to;
        }
        w.pts.front() = ant.pos;
    } else {
        w.pts.push_back(ant.pos);
        w.pts.push_back(r);
    }
    const double l2 = norm(sub(r, virt));
    const double dz = s.rx_z - ant.z;
    length_m = std::sqrt(l2 * l2 + dz * dz);
    pattern_db = -building::pattern_gain_db(ant.pattern, building::bearing_deg(ant.pos, w.pts[1]));
    return true;
}

/// The reflection losses of the walls in w.chain: count of bounces at the
/// parameter's loss and the sum of the class-specific ones.
void reflection_terms(const Scene& s, const Ant& ant, const Work& w, int& default_count, double& class_db) {
    default_count = 0;
    class_db = 0.0;
    for (int k : w.chain) {
        const int seg = ant.images[static_cast<std::size_t>(k)].seg;
        const WallInfo& info = s.walls[static_cast<std::size_t>(s.seg_wall[static_cast<std::size_t>(seg)])];
        if (info.reflection_db)
            class_db += *info.reflection_db;
        else
            ++default_count;
    }
}

double group_frequency(const Building& b, const std::optional<double>& given, const std::string& cell_id,
                       const std::string& antenna_id) {
    if (given) return *given;
    const building::Cell* c = cell_id.empty() ? nullptr : b.find_cell(cell_id);
    if (c != nullptr && c->carrier_hz > 0) return static_cast<double>(c->carrier_hz);
    throw std::invalid_argument("antenna '" + antenna_id +
                                "': its cell has no carrier frequency; give ModelParams::frequency_hz");
}

Scene make_scene(const Building& b, const std::string& floor_id, const ModelParams& p, const RunControl& run) {
    const auto problems = p.validate();
    if (!problems.empty()) {
        std::string m = "invalid model parameters:";
        for (const auto& x : problems) m += " " + x + ";";
        throw std::invalid_argument(m);
    }
    const Floor* rf = b.find_floor(floor_id);
    if (rf == nullptr) throw std::invalid_argument("no floor '" + floor_id + "'");
    const auto mpu = b.metres_per_unit(*rf);
    if (!mpu) throw std::logic_error("floor '" + floor_id + "' has no scale");

    Scene s;
    s.floor_id = floor_id;
    s.rx_z = p.rx_height_m;
#if YANTRA_RAYTRACE_SIMD
    if (run.isa == Isa::Simd) s.select = detail::select_simd;
#else
    (void)run;
#endif

    for (std::size_t wi = 0; wi < rf->walls.size(); ++wi) {
        const building::Wall& w = rf->walls[wi];
        WallInfo info;
        const building::WallClass* wc = w.wall_class.empty() ? nullptr : b.find_class(w.wall_class);
        if (!w.wall_class.empty() && wc == nullptr)
            throw std::invalid_argument("wall " + std::to_string(wi) + " of floor '" + floor_id +
                                        "': unknown wall class '" + w.wall_class + "'");
        if (wc != nullptr) info.reflection_db = wc->reflection_loss_db;
        if (w.attenuation_db) {
            info.fixed_db = *w.attenuation_db;
        } else {
            auto it = std::find(s.classes.begin(), s.classes.end(), w.wall_class);
            info.cls = static_cast<int>(it - s.classes.begin());
            if (it == s.classes.end()) s.classes.push_back(w.wall_class);
        }
        s.walls.push_back(info);
        for (std::size_t si = 0; si + 1 < w.points.size(); ++si) {
            s.ax.push_back(w.points[si].x * *mpu);
            s.ay.push_back(w.points[si].y * *mpu);
            s.bx.push_back(w.points[si + 1].x * *mpu);
            s.by.push_back(w.points[si + 1].y * *mpu);
            s.seg_wall.push_back(static_cast<int>(wi));
        }
    }
    s.nseg = s.ax.size();
    for (std::size_t i = 0; i < s.nseg; ++i) {
        s.minx.push_back(std::min(s.ax[i], s.bx[i]));
        s.maxx.push_back(std::max(s.ax[i], s.bx[i]));
        s.miny.push_back(std::min(s.ay[i], s.by[i]));
        s.maxy.push_back(std::max(s.ay[i], s.by[i]));
    }
    while (s.minx.size() % detail::kLanes != 0) {
        const double inf = std::numeric_limits<double>::infinity();
        s.minx.push_back(inf);
        s.miny.push_back(inf);
        s.maxx.push_back(-inf);
        s.maxy.push_back(-inf);
    }

    for (std::size_t fi : b.floors_by_level()) {
        const Floor& af = b.floors[fi];
        const bool same = &af == rf;
        if (!same && !p.other_floors) continue;
        for (const building::Antenna& a : af.antennas) {
            if (a.type == building::AntennaType::OutdoorDonor) {
                s.skipped.push_back(a.id + ": an outdoor donor direction is not traced");
                continue;
            }
            std::optional<Point2> pos;
            if (same) {
                pos = b.to_plan_metres(af, a.position);
            } else if (auto bm = b.to_building_metres(af, a.position)) {
                if (auto plan = b.from_building_metres(*rf, *bm)) pos = b.to_plan_metres(*rf, *plan);
            }
            if (!pos) {
                s.skipped.push_back(a.id + ": floor '" + af.id + "' has no scale");
                continue;
            }
            Ant ant;
            ant.id = a.id;
            ant.pos = *pos;
            ant.z = level_above_m(b, *rf, af.level) + a.height_m;
            ant.pattern = a.pattern;
            ant.slabs = std::abs(af.level - rf->level);
            ant.slab_db = b.inter_floor_attenuation_db(af.level, rf->level);
            if (auto ov = p.antenna_eirp_dbm.find(a.id); ov != p.antenna_eirp_dbm.end()) {
                ant.eirp_dbm = ov->second;
            } else if (auto e = a.effective_eirp_dbm()) {
                ant.eirp_dbm = *e;
            } else {
                ant.eirp_dbm = p.default_eirp_dbm;
                s.default_eirp.push_back(a.id);
            }
            const building::Cell* cell = a.cell.empty() ? nullptr : b.find_cell(a.cell);
            const std::string key = cell != nullptr ? "cell:" + cell->id : "antenna:" + a.id;
            auto g = std::find_if(s.groups.begin(), s.groups.end(), [&](const SourceGroup& x) { return x.key == key; });
            if (g == s.groups.end()) {
                SourceGroup sg;
                sg.key = key;
                sg.cell_id = cell != nullptr ? cell->id : "";
                sg.label = cell != nullptr ? (cell->label.empty() ? cell->id : cell->label)
                                           : (a.name.empty() ? a.id : a.name);
                sg.frequency_hz = group_frequency(b, p.frequency_hz, sg.cell_id, a.id);
                s.groups.push_back(std::move(sg));
                g = s.groups.end() - 1;
            }
            g->antenna_ids.push_back(a.id);
            ant.group = static_cast<int>(g - s.groups.begin());
            s.used.push_back(a.id);
            s.ants.push_back(std::move(ant));
        }
    }
    for (Ant& ant : s.ants) build_images(s, ant, p.max_reflections);
    return s;
}

/// The class losses of every group at its frequency.
std::vector<GroupLoss> group_losses(const Building& b, const std::vector<std::string>& classes,
                                    const std::vector<SourceGroup>& groups, const ModelParams& p) {
    std::vector<GroupLoss> out;
    for (const SourceGroup& g : groups) {
        GroupLoss gl;
        for (const std::string& id : classes) {
            if (auto ov = p.wall_class_loss_db.find(id); ov != p.wall_class_loss_db.end()) {
                gl.class_db.push_back(ov->second);
                continue;
            }
            const building::WallClass* c = b.find_class(id);
            if (c == nullptr) throw std::invalid_argument("unknown wall class '" + id + "'");
            gl.class_db.push_back(c->attenuation_at_db(g.frequency_hz / 1e6));
        }
        out.push_back(std::move(gl));
    }
    return out;
}

double to_dbm(double mw) { return mw > 0.0 ? 10.0 * std::log10(mw) : kNoLevel; }

void best_of(const std::vector<std::vector<double>>& levels, std::size_t n, std::vector<double>& best,
             std::vector<int>& best_group) {
    best.assign(n, kNoLevel);
    best_group.assign(n, -1);
    for (std::size_t g = 0; g < levels.size(); ++g)
        for (std::size_t i = 0; i < n; ++i)
            if (levels[g][i] > best[i]) {
                best[i] = levels[g][i];
                best_group[i] = static_cast<int>(g);
            }
}

/// Lowest and highest column of grid row y where the beam of `im` can be
/// (a superset; in_beam() decides per cell).
bool beam_columns(const Image& im, const Grid& g, double y, int& c0, int& c1) {
    double lo = -std::numeric_limits<double>::infinity();
    double hi = std::numeric_limits<double>::infinity();
    // Each constraint is k0 + k1 x >= 0.
    auto limit = [&](double k0, double k1) {
        if (k1 == 0.0) {
            if (k0 < 0.0) {
                lo = 1.0;
                hi = 0.0;
            }
            return;
        }
        const double x = -k0 / k1;
        if (k1 > 0.0)
            lo = std::max(lo, x);
        else
            hi = std::min(hi, x);
    };
    const Point2 ab = sub(im.b, im.a);
    const double side_i = cross(ab, sub(im.p, im.a));
    const double o = cross(sub(im.a, im.p), sub(im.b, im.p));
    if (side_i == 0.0 || o == 0.0) return false;
    const double sg = side_i > 0 ? -1.0 : 1.0;  // far side
    // cross(ab, (x, y) - a) = ab.x (y - a.y) - ab.y (x - a.x)
    limit(sg * (ab.x * (y - im.a.y) + ab.y * im.a.x), sg * -ab.y);
    const double so = o > 0 ? 1.0 : -1.0;
    const Point2 va = sub(im.a, im.p);
    const Point2 vb = sub(im.b, im.p);
    limit(so * (va.x * (y - im.p.y) + va.y * im.p.x), so * -va.y);
    limit(-so * (vb.x * (y - im.p.y) + vb.y * im.p.x), -so * -vb.y);
    if (!(hi >= lo)) return false;
    const double first = std::floor((lo - g.origin_m.x) / g.cell_m - 0.5) - 1.0;
    const double last = std::ceil((hi - g.origin_m.x) / g.cell_m - 0.5) + 1.0;
    if (last < 0.0 || first > g.cols - 1) return false;
    c0 = static_cast<int>(std::max(first, 0.0));
    c1 = static_cast<int>(std::min(last, static_cast<double>(g.cols - 1)));
    return c0 <= c1;
}

}  // namespace

double path_loss_db(double d_m, double frequency_hz, double exponent, double d0_m) {
    if (d_m <= d0_m) return free_space_db(d_m, frequency_hz);
    return free_space_db(d0_m, frequency_hz) + 10.0 * exponent * std::log10(d_m / d0_m);
}

double level_above_m(const Building& b, const Floor& f, int level) {
    const int lo = std::min(f.level, level);
    const int hi = std::max(f.level, level);
    double m = 0.0;
    for (int k = lo; k < hi; ++k) {
        const Floor* at = b.floor_at_level(k);
        m += at != nullptr ? at->height_m : f.height_m;
    }
    return level > f.level ? m : -m;
}

std::vector<std::string> ModelParams::validate() const {
    std::vector<std::string> out;
    if (frequency_hz && (!std::isfinite(*frequency_hz) || *frequency_hz <= 0.0))
        out.push_back("frequency_hz: must be > 0");
    check_range(out, "grid_m", grid_m, 0.05, 10.0);
    if (max_reflections < 0 || max_reflections > kMaxReflections)
        out.push_back("max_reflections: " + std::to_string(max_reflections) + " is outside 0 .. " +
                      std::to_string(kMaxReflections));
    check_range(out, "rx_height_m", rx_height_m, 0.0, 10.0);
    check_range(out, "path_loss_exponent", path_loss_exponent, 1.0, 8.0);
    check_range(out, "reference_distance_m", reference_distance_m, 0.01, 100.0);
    check_range(out, "reflection_loss_db", reflection_loss_db, 0.0, 60.0);
    if (floor_loss_db) check_range(out, "floor_loss_db", *floor_loss_db, 0.0, 100.0);
    for (const auto& [id, v] : wall_class_loss_db) check_range(out, "wall_class_loss_db[" + id + "]", v, 0.0, 100.0);
    for (const auto& [id, v] : antenna_eirp_dbm) check_range(out, "antenna_eirp_dbm[" + id + "]", v, -50.0, 80.0);
    check_range(out, "default_eirp_dbm", default_eirp_dbm, -50.0, 80.0);
    return out;
}

bool simd_built() noexcept { return YANTRA_RAYTRACE_SIMD != 0; }

std::optional<std::size_t> Grid::cell_at(Point2 m) const {
    const double cx = std::floor((m.x - origin_m.x) / cell_m);
    const double cy = std::floor((m.y - origin_m.y) / cell_m);
    if (!(cx >= 0.0 && cy >= 0.0 && cx < cols && cy < rows)) return std::nullopt;
    return index(static_cast<int>(cx), static_cast<int>(cy));
}

Grid floor_grid(const Building& b, const Floor& f, double cell_m) {
    const auto mpu = b.metres_per_unit(f);
    if (!mpu) throw std::logic_error("floor '" + f.id + "' has no scale");
    if (!std::isfinite(cell_m) || cell_m < 0.05 || cell_m > 10.0)
        throw std::invalid_argument("grid cell size " + fmt(cell_m) + " m is outside 0.05 .. 10 m");
    Point2 lo;
    Point2 hi;
    if (f.plan_width > 0.0 && f.plan_height > 0.0) {
        hi = {f.plan_width * *mpu, f.plan_height * *mpu};
    } else {
        bool any = false;
        auto take = [&](Point2 plan) {
            const Point2 m{plan.x * *mpu, plan.y * *mpu};
            if (!any) {
                lo = hi = m;
                any = true;
            }
            lo = {std::min(lo.x, m.x), std::min(lo.y, m.y)};
            hi = {std::max(hi.x, m.x), std::max(hi.y, m.y)};
        };
        for (const auto& w : f.walls)
            for (const auto& pt : w.points) take(pt);
        for (const auto& a : f.antennas) take(a.position);
        if (!any) throw std::invalid_argument("floor '" + f.id + "' has no plan size, walls or antennas");
        lo = {lo.x - 1.0, lo.y - 1.0};
        hi = {hi.x + 1.0, hi.y + 1.0};
    }
    Grid g;
    g.origin_m = lo;
    g.cell_m = cell_m;
    const double cols = std::ceil((hi.x - lo.x) / cell_m - 1e-9);
    const double rows = std::ceil((hi.y - lo.y) / cell_m - 1e-9);
    if (!(cols >= 1.0 && rows >= 1.0) || cols * rows > static_cast<double>(kMaxGridCells))
        throw std::invalid_argument("grid of floor '" + f.id + "' would have " + fmt(cols) + " x " + fmt(rows) +
                                    " cells (1 .. " + std::to_string(kMaxGridCells) + ")");
    g.cols = static_cast<int>(cols);
    g.rows = static_cast<int>(rows);
    return g;
}

CoverageMap compute_map(const Building& b, const std::string& floor_id, const ModelParams& p, const RunControl& run) {
    const Scene s = make_scene(b, floor_id, p, run);
    const std::vector<GroupLoss> gl = group_losses(b, s.classes, s.groups, p);
    CoverageMap m;
    m.floor_id = floor_id;
    m.grid = floor_grid(b, *b.find_floor(floor_id), p.grid_m);
    m.params = p;
    m.groups = s.groups;
    m.antennas_used = s.used;
    m.skipped = s.skipped;
    m.default_eirp = s.default_eirp;
    const Grid& g = m.grid;
    const std::size_t ng = s.groups.size();
    std::vector<double> power(ng * g.size(), 0.0);  // mW, [group][cell]
    std::atomic<std::size_t> rays{0};

    const bool complete = run_parallel(run, static_cast<std::size_t>(g.rows), [&](std::size_t row_index) {
        const int row = static_cast<int>(row_index);
        Work w;
        w.cand.resize(s.minx.size() + detail::kLanes);
        std::size_t count = 0;
        const double y = g.centre(0, row).y;
        auto add_ray = [&](const Ant& ant, int image, int col) {
            const Point2 r = g.centre(col, row);
            double len = 0.0;
            double pattern = 0.0;
            if (!ray_points(s, ant, image, r, w, len, pattern)) return;
            const GroupLoss& loss_of = gl[static_cast<std::size_t>(ant.group)];
            double walls = 0.0;
            for (std::size_t k = 0; k + 1 < w.pts.size(); ++k)
                leg_walls(s, w, w.pts[k], w.pts[k + 1], [&](int wall) {
                    const WallInfo& info = s.walls[static_cast<std::size_t>(wall)];
                    walls += info.cls < 0 ? info.fixed_db : loss_of.class_db[static_cast<std::size_t>(info.cls)];
                });
            int default_refl = 0;
            double class_refl = 0.0;
            reflection_terms(s, ant, w, default_refl, class_refl);
            const double slab = p.floor_loss_db ? ant.slabs * *p.floor_loss_db : ant.slab_db;
            const double f = s.groups[static_cast<std::size_t>(ant.group)].frequency_hz;
            const double loss = path_loss_db(len, f, p.path_loss_exponent, p.reference_distance_m) + walls +
                                default_refl * p.reflection_loss_db + class_refl + pattern + slab;
            power[static_cast<std::size_t>(ant.group) * g.size() + g.index(col, row)] +=
                std::pow(10.0, (ant.eirp_dbm - loss) / 10.0);
            ++count;
        };
        for (const Ant& ant : s.ants) {
            for (int col = 0; col < g.cols; ++col) add_ray(ant, -1, col);
            for (std::size_t i = 0; i < ant.images.size(); ++i) {
                int c0 = 0;
                int c1 = -1;
                if (!beam_columns(ant.images[i], g, y, c0, c1)) continue;
                for (int col = c0; col <= c1; ++col) add_ray(ant, static_cast<int>(i), col);
            }
        }
        rays.fetch_add(count);
    });
    m.cancelled = !complete;
    m.rays = rays.load();
    m.level_dbm.assign(ng, std::vector<double>(g.size(), kNoLevel));
    for (std::size_t k = 0; k < ng; ++k)
        for (std::size_t i = 0; i < g.size(); ++i) m.level_dbm[k][i] = to_dbm(power[k * g.size() + i]);
    best_of(m.level_dbm, g.size(), m.best_dbm, m.best_group);
    return m;
}

PathSet trace_points(const Building& b, const std::string& floor_id, const std::vector<Point2>& points_m,
                     const ModelParams& p, const RunControl& run) {
    const Scene s = make_scene(b, floor_id, p, run);
    PathSet out;
    out.floor_id = floor_id;
    out.points_m = points_m;
    out.groups = s.groups;
    out.wall_classes = s.classes;
    out.skipped = s.skipped;
    for (const Ant& a : s.ants) {
        out.antennas.push_back(a.id);
        out.antenna_group.push_back(a.group);
    }
    out.rays.resize(points_m.size());
    const bool complete = run_parallel(run, points_m.size(), [&](std::size_t pi) {
        Work w;
        w.cand.resize(s.minx.size() + detail::kLanes);
        const Point2 r = points_m[pi];
        auto& rays = out.rays[pi];
        for (std::size_t ai = 0; ai < s.ants.size(); ++ai) {
            const Ant& ant = s.ants[ai];
            auto add_ray = [&](int image) {
                RayTerm t;
                if (!ray_points(s, ant, image, r, w, t.length_m, t.pattern_loss_db)) return;
                t.antenna = static_cast<int>(ai);
                t.reflections = static_cast<int>(w.pts.size()) - 2;
                reflection_terms(s, ant, w, t.default_reflections, t.class_reflection_db);
                t.slabs = ant.slabs;
                t.slab_db = ant.slab_db;
                for (std::size_t k = 0; k + 1 < w.pts.size(); ++k)
                    leg_walls(s, w, w.pts[k], w.pts[k + 1], [&](int wall) {
                        const WallInfo& info = s.walls[static_cast<std::size_t>(wall)];
                        if (info.cls < 0) {
                            t.fixed_wall_db += info.fixed_db;
                            return;
                        }
                        auto it = std::find_if(t.class_crossings.begin(), t.class_crossings.end(),
                                               [&](const auto& c) { return c.first == info.cls; });
                        if (it == t.class_crossings.end())
                            t.class_crossings.emplace_back(info.cls, 1);
                        else
                            ++it->second;
                    });
                rays.push_back(std::move(t));
            };
            add_ray(-1);
            for (std::size_t i = 0; i < ant.images.size(); ++i) add_ray(static_cast<int>(i));
        }
    });
    out.cancelled = !complete;
    return out;
}

std::vector<double> group_frequencies(const Building& b, const PathSet& s, const ModelParams& p) {
    std::vector<double> out;
    for (const SourceGroup& g : s.groups)
        out.push_back(group_frequency(b, p.frequency_hz, g.cell_id,
                                      g.antenna_ids.empty() ? g.key : g.antenna_ids.front()));
    return out;
}

PointLevels evaluate(const Building& b, const PathSet& s, const ModelParams& p) {
    const auto problems = p.validate();
    if (!problems.empty()) throw std::invalid_argument("invalid model parameters: " + problems.front());
    // The groups' frequencies: the given one, else each cell's carrier.
    std::vector<SourceGroup> groups = s.groups;
    const std::vector<double> freq = group_frequencies(b, s, p);
    for (std::size_t k = 0; k < groups.size(); ++k) groups[k].frequency_hz = freq[k];
    const std::vector<GroupLoss> gl = group_losses(b, s.wall_classes, groups, p);
    std::vector<double> eirp;
    for (const std::string& id : s.antennas) {
        const auto [floor, a] = b.find_antenna(id);
        if (auto ov = p.antenna_eirp_dbm.find(id); ov != p.antenna_eirp_dbm.end())
            eirp.push_back(ov->second);
        else if (a != nullptr && a->effective_eirp_dbm())
            eirp.push_back(*a->effective_eirp_dbm());
        else
            eirp.push_back(p.default_eirp_dbm);
    }
    const std::size_t n = s.points_m.size();
    std::vector<std::vector<double>> power(groups.size(), std::vector<double>(n, 0.0));
    for (std::size_t pi = 0; pi < s.rays.size(); ++pi)
        for (const RayTerm& r : s.rays[pi]) {
            const std::size_t ai = static_cast<std::size_t>(r.antenna);
            const std::size_t g = static_cast<std::size_t>(s.antenna_group[ai]);
            double walls = r.fixed_wall_db;
            for (const auto& [cls, count] : r.class_crossings)
                walls += count * gl[g].class_db[static_cast<std::size_t>(cls)];
            const double slab = p.floor_loss_db ? r.slabs * *p.floor_loss_db : r.slab_db;
            const double loss = path_loss_db(r.length_m, groups[g].frequency_hz, p.path_loss_exponent,
                                             p.reference_distance_m) +
                                walls + r.default_reflections * p.reflection_loss_db + r.class_reflection_db +
                                r.pattern_loss_db + slab;
            power[g][pi] += std::pow(10.0, (eirp[ai] - loss) / 10.0);
        }
    PointLevels out;
    out.level_dbm.assign(groups.size(), std::vector<double>(n, kNoLevel));
    for (std::size_t g = 0; g < groups.size(); ++g)
        for (std::size_t i = 0; i < n; ++i) out.level_dbm[g][i] = to_dbm(power[g][i]);
    best_of(out.level_dbm, n, out.best_dbm, out.best_group);
    return out;
}

}  // namespace yantra::raytrace
