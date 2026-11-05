// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/survey/points.h"

#include <algorithm>
#include <cstdio>

namespace yantra::survey {

namespace {

using nlohmann::json;
using Code = project::ProjectError::Code;

std::string cell_text(const json& c) { return carrier_cell_text(c); }

const json& carriers_of(const json& sample) {
    static const json empty = json::array();
    if (!sample.is_object()) return empty;
    const auto it = sample.find("carriers");
    return it != sample.end() && it->is_array() ? *it : empty;
}

bool measured(const json& c) { return c.value("role", "") != "neighbour_entry"; }

bool failed(const json& c, const std::vector<CellRef>& cells) {
    if (cells.empty()) return false;
    const auto f = c.value("f", std::uint64_t{0});
    for (const CellRef& r : cells)
        if (r.frequency_hz == f && (r.cell.empty() || r.cell == cell_text(c))) return true;
    return false;
}

// The serving carrier (the best remaining server with failed cells) and
// the strongest measured carrier among those `keep` accepts.
struct Picked {
    const json* serving = nullptr;
    const json* strongest = nullptr;
};
template <typename Keep>
Picked pick_carriers(const json& sample, const std::vector<CellRef>& failed_cells, Keep keep) {
    Picked out;
    double best_level = 0.0;
    for (const json& c : carriers_of(sample)) {
        if (!keep(c) || failed(c, failed_cells)) continue;
        if (c.value("role", "") == "serving" && out.serving == nullptr) out.serving = &c;
        if (!measured(c) || !c.contains("level")) continue;
        const double level = c["level"].get<double>();
        if (out.strongest == nullptr || level > best_level) {
            out.strongest = &c;
            best_level = level;
        }
    }
    // PP-03: the stored serving cell failed: the strongest remaining serves.
    if (out.serving == nullptr && !failed_cells.empty()) {
        bool had = false;
        for (const json& c : carriers_of(sample))
            if (keep(c) && c.value("role", "") == "serving") had = true;
        if (had) out.serving = out.strongest;
    }
    return out;
}

// The carrier the metric reads, or nullptr.
const json* pick(const json& sample, const Metric& m) {
    const auto dev = [&](const json& c) { return m.device.empty() || c.value("dev", "") == m.device; };
    if (m.carrier == Metric::Carrier::Serving) return pick_carriers(sample, m.failed_cells, dev).serving;
    if (m.carrier == Metric::Carrier::Strongest) return pick_carriers(sample, m.failed_cells, dev).strongest;
    const Picked p = pick_carriers(sample, m.failed_cells, [&](const json& c) {
        return dev(c) && c.value("f", std::uint64_t{0}) == m.frequency_hz;
    });
    return p.serving != nullptr ? p.serving : p.strongest;
}

}  // namespace

std::string Metric::label() const {
    std::string v = value == Value::Level ? "Level" : value == Value::C1 ? "C1" : quality_field;
    if (carrier == Carrier::Serving) v += ", serving carrier";
    else if (carrier == Carrier::Strongest) v += ", strongest carrier";
    else {
        char f[48];
        std::snprintf(f, sizeof f, "%.4f MHz", static_cast<double>(frequency_hz) / 1e6);
        v += std::string(", ") + f;
    }
    // PP-03: a simulation is always labelled as one.
    if (!failed_cells.empty())
        v += " (simulation: " + std::to_string(failed_cells.size()) + (failed_cells.size() == 1 ? " cell" : " cells") +
             " off, not measured)";
    return v;
}

std::optional<MetricValue> metric_value(const json& sample, const Metric& metric) {
    const json* c = pick(sample, metric);
    if (c == nullptr) return std::nullopt;
    MetricValue out;
    out.frequency_hz = c->value("f", std::uint64_t{0});
    out.cell = cell_text(*c);
    switch (metric.value) {
        case Metric::Value::Level:
            if (!c->contains("level")) return std::nullopt;
            out.value = (*c)["level"].get<double>();
            out.unit = c->value("unit", "dBm");
            return out;
        case Metric::Value::C1:
            if (!c->contains("c1")) return std::nullopt;
            out.value = (*c)["c1"].get<double>();
            out.unit = "dB";
            return out;
        case Metric::Value::Quality:
            for (const json& q : c->value("quality", json::array()))
                if (q.value("field", "") == metric.quality_field) {
                    out.value = q.value("value", 0.0);
                    out.unit = q.value("unit", "");
                    return out;
                }
            return std::nullopt;
    }
    return std::nullopt;
}

std::string carrier_cell_text(const json& c) {
    std::string out;
    if (!c.is_object() || !c.contains("cell")) return out;
    for (const auto& [k, v] : c["cell"].items()) out += (out.empty() ? "" : " ") + k + "=" + v.get<std::string>();
    return out;
}

std::string serving_cell(const json& sample, const std::string& device, const std::vector<CellRef>& failed_cells) {
    const json* s = pick_carriers(sample, failed_cells, [&](const json& c) {
                        return device.empty() || c.value("dev", "") == device;
                    }).serving;
    return s != nullptr ? cell_text(*s) : std::string();
}

std::uint32_t ColourScale::colour_of(std::optional<double> value) const {
    if (!value) return no_value_rgb;
    if (lower_is_better) {
        for (const ColourBand& b : bands)
            if (*value <= b.from) return b.rgb;
        return outside_rgb;
    }
    std::uint32_t rgb = outside_rgb;
    for (const ColourBand& b : bands)
        if (*value >= b.from) rgb = b.rgb;
    return rgb;
}

ColourScale default_colour_scale(const std::string& unit) {
    ColourScale s;
    s.unit = unit;
    // Red to green: poor, fair, good, very good.
    const std::uint32_t rgb[4] = {0xe53935, 0xfb8c00, 0xfdd835, 0x43a047};
    auto bands = [&](std::vector<double> from) {
        for (std::size_t i = 0; i < from.size(); ++i) {
            char label[32];
            std::snprintf(label, sizeof label, "%s %g %s", s.lower_is_better ? "<=" : ">=", from[i], unit.c_str());
            s.bands.push_back({from[i], rgb[s.lower_is_better ? 3 - i : i], label});
        }
    };
    if (unit == "dBm") bands({-100, -90, -80, -70});
    else if (unit == "dBFS") bands({-70, -60, -50, -40});
    else if (unit == "dB") bands({0, 5, 10, 20});
    else if (unit == "%") {
        s.lower_is_better = true;
        bands({1, 2, 5, 10});
    } else {
        s.bands.push_back({-1e300, 0x1e88e5, "value"});
    }
    return s;
}

PointList point_list(const project::Project& p, const PointQuery& q) {
    const building::Building b = p.building();
    const building::Floor* f = b.find_floor(q.floor_id);
    if (f == nullptr) throw project::ProjectError(Code::NotFound, "no floor " + q.floor_id);
    PointList list;
    list.floor_id = q.floor_id;
    list.metric = q.metric;
    if (q.sessions.empty()) {
        for (const project::Session& s : p.sessions(q.floor_id)) list.sessions.push_back(s.id);
    } else {
        list.sessions = q.sessions;
        std::sort(list.sessions.begin(), list.sessions.end());
        list.sessions.erase(std::unique(list.sessions.begin(), list.sessions.end()), list.sessions.end());
        for (std::int64_t id : list.sessions) {
            const auto s = p.session(id);
            if (!s) throw project::ProjectError(Code::NotFound, "no session " + std::to_string(id));
            if (s->floor_id != q.floor_id)
                throw project::ProjectError(Code::Invalid, "session " + std::to_string(id) + " is on another floor");
        }
    }
    list.to_scale = b.metres_per_unit(*f).has_value();
    for (std::int64_t id : list.sessions) {
        for (project::StoredPoint& sp : p.points(id)) {
            SurveyPoint pt;
            pt.session_id = sp.session_id;
            pt.sequence = sp.sequence;
            pt.time_ns = sp.time_ns;
            pt.utc_ns = sp.utc_ns;
            pt.plan = sp.plan;
            pt.metres = sp.metres;
            pt.kind = std::move(sp.kind);
            pt.group = sp.group;
            pt.label = std::move(sp.label);
            const json sample = json::parse(sp.sample, nullptr, false);
            if (!sample.is_discarded()) {
                if (const auto v = metric_value(sample, q.metric)) {
                    pt.value = v->value;
                    pt.unit = v->unit;
                    pt.frequency_hz = v->frequency_hz;
                    pt.cell = v->cell;
                    if (list.unit.empty()) list.unit = v->unit;
                    else if (v->unit != list.unit) list.mixed_units = true;
                }
                const json* serving = pick_carriers(sample, q.metric.failed_cells, [&](const json& c) {
                                          return q.metric.device.empty() || c.value("dev", "") == q.metric.device;
                                      }).serving;
                if (serving != nullptr) {
                    pt.serving_cell = cell_text(*serving);
                    pt.serving_hz = serving->value("f", std::uint64_t{0});
                }
            }
            if (!pt.metres) list.to_scale = false;
            list.points.push_back(std::move(pt));
        }
    }
    list.scale = q.scale ? *q.scale : default_colour_scale(list.unit);
    for (SurveyPoint& pt : list.points)
        pt.rgb = list.scale.colour_of(pt.unit == list.scale.unit ? pt.value : std::nullopt);
    return list;
}

std::vector<std::pair<std::string, std::size_t>> point_counts(const project::Project& p) {
    std::vector<std::pair<std::string, std::size_t>> out;
    for (const building::Floor& f : p.floors()) out.emplace_back(f.id, p.point_count(f.id));
    return out;
}

}  // namespace yantra::survey
