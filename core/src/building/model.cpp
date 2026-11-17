// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building model (IND-04, IND-06..09), see model.h. Copied from yantra-v2
// 68ea20a: core/src/building/building.cpp, adapted (scale methods, plan
// sources and trims as data, cells by named key values, plan-frame metres
// for RAD-18).

#include "yantra/core/building/model.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace yantra::building {

namespace {

bool finite(double v) { return std::isfinite(v); }
bool finite(Point2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

}  // namespace

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

std::string plan_name(const Building& b, const Floor& f) {
    if (b.name.empty()) return f.name;
    if (f.name.empty()) return b.name;
    return b.name + ", " + f.name;
}

namespace {

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
    }
    return errors;
}

}  // namespace yantra::building
