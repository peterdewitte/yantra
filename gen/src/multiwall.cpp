// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Indoor multi-wall model (RAD-73); see multiwall.hpp and
// docs/CHANNEL_MODEL.md. Segment intersection and wall-crossing rules
// copied from yantra 68ea20a (core/src/building/geometry.cpp), the pattern
// formula from core/src/building/building.cpp; reflections, floors and
// tables added; scene 1.1 polylines, per-band walls and storeys follow
// yantra 07a3da6 (core/src/building/geometry.cpp walls_crossed,
// model.cpp WallClass::attenuation_at_db).

#include "yantra/gen/multiwall.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace yantra::gen {

namespace {

constexpr double kEndEps = 1e-9;

double cross(PlanPoint a, PlanPoint b) { return a.x_m * b.y_m - a.y_m * b.x_m; }
PlanPoint sub(PlanPoint a, PlanPoint b) { return {a.x_m - b.x_m, a.y_m - b.y_m}; }
double length(PlanPoint a, PlanPoint b) { return std::hypot(b.x_m - a.x_m, b.y_m - a.y_m); }

PlanPoint wall_start(const radios::SceneWall& w) { return {w.x1_m, w.y1_m}; }
PlanPoint wall_end(const radios::SceneWall& w) { return {w.x2_m, w.y2_m}; }

// Mirror image of p across the line through wall w; nullopt for a wall of
// zero length.
std::optional<PlanPoint> mirror(PlanPoint p, const radios::SceneWall& w)
{
    const PlanPoint a = wall_start(w);
    const PlanPoint d = sub(wall_end(w), a);
    const double dd = d.x_m * d.x_m + d.y_m * d.y_m;
    if (!(dd > 0.0))
        return std::nullopt;
    const double t = ((p.x_m - a.x_m) * d.x_m + (p.y_m - a.y_m) * d.y_m) / dd;
    const PlanPoint foot{a.x_m + t * d.x_m, a.y_m + t * d.y_m};
    return PlanPoint{2.0 * foot.x_m - p.x_m, 2.0 * foot.y_m - p.y_m};
}

// The point where the path from `from` to `to` hits wall w strictly inside
// the path; nullopt otherwise.
std::optional<PlanPoint> hit(PlanPoint from, PlanPoint to, const radios::SceneWall& w)
{
    const auto t = segment_intersection(from, to, wall_start(w), wall_end(w));
    if (!t || *t <= kEndEps || *t >= 1.0 - kEndEps)
        return std::nullopt;
    return PlanPoint{from.x_m + (to.x_m - from.x_m) * *t, from.y_m + (to.y_m - from.y_m) * *t};
}

double power_sum_dbm(const std::vector<double>& levels)
{
    double p = 0.0;
    for (double l : levels)
        p += std::pow(10.0, l / 10.0);
    return p > 0.0 ? 10.0 * std::log10(p) : -std::numeric_limits<double>::infinity();
}

}  // namespace

double bearing_deg(PlanPoint a, PlanPoint b)
{
    const double deg = std::atan2(b.x_m - a.x_m, -(b.y_m - a.y_m)) * 180.0 / std::numbers::pi;
    return deg < 0.0 ? deg + 360.0 : deg;
}

double pattern_gain_db(const radios::ScenePattern& p, double bearing)
{
    using Type = radios::ScenePattern::Type;
    if (p.type == Type::omni)
        return 0.0;
    double off = std::fmod(bearing - p.azimuth_deg, 360.0);
    if (off < 0.0)
        off += 360.0;  // clockwise from the azimuth, [0, 360)
    if (p.type == Type::sector) {
        const double a = off > 180.0 ? 360.0 - off : off;
        const double loss = 12.0 * (a / p.beamwidth_deg) * (a / p.beamwidth_deg);
        return -std::min(loss, p.front_to_back_db);
    }
    // Table: entry k at k * step clockwise from the azimuth; linear in
    // between, and from the last entry round to the first (at 360).
    const size_t n = p.gain_db.size();
    if (n == 0)
        return 0.0;
    const size_t k = std::min(static_cast<size_t>(std::floor(off / p.step_deg)), n - 1);
    const double start = static_cast<double>(k) * p.step_deg;
    const double width = k + 1 < n ? p.step_deg : 360.0 - start;
    const double next = k + 1 < n ? p.gain_db[k + 1] : p.gain_db[0];
    const double f = width > 0.0 ? std::clamp((off - start) / width, 0.0, 1.0) : 0.0;
    return p.gain_db[k] + (next - p.gain_db[k]) * f;
}

std::optional<double> segment_intersection(PlanPoint p0, PlanPoint p1, PlanPoint q0, PlanPoint q1)
{
    const PlanPoint r = sub(p1, p0);
    const PlanPoint s = sub(q1, q0);
    const double denom = cross(r, s);
    const double scale =
        std::max({std::fabs(r.x_m), std::fabs(r.y_m), std::fabs(s.x_m), std::fabs(s.y_m), 1e-300});
    if (std::fabs(denom) <= 1e-12 * scale * scale)
        return std::nullopt;  // parallel or collinear
    const PlanPoint qp = sub(q0, p0);
    const double t = cross(qp, s) / denom;
    const double u = cross(qp, r) / denom;
    const double eps = 1e-12;
    if (t < -eps || t > 1.0 + eps || u < -eps || u > 1.0 + eps)
        return std::nullopt;
    return std::clamp(t, 0.0, 1.0);
}

WallCrossings walls_crossed(const radios::Scene& scene, PlanPoint a, PlanPoint b, const std::vector<size_t>& skip,
                            double frequency_hz)
{
    WallCrossings result;
    for (size_t i = 0; i < scene.walls.size(); ++i) {
        if (std::find(skip.begin(), skip.end(), i) != skip.end())
            continue;
        const radios::SceneWall& w = scene.walls[i];
        const auto t = segment_intersection(a, b, wall_start(w), wall_end(w));
        if (!t || *t <= kEndEps || *t >= 1.0 - kEndEps)
            continue;
        result.crossings.push_back({i, *t, w.attenuation_at_db(frequency_hz)});
    }
    std::stable_sort(result.crossings.begin(), result.crossings.end(),
                     [](const WallCrossing& x, const WallCrossing& y) { return x.t < y.t; });
    // A wall drawn as several collinear pieces is crossed once where the
    // path passes exactly through the joint of two pieces, and a polyline
    // once where the path passes exactly through one of its vertices.
    std::vector<WallCrossing> kept;
    for (const WallCrossing& c : result.crossings) {
        const PlanPoint dc = sub(wall_end(scene.walls[c.wall]), wall_start(scene.walls[c.wall]));
        const std::string& polyline = scene.walls[c.wall].polyline;
        const bool joint = std::any_of(kept.begin(), kept.end(), [&](const WallCrossing& k) {
            if (std::fabs(k.t - c.t) > kEndEps)
                return false;
            if (!polyline.empty() && scene.walls[k.wall].polyline == polyline)
                return true;
            const PlanPoint dk = sub(wall_end(scene.walls[k.wall]), wall_start(scene.walls[k.wall]));
            const double scale = std::hypot(dc.x_m, dc.y_m) * std::hypot(dk.x_m, dk.y_m);
            return std::fabs(cross(dc, dk)) <= 1e-9 * scale;
        });
        if (!joint)
            kept.push_back(c);
    }
    result.crossings = std::move(kept);
    for (const auto& c : result.crossings)
        result.total_db += c.attenuation_db;
    return result;
}

MultiWallModel::MultiWallModel(MultiWallParams params) : params_(params)
{
    const auto finite = [](double v) { return std::isfinite(v); };
    if (!finite(params.receiver_height_m) || !(params.slab_attenuation_db >= 0.0) ||
        !finite(params.slab_attenuation_db) || !(params.storey_height_m > 0.0) || !finite(params.storey_height_m) ||
        params.reflections < 0 || params.reflections > 2 || !(params.reflection_loss_db >= 0.0) ||
        !finite(params.reflection_loss_db) || !(params.shadowing_sigma_db >= 0.0) ||
        !finite(params.shadowing_sigma_db) || !(params.shadowing_decorrelation_m > 0.0) ||
        !finite(params.shadowing_decorrelation_m))
        throw std::invalid_argument("multi-wall model: parameter out of range");
}

double MultiWallModel::shadowing(const std::string& antenna, PlanPoint rx) const
{
    if (params_.shadowing_sigma_db == 0.0)
        return 0.0;
    auto it = fields_.find(antenna);
    if (it == fields_.end()) {
        const uint64_t seed = derive_seed(params_.seed, {string_tag("multiwall"), string_tag(antenna.c_str())});
        it = fields_.emplace(antenna, ShadowingField(params_.shadowing_sigma_db,
                                                     params_.shadowing_decorrelation_m, seed))
                 .first;
    }
    return it->second.value(rx.x_m, rx.y_m);
}

AntennaContribution MultiWallModel::antenna(const radios::Scene& scene, const radios::SceneAntenna& a, PlanPoint rx,
                                            double frequency_hz) const
{
    if (!(frequency_hz > 0.0) || !std::isfinite(rx.x_m) || !std::isfinite(rx.y_m))
        throw std::invalid_argument("multi-wall model: frequency must be > 0 and the position finite");
    const double storey = scene.floor.storey_height_m.value_or(params_.storey_height_m);
    const double slab = scene.floor.slab_attenuation_db.value_or(params_.slab_attenuation_db);
    const PlanPoint ap{a.x_m, a.y_m};
    // The antenna's floor: its own level and slab sum when the scene lists
    // it (1.1 storeys), else uniform storeys.
    const radios::SceneStorey* level = a.floor_offset != 0 ? scene.floor.find_storey(a.floor_offset) : nullptr;
    const double floor_level_m = level ? level->level_m : static_cast<double>(a.floor_offset) * storey;
    const double dz = floor_level_m + a.height_m - params_.receiver_height_m;

    AntennaContribution c;
    c.antenna = a.id;
    c.cell = a.cell;
    c.floor_loss_db = level ? level->slab_attenuation_db : std::abs(a.floor_offset) * slab;
    c.shadowing_db = shadowing(a.id, rx);

    auto add_path = [&](int bounces, std::vector<size_t> reflected, double length_2d, PlanPoint first,
                        double wall_loss, int walls) {
        PathContribution p;
        p.bounces = bounces;
        p.length_m = std::sqrt(length_2d * length_2d + dz * dz);
        p.fspl_db = free_space_path_loss_db(p.length_m, frequency_hz);
        p.wall_loss_db = wall_loss;
        p.walls = walls;
        for (size_t w : reflected) {
            const radios::SceneWallClass* cls = scene.find_wall_class(scene.walls[w].wall_class);
            p.reflection_loss_db +=
                cls && cls->reflection_loss_db ? *cls->reflection_loss_db : params_.reflection_loss_db;
        }
        p.reflected = std::move(reflected);
        // The direction at the antenna; straight up or down when the
        // receiver is right below or above it (any bearing, use 0).
        const double bearing = length(ap, first) > 0.0 ? bearing_deg(ap, first) : 0.0;
        p.pattern_db = pattern_gain_db(a.pattern, bearing);
        p.level_dbm = a.eirp_dbm + p.pattern_db - p.fspl_db - p.wall_loss_db - p.reflection_loss_db;
        c.paths.push_back(std::move(p));
    };

    // Direct path.
    const WallCrossings direct = walls_crossed(scene, ap, rx, {}, frequency_hz);
    add_path(0, {}, length(ap, rx), rx, direct.total_db, static_cast<int>(direct.crossings.size()));
    c.distance_m = c.paths.front().length_m;

    // Image method: one bounce off wall i, then two bounces off i and j.
    if (params_.reflections >= 1) {
        for (size_t i = 0; i < scene.walls.size(); ++i) {
            const auto img = mirror(ap, scene.walls[i]);
            if (!img)
                continue;
            const auto p1 = hit(*img, rx, scene.walls[i]);
            if (!p1)
                continue;
            const WallCrossings l1 = walls_crossed(scene, ap, *p1, {i}, frequency_hz);
            const WallCrossings l2 = walls_crossed(scene, *p1, rx, {i}, frequency_hz);
            add_path(1, {i}, length(*img, rx), *p1, l1.total_db + l2.total_db,
                     static_cast<int>(l1.crossings.size() + l2.crossings.size()));
        }
    }
    if (params_.reflections >= 2) {
        for (size_t i = 0; i < scene.walls.size(); ++i) {
            const auto img1 = mirror(ap, scene.walls[i]);
            if (!img1)
                continue;
            for (size_t j = 0; j < scene.walls.size(); ++j) {
                if (j == i)
                    continue;
                const auto img2 = mirror(*img1, scene.walls[j]);
                if (!img2)
                    continue;
                const auto p2 = hit(*img2, rx, scene.walls[j]);
                if (!p2)
                    continue;
                const auto p1 = hit(*img1, *p2, scene.walls[i]);
                if (!p1)
                    continue;
                const WallCrossings l1 = walls_crossed(scene, ap, *p1, {i}, frequency_hz);
                const WallCrossings l2 = walls_crossed(scene, *p1, *p2, {i, j}, frequency_hz);
                const WallCrossings l3 = walls_crossed(scene, *p2, rx, {j}, frequency_hz);
                add_path(2, {i, j}, length(*img2, rx), *p1, l1.total_db + l2.total_db + l3.total_db,
                         static_cast<int>(l1.crossings.size() + l2.crossings.size() + l3.crossings.size()));
            }
        }
    }

    std::vector<double> levels;
    levels.reserve(c.paths.size());
    for (const PathContribution& p : c.paths)
        levels.push_back(p.level_dbm);
    c.level_dbm = power_sum_dbm(levels) - c.floor_loss_db - c.shadowing_db;
    return c;
}

CellLevel MultiWallModel::cell(const radios::Scene& scene, const std::string& cell_id, PlanPoint rx) const
{
    const radios::SceneCell* sc = scene.find_cell(cell_id);
    if (!sc)
        throw std::invalid_argument("multi-wall model: the scene has no cell \"" + cell_id + "\"");
    CellLevel out;
    out.cell = sc->id;
    out.carrier_hz = sc->carrier_hz;
    std::vector<double> levels;
    for (const radios::SceneAntenna& a : scene.antennas) {
        if (a.cell != cell_id)
            continue;
        out.antennas.push_back(antenna(scene, a, rx, static_cast<double>(sc->carrier_hz)));
        levels.push_back(out.antennas.back().level_dbm);
        if (out.antennas.back().level_dbm > out.antennas[out.best].level_dbm)
            out.best = out.antennas.size() - 1;
    }
    out.level_dbm = power_sum_dbm(levels);
    return out;
}

std::vector<CellLevel> MultiWallModel::cells(const radios::Scene& scene, PlanPoint rx) const
{
    std::vector<CellLevel> out;
    out.reserve(scene.cells.size());
    for (const radios::SceneCell& c : scene.cells)
        out.push_back(cell(scene, c.id, rx));
    return out;
}

}  // namespace yantra::gen
