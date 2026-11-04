// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building model (IND-04, IND-06..12), see model.h. Copied from yantra-v2
// 68ea20a: core/src/building/building.cpp, adapted (per-band wall-class
// table, scale methods, plan sources and trims as data, cells by named key
// values, plan-frame metres for RAD-18/RAD-36).

#include "yantra/core/building/model.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <set>

namespace yantra::building {

namespace {

bool finite(double v) { return std::isfinite(v); }
bool finite(Point2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

double wrap360(double deg) {
    double d = std::fmod(deg, 360.0);
    if (d < 0.0) d += 360.0;
    return d >= 360.0 ? 0.0 : d;
}

}  // namespace

std::vector<WallClass> default_wall_classes() {
    auto c = [](const char* id, const char* name, double at400, double at900) {
        return WallClass{id, name, {{400.0, at400}, {900.0, at900}}, std::nullopt};
    };
    return {
        c("concrete", "Concrete", 12.0, 23.0),
        c("brick", "Brick", 8.0, 10.0),
        c("plasterboard", "Plasterboard", 3.0, 6.0),
        c("glass", "Glass", 2.0, 4.5),
        c("wood", "Wood", 3.0, 6.5),
        c("door", "Door", 4.0, 9.0),
        c("metal", "Metal", 25.0, 25.0),
        c("coated_glass", "Coated glass", 20.0, 20.0),
    };
}

double WallClass::attenuation_at_db(double mhz) const {
    if (bands.empty()) return 0.0;
    if (!(mhz > 0.0) || mhz <= bands.front().frequency_mhz) return bands.front().attenuation_db;
    if (mhz >= bands.back().frequency_mhz) return bands.back().attenuation_db;
    for (std::size_t i = 1; i < bands.size(); ++i) {
        const BandAttenuation& lo = bands[i - 1];
        const BandAttenuation& hi = bands[i];
        if (mhz <= hi.frequency_mhz) {
            const double t = std::log(mhz / lo.frequency_mhz) / std::log(hi.frequency_mhz / lo.frequency_mhz);
            return lo.attenuation_db + t * (hi.attenuation_db - lo.attenuation_db);
        }
    }
    return bands.back().attenuation_db;
}

double bearing_deg(Point2 a, Point2 b) {
    return wrap360(std::atan2(b.x - a.x, -(b.y - a.y)) * 180.0 / std::numbers::pi);
}

double pattern_gain_db(const AntennaPattern& p, double bearing) {
    switch (p.type) {
    case AntennaPattern::Type::Omni:
        return 0.0;
    case AntennaPattern::Type::Sector: {
        double off = std::fmod(std::fabs(bearing - p.azimuth_deg), 360.0);
        if (off > 180.0) off = 360.0 - off;
        const double r = off / p.beamwidth_deg;
        return -std::min(12.0 * r * r, p.front_to_back_db);
    }
    case AntennaPattern::Type::Table: {
        // Entry k at k * step clockwise from the azimuth; linear in between,
        // and from the last entry round to the first (at 360 degrees).
        const std::size_t n = p.gain_db.size();
        if (n == 0 || !(p.step_deg > 0.0)) return 0.0;
        const double off = wrap360(bearing - p.azimuth_deg);
        const std::size_t k = std::min(static_cast<std::size_t>(std::floor(off / p.step_deg)), n - 1);
        const double start = static_cast<double>(k) * p.step_deg;
        const double width = k + 1 < n ? p.step_deg : 360.0 - start;
        const double next = k + 1 < n ? p.gain_db[k + 1] : p.gain_db[0];
        const double t = width > 0.0 ? std::clamp((off - start) / width, 0.0, 1.0) : 0.0;
        return p.gain_db[k] + (next - p.gain_db[k]) * t;
    }
    }
    return 0.0;
}

std::optional<double> Antenna::effective_eirp_dbm() const {
    if (eirp_dbm) return eirp_dbm;
    if (feed_power_dbm) return *feed_power_dbm - cable_loss_db - splitter_loss_db + gain_dbi;
    return std::nullopt;
}

std::optional<double> own_metres_per_unit(const Scale& s) {
    switch (s.method) {
    case Scale::Method::TwoPoints:
    case Scale::Method::ScaleBar: {
        if (!finite(s.a) || !finite(s.b) || !finite(s.distance_m) || !(s.distance_m > 0.0))
            return std::nullopt;
        const double units = std::hypot(s.b.x - s.a.x, s.b.y - s.a.y);
        if (!(units > 1e-9)) return std::nullopt;
        const double m = s.distance_m / units;
        return finite(m) && m > 0.0 ? std::optional<double>(m) : std::nullopt;
    }
    case Scale::Method::Factor:
        return finite(s.metres_per_unit) && s.metres_per_unit > 0.0 ? std::optional<double>(s.metres_per_unit)
                                                                     : std::nullopt;
    case Scale::Method::None:
    case Scale::Method::Shared:
        return std::nullopt;
    }
    return std::nullopt;
}

const WallClass* Building::find_class(const std::string& id) const {
    for (const auto& c : wall_classes)
        if (c.id == id) return &c;
    return nullptr;
}

const Cell* Building::find_cell(const std::string& id) const {
    for (const auto& c : cells)
        if (c.id == id) return &c;
    return nullptr;
}

const PlanSource* Building::find_source(const std::string& id) const {
    for (const auto& s : sources)
        if (s.id == id) return &s;
    return nullptr;
}

const Floor* Building::find_floor(const std::string& id) const {
    for (const auto& f : floors)
        if (f.id == id) return &f;
    return nullptr;
}

Floor* Building::find_floor(const std::string& id) {
    for (auto& f : floors)
        if (f.id == id) return &f;
    return nullptr;
}

const Floor* Building::floor_at_level(int level) const {
    for (const auto& f : floors)
        if (f.level == level) return &f;
    return nullptr;
}

std::pair<const Floor*, const Antenna*> Building::find_antenna(const std::string& id) const {
    for (const auto& f : floors)
        for (const auto& a : f.antennas)
            if (a.id == id) return {&f, &a};
    return {nullptr, nullptr};
}

std::vector<std::size_t> Building::floors_by_level() const {
    std::vector<std::size_t> order(floors.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return floors[a].level < floors[b].level; });
    return order;
}

std::optional<double> Building::metres_per_unit(const Floor& f) const {
    if (f.scale.method != Scale::Method::Shared) return own_metres_per_unit(f.scale);
    const Floor* other = find_floor(f.scale.shared_floor);
    if (other == nullptr || other == &f || other->scale.method == Scale::Method::Shared) return std::nullopt;
    return own_metres_per_unit(other->scale);
}

std::optional<Point2> Building::to_plan_metres(const Floor& f, Point2 plan) const {
    const auto m = metres_per_unit(f);
    if (!m) return std::nullopt;
    return Point2{plan.x * *m, plan.y * *m};
}

std::optional<Point2> Building::from_plan_metres(const Floor& f, Point2 metres) const {
    const auto m = metres_per_unit(f);
    if (!m) return std::nullopt;
    return Point2{metres.x / *m, metres.y / *m};
}

std::optional<Point2> Building::plan_size_m(const Floor& f) const {
    return to_plan_metres(f, {f.plan_width, f.plan_height});
}

std::optional<Point2> Building::to_building_metres(const Floor& f, Point2 plan) const {
    const auto m = metres_per_unit(f);
    if (!m) return std::nullopt;
    const Point2 r = f.reference_point.value_or(Point2{});
    return Point2{(plan.x - r.x) * *m, (plan.y - r.y) * *m};
}

std::optional<Point2> Building::from_building_metres(const Floor& f, Point2 metres) const {
    const auto m = metres_per_unit(f);
    if (!m) return std::nullopt;
    const Point2 r = f.reference_point.value_or(Point2{});
    return Point2{metres.x / *m + r.x, metres.y / *m + r.y};
}

std::optional<double> Building::wall_attenuation_db(const Wall& w, double mhz) const {
    if (w.attenuation_db) return *w.attenuation_db;
    const WallClass* c = find_class(w.wall_class);
    if (c == nullptr) return std::nullopt;
    return c->attenuation_at_db(mhz);
}

double Building::inter_floor_attenuation_db(int from_level, int to_level) const {
    const int lo = std::min(from_level, to_level);
    const int hi = std::max(from_level, to_level);
    double total = 0.0;
    for (int k = lo; k < hi; ++k) {
        const Floor* f = floor_at_level(k);
        total += (f != nullptr && f->slab_attenuation_db) ? *f->slab_attenuation_db : floor_attenuation_db;
    }
    return total;
}

std::string plan_name(const Building& b, const Floor& f) {
    if (b.name.empty()) return f.name;
    if (f.name.empty()) return b.name;
    return b.name + ", " + f.name;
}

namespace {

void check_pattern(const AntennaPattern& p, const std::string& where, std::vector<std::string>& errors) {
    auto err = [&](const std::string& what) { errors.push_back(where + ": " + what); };
    switch (p.type) {
    case AntennaPattern::Type::Omni:
        break;
    case AntennaPattern::Type::Sector:
        if (!finite(p.azimuth_deg) || p.azimuth_deg < 0.0 || p.azimuth_deg >= 360.0)
            err("azimuth_deg must be in [0, 360)");
        if (!finite(p.beamwidth_deg) || !(p.beamwidth_deg > 0.0) || p.beamwidth_deg > 360.0)
            err("beamwidth_deg must be in (0, 360]");
        if (!finite(p.front_to_back_db) || p.front_to_back_db < 0.0) err("front_to_back_db must be >= 0");
        break;
    case AntennaPattern::Type::Table:
        if (!finite(p.azimuth_deg) || p.azimuth_deg < 0.0 || p.azimuth_deg >= 360.0)
            err("azimuth_deg must be in [0, 360)");
        if (!finite(p.step_deg) || !(p.step_deg > 0.0)) err("step_deg must be > 0");
        if (p.gain_db.empty()) err("gain_db must not be empty");
        if (finite(p.step_deg) && p.step_deg * static_cast<double>(p.gain_db.size()) > 360.0 + 1e-9)
            err("gain_db covers more than 360 degrees");
        for (double g : p.gain_db)
            if (!finite(g)) {
                err("gain_db must be finite");
                break;
            }
        break;
    }
}

void check_trim(const Trim& t, const std::string& where, std::vector<std::string>& errors) {
    auto err = [&](const std::string& what) { errors.push_back(where + ": " + what); };
    if (t.quarter_turns < 0 || t.quarter_turns > 3) err("quarter_turns must be 0..3");
    if (!finite(t.deskew_deg) || t.deskew_deg < -45.0 || t.deskew_deg > 45.0) err("deskew_deg must be -45..45");
    for (const Point2& p : t.outline)
        if (!finite(p)) {
            err("outline points must be finite");
            return;
        }
    if (t.shape == Trim::Shape::None) {
        if (!t.outline.empty()) err("a trim without shape has no outline");
        return;
    }
    if (t.shape == Trim::Shape::Rectangle && t.outline.size() != 4) err("a rectangle has 4 corners");
    if (t.shape == Trim::Shape::Polygon && t.outline.size() < 3) err("a polygon needs at least 3 points");
    double area2 = 0.0;
    for (std::size_t i = 0; i < t.outline.size(); ++i) {
        const Point2& a = t.outline[i];
        const Point2& b = t.outline[(i + 1) % t.outline.size()];
        area2 += a.x * b.y - b.x * a.y;
    }
    if (t.outline.size() >= 3 && !(std::fabs(area2) > 1e-12)) err("the outline has no area");
}

}  // namespace

std::vector<std::string> Building::validate() const {
    std::vector<std::string> errors;
    auto err = [&](const std::string& where, const std::string& what) { errors.push_back(where + ": " + what); };
    auto at = [](const std::string& base, const char* member, std::size_t i) {
        return base + (base.empty() ? "" : ".") + member + "[" + std::to_string(i) + "]";
    };

    std::set<std::string> class_ids;
    for (std::size_t i = 0; i < wall_classes.size(); ++i) {
        const WallClass& c = wall_classes[i];
        const std::string w = at("", "wall_classes", i);
        if (c.id.empty()) err(w, "empty id");
        else if (!class_ids.insert(c.id).second) err(w, "duplicate wall class " + quoted(c.id));
        if (c.bands.empty()) err(w, "needs at least one band");
        for (std::size_t k = 0; k < c.bands.size(); ++k) {
            const BandAttenuation& band = c.bands[k];
            const std::string bw = at(w, "bands", k);
            if (!finite(band.frequency_mhz) || !(band.frequency_mhz > 0.0)) err(bw, "frequency_mhz must be > 0");
            else if (k > 0 && !(band.frequency_mhz > c.bands[k - 1].frequency_mhz))
                err(bw, "frequencies must be ascending");
            if (!finite(band.attenuation_db) || band.attenuation_db < 0.0) err(bw, "attenuation_db must be >= 0");
        }
        if (c.reflection_loss_db && (!finite(*c.reflection_loss_db) || *c.reflection_loss_db < 0.0))
            err(w, "reflection_loss_db must be >= 0");
    }
    if (!finite(floor_attenuation_db) || floor_attenuation_db < 0.0)
        err("floor_attenuation_db", "must be >= 0");

    std::set<std::string> cell_ids;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const Cell& c = cells[i];
        const std::string w = at("", "cells", i);
        if (c.id.empty()) err(w, "empty id");
        else if (!cell_ids.insert(c.id).second) err(w, "duplicate cell " + quoted(c.id));
        for (const auto& [k, v] : c.identity)
            if (k.empty()) err(w, "identity names must not be empty");
    }

    std::set<std::string> source_ids;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const PlanSource& s = sources[i];
        const std::string w = at("", "sources", i);
        if (s.id.empty()) err(w, "empty id");
        else if (!source_ids.insert(s.id).second) err(w, "duplicate source " + quoted(s.id));
        if (s.file_name.find_first_of("/\\") != std::string::npos) err(w, "file_name must not hold a folder");
        const bool hex = std::all_of(s.sha256.begin(), s.sha256.end(),
                                     [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
        if (!s.sha256.empty() && (s.sha256.size() != 64 || !hex)) err(w, "sha256 must be 64 lower-case hex digits");
        if (!s.embedded && s.sha256.empty()) err(w, "a referenced (not embedded) source needs its sha256");
    }

    std::set<std::string> floor_ids;
    std::set<int> levels;
    std::set<std::string> antenna_ids;
    for (std::size_t fi = 0; fi < floors.size(); ++fi) {
        const Floor& f = floors[fi];
        const std::string fw = at("", "floors", fi);
        if (f.id.empty()) err(fw, "empty id");
        else if (!floor_ids.insert(f.id).second) err(fw, "duplicate floor " + quoted(f.id));
        if (!levels.insert(f.level).second) err(fw, "duplicate level " + std::to_string(f.level));
        if (!finite(f.height_m) || !(f.height_m > 0.0)) err(fw, "height_m must be > 0");
        if (!f.source.empty() && find_source(f.source) == nullptr) err(fw, "unknown source " + quoted(f.source));
        if (f.page < 0) err(fw, "page must be >= 0");
        check_trim(f.trim, fw + ".trim", errors);
        if (!finite(f.plan_width) || !finite(f.plan_height) || f.plan_width < 0.0 || f.plan_height < 0.0)
            err(fw, "plan size must be >= 0");
        switch (f.scale.method) {
        case Scale::Method::None:
            break;
        case Scale::Method::Shared: {
            const Floor* other = find_floor(f.scale.shared_floor);
            if (other == nullptr) err(fw + ".scale", "unknown floor " + quoted(f.scale.shared_floor));
            else if (other == &f) err(fw + ".scale", "a floor cannot share its own scale");
            else if (other->scale.method == Scale::Method::Shared)
                err(fw + ".scale", "floor " + quoted(other->id) + " shares a scale itself");
            break;
        }
        case Scale::Method::Factor:
            if (!own_metres_per_unit(f.scale)) err(fw + ".scale", "metres_per_unit must be > 0");
            break;
        case Scale::Method::TwoPoints:
        case Scale::Method::ScaleBar:
            if (!own_metres_per_unit(f.scale))
                err(fw + ".scale", "needs two different finite points and a distance > 0 m");
            break;
        }
        if (f.reference_point && !finite(*f.reference_point)) err(fw, "reference_point must be finite");
        if (f.slab_attenuation_db && (!finite(*f.slab_attenuation_db) || *f.slab_attenuation_db < 0.0))
            err(fw, "slab_attenuation_db must be >= 0");

        std::set<std::string> wall_ids;
        for (std::size_t wi = 0; wi < f.walls.size(); ++wi) {
            const Wall& w = f.walls[wi];
            const std::string ww = at(fw, "walls", wi);
            if (!w.id.empty() && !wall_ids.insert(w.id).second) err(ww, "duplicate wall " + quoted(w.id));
            if (w.wall_class.empty()) {
                if (!w.attenuation_db) err(ww, "a wall without class needs its own attenuation_db");
            } else if (find_class(w.wall_class) == nullptr) {
                err(ww, "unknown wall class " + quoted(w.wall_class));
            }
            if (w.attenuation_db && (!finite(*w.attenuation_db) || *w.attenuation_db < 0.0))
                err(ww, "attenuation_db must be >= 0");
            if (w.points.size() < 2) err(ww, "a wall needs at least 2 points");
            for (const Point2& p : w.points)
                if (!finite(p)) {
                    err(ww, "points must be finite");
                    break;
                }
        }
        for (std::size_t ai = 0; ai < f.antennas.size(); ++ai) {
            const Antenna& a = f.antennas[ai];
            const std::string aw = at(fw, "antennas", ai);
            if (a.id.empty()) err(aw, "empty id");
            else if (!antenna_ids.insert(a.id).second) err(aw, "duplicate antenna " + quoted(a.id));
            if (!finite(a.position)) err(aw, "position must be finite");
            if (!finite(a.height_m)) err(aw, "height_m must be finite");
            if (!finite(a.gain_dbi)) err(aw, "gain_dbi must be finite");
            if (!finite(a.cable_loss_db) || !finite(a.splitter_loss_db) || a.cable_loss_db < 0.0 ||
                a.splitter_loss_db < 0.0)
                err(aw, "cable and splitter losses must be >= 0");
            if ((a.eirp_dbm && !finite(*a.eirp_dbm)) || (a.feed_power_dbm && !finite(*a.feed_power_dbm)))
                err(aw, "powers must be finite");
            if (!a.cell.empty() && find_cell(a.cell) == nullptr) err(aw, "unknown cell " + quoted(a.cell));
            check_pattern(a.pattern, aw + ".pattern", errors);
            if (a.direction_deg && !finite(*a.direction_deg)) err(aw, "direction_deg must be finite");
            if (a.elevation_deg && (!finite(*a.elevation_deg) || *a.elevation_deg < -90.0 || *a.elevation_deg > 90.0))
                err(aw, "elevation_deg must be -90..90");
        }
    }
    return errors;
}

}  // namespace yantra::building
