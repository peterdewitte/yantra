// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied in part from yantra-v2 68ea20a: core/src/survey/views.cpp
// (classify, with_offset), adapted for IND-30, PP-02, PP-04.

#include "yantra/core/survey/views.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace yantra::survey {

namespace {

std::string number_text(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

bool good(double value, double threshold, bool higher_better) {
    return higher_better ? value >= threshold : value <= threshold;
}

}  // namespace

Classified classify(const PointList& list, double threshold, std::optional<bool> higher_better) {
    if (!std::isfinite(threshold)) throw std::invalid_argument("classify: threshold must be finite");
    Classified c;
    c.threshold = threshold;
    c.higher_better = higher_better.value_or(higher_is_better(list.unit));
    c.label = std::string("Good: ") + (c.higher_better ? ">= " : "<= ") + number_text(threshold) +
              (list.unit.empty() ? "" : " " + list.unit);
    c.verdicts.reserve(list.points.size());
    for (const SurveyPoint& p : list.points) {
        Verdict v = Verdict::NoValue;
        if (p.value && p.unit == list.unit) v = good(*p.value, threshold, c.higher_better) ? Verdict::Good : Verdict::Bad;
        c.verdicts.push_back(v);
        (v == Verdict::Good ? c.good : v == Verdict::Bad ? c.bad : c.no_value) += 1;
    }
    if (!list.points.empty())
        c.percent_good = 100.0 * static_cast<double>(c.good) / static_cast<double>(list.points.size());
    return c;
}

OffsetList with_offset(const PointList& list, double offset_db) {
    if (!std::isfinite(offset_db)) throw std::invalid_argument("with_offset: offset must be finite");
    if (list.metric.value == Metric::Value::Quality)
        throw std::invalid_argument("with_offset: only levels and C1 move with a level offset");
    OffsetList out;
    out.offset_db = offset_db;
    out.label = std::string("Simulation: ") + (list.metric.value == Metric::Value::C1 ? "C1 " : "level ") +
                (offset_db >= 0 ? "+" : "") + number_text(offset_db) + " dB (not measured)";
    out.list = list;
    for (SurveyPoint& p : out.list.points) {
        if (p.value && p.unit == list.unit) *p.value += offset_db;
        p.rgb = out.list.scale.colour_of(p.value && p.unit == out.list.scale.unit ? p.value : std::nullopt);
    }
    return out;
}

std::vector<ServingChange> serving_changes(const std::vector<SurveyPoint>& points) {
    std::vector<ServingChange> out;
    std::map<std::int64_t, const SurveyPoint*> last;  // per session
    for (std::size_t i = 0; i < points.size(); ++i) {
        const SurveyPoint& p = points[i];
        if (p.serving_cell.empty() && p.serving_hz == 0) continue;
        auto [it, fresh] = last.try_emplace(p.session_id, &p);
        if (fresh) continue;
        const SurveyPoint& prev = *it->second;
        if (prev.serving_cell != p.serving_cell || prev.serving_hz != p.serving_hz)
            out.push_back({i, p.session_id, prev.serving_cell, p.serving_cell});
        it->second = &p;
    }
    return out;
}

std::vector<CellRef> cells_seen(const project::Project& project, const PointQuery& query) {
    // The sessions exactly as point_list selects them (and checks them).
    PointQuery q = query;
    q.metric.failed_cells.clear();
    const PointList list = point_list(project, q);
    std::set<std::pair<std::uint64_t, std::string>> cells;
    for (std::int64_t id : list.sessions) {
        for (const project::StoredPoint& sp : project.points(id)) {
            const nlohmann::json sample = nlohmann::json::parse(sp.sample, nullptr, false);
            if (!sample.is_object() || !sample.contains("carriers") || !sample["carriers"].is_array()) continue;
            for (const nlohmann::json& c : sample["carriers"]) {
                if (c.value("role", "") == "neighbour_entry") continue;  // a list entry, not a measured cell
                if (!query.metric.device.empty() && c.value("dev", "") != query.metric.device) continue;
                cells.emplace(c.value("f", std::uint64_t{0}), carrier_cell_text(c));
            }
        }
    }
    std::vector<CellRef> out;
    for (const auto& [f, cell] : cells) out.push_back({f, cell});
    return out;
}

}  // namespace yantra::survey
