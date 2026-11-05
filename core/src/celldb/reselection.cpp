// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cell reselection simulator (reselection.h, DB-08).

#include "yantra/core/celldb/reselection.h"

#include <algorithm>
#include <cmath>
#include <deque>

#include "yantra/core/celldb/network.h"
#include "yantra/core/measure/technology_map.h"

namespace yantra::celldb {

namespace {

const char* status_text(yantra_status s) {
    switch (s) {
    case YANTRA_ERR_UNSUPPORTED: return "the plugin cannot decide with these parameters";
    case YANTRA_ERR_INVALID_ARGUMENT: return "the plugin refused the question";
    default: return "the plugin failed";
    }
}

std::optional<double> c1_of(const Technology& t, const yantra_record* record, double level, double ms_power) {
    if (t.api == nullptr || t.api->compute_c1 == nullptr) return std::nullopt;
    yantra_c1_input in{sizeof(yantra_c1_input), 0, record, level, ms_power};
    double c1 = 0.0;
    if (t.api->compute_c1(t.instance, &in, &c1) != YANTRA_OK || !std::isfinite(c1)) return std::nullopt;
    return c1;
}

}  // namespace

ReselectionOutcome simulate(const Technology& t, const SimCell& serving, const std::vector<SimCell>& neighbours,
                            double ms_power_dbm) {
    ReselectionOutcome o;
    if (t.api == nullptr || t.api->reselect == nullptr) {
        o.error = "the " + (t.label.empty() ? t.id : t.label) + " plugin has no reselection rule";
        return o;
    }
    std::vector<plugin::Record> records;
    records.reserve(neighbours.size() + 1);
    records.push_back(t.cell_record(serving.fields));
    for (const SimCell& n : neighbours) records.push_back(t.cell_record(n.fields));
    std::deque<measure::RecordView> views;  // stays in place: the C structs point into it
    for (const plugin::Record& r : records) views.emplace_back(r, t.cell_schema_index);
    const yantra_reselection_cell s{sizeof(yantra_reselection_cell), 0, views[0].get(), serving.level_dbm};
    std::vector<yantra_reselection_cell> cells;
    cells.reserve(neighbours.size());
    for (std::size_t k = 0; k < neighbours.size(); ++k)
        cells.push_back(yantra_reselection_cell{sizeof(yantra_reselection_cell), 0, views[k + 1].get(),
                                                neighbours[k].level_dbm});
    std::vector<const yantra_reselection_cell*> ptrs;
    for (const yantra_reselection_cell& c : cells) ptrs.push_back(&c);
    const yantra_reselection_input in{sizeof(yantra_reselection_input), static_cast<std::uint32_t>(ptrs.size()), &s,
                                      ptrs.empty() ? nullptr : ptrs.data(), ms_power_dbm};
    yantra_reselection_result out{};
    out.struct_size = sizeof out;
    out.chosen = -1;
    o.serving_c1 = c1_of(t, views[0].get(), serving.level_dbm, ms_power_dbm);
    for (std::size_t k = 0; k < neighbours.size(); ++k)
        o.neighbour_c1.push_back(c1_of(t, views[k + 1].get(), neighbours[k].level_dbm, ms_power_dbm));
    const yantra_status st = t.api->reselect(t.instance, &in, &out);
    if (st != YANTRA_OK) {
        o.error = status_text(st);
        return o;
    }
    out.reason[sizeof out.reason - 1] = '\0';
    o.ok = true;
    o.serving_state = out.serving_state;
    o.state = t.serving_state_name(out.serving_state);
    o.reason = out.reason;
    if (out.chosen >= 0 && static_cast<std::size_t>(out.chosen) < neighbours.size())
        o.chosen = static_cast<std::size_t>(out.chosen);
    return o;
}

std::vector<std::string> neighbour_entries(const CellDatabase& db, std::string_view id) {
    std::vector<std::string> out;
    const CellEntry* e = db.find(id);
    if (e == nullptr) return out;
    const std::vector<CellView> views = database_views(db);
    for (const NeighbourRef& ref : e->neighbours) {
        const Resolution r = resolve(ref, views, e->cell.technology_id);
        if (r.kind != Resolution::Kind::Unique) continue;
        const std::string& n = views[r.index].id;
        if (n != e->cell.id && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
    return out;
}

ReselectionOutcome simulate(const CellDatabase& db, const Technologies& technologies, std::string_view serving,
                            const std::map<std::string, double>& levels, double ms_power_dbm,
                            double absent_level_dbm) {
    ReselectionOutcome o;
    const CellEntry* e = db.find(serving);
    if (e == nullptr) {
        o.error = "no cell " + std::string(serving);
        return o;
    }
    const Technology* t = find_technology(technologies, e->cell.technology_id);
    if (t == nullptr) {
        o.error = "no plugin for the technology '" + e->cell.technology_id + "'";
        return o;
    }
    auto cell = [&](const CellEntry& x) {
        const auto it = levels.find(x.cell.id);
        return SimCell{x.cell.id, x.fields(), it == levels.end() ? absent_level_dbm : it->second};
    };
    std::vector<SimCell> ns;
    for (const std::string& id : neighbour_entries(db, serving)) ns.push_back(cell(*db.find(id)));
    return simulate(*t, cell(*e), ns, ms_power_dbm);
}

std::vector<WalkEvent> simulate_walk(const CellDatabase& db, const Technologies& technologies,
                                     const std::vector<WalkStep>& steps, const WalkOptions& o) {
    std::vector<WalkEvent> out;
    if (steps.empty()) return out;
    std::string serving = o.start;
    if (serving.empty()) {
        double best = -1e300;
        for (const auto& [id, level] : steps.front().levels)
            if (db.find(id) != nullptr && level > best) {
                best = level;
                serving = id;
            }
    }
    if (serving.empty() || db.find(serving) == nullptr) return out;
    std::string pending;              // the neighbour chosen since `since`
    std::int64_t since = 0;
    const auto hold_ns = static_cast<std::int64_t>(std::llround(o.hold_s * 1e9));
    for (const WalkStep& step : steps) {
        WalkEvent ev;
        ev.time_ns = step.time_ns;
        ev.serving = serving;
        ev.outcome = simulate(db, technologies, serving, step.levels, o.ms_power_dbm, o.absent_level_dbm);
        if (ev.outcome.ok && ev.outcome.chosen) {
            const std::vector<std::string> ns = neighbour_entries(db, serving);
            ev.chosen = ns[*ev.outcome.chosen];
        }
        if (ev.chosen.empty()) {
            pending.clear();
        } else {
            if (ev.chosen != pending) {
                pending = ev.chosen;
                since = step.time_ns;
            }
            if (step.time_ns - since >= hold_ns) {
                ev.reselected = true;
                serving = ev.chosen;
                pending.clear();
            }
        }
        out.push_back(std::move(ev));
    }
    return out;
}

std::vector<WalkStep> walk_steps(const std::vector<measure::MeasurementCycle>& cycles, const CellDatabase& db,
                                 const Technologies& technologies) {
    namespace sf = measure::sample_flag;
    const std::vector<CellView> views = database_views(db);
    std::vector<WalkStep> out;
    for (const measure::MeasurementCycle& cy : cycles) {
        WalkStep step;
        step.time_ns = cy.time.monotonic_ns;
        for (const measure::CarrierSample& s : cy.samples) {
            const measure::MappedRecord& m = s.latest;
            if (s.records_in_cycle == 0 || ((s.flags | m.flags) & (sf::kNoSync | sf::kStale | sf::kHeld |
                                                                   sf::kUncalibrated)) != 0)
                continue;
            if (m.schema == nullptr || !std::isfinite(m.level)) continue;
            const Technology* t = find_technology(technologies, m.provenance.technology_id);
            if (t == nullptr || m.schema_index != t->cell_schema_index) continue;
            // The record as a reference by the fields that identify cells.
            NeighbourRef ref;
            for (const std::string& f : t->match_fields(db.match_fields(t->id))) {
                const auto i = m.schema->field_index(f);
                if (!i || *i >= m.record.values.size()) continue;
                if (auto v = from_record_value(m.record.values[*i])) ref.fields[f] = std::move(*v);
            }
            if (ref.fields.empty()) continue;
            const Resolution r = resolve(ref, views, t->id);
            if (r.kind != Resolution::Kind::Unique) continue;
            auto [it, fresh] = step.levels.emplace(views[r.index].id, m.level);
            if (!fresh) it->second = std::max(it->second, m.level);
        }
        out.push_back(std::move(step));
    }
    return out;
}

}  // namespace yantra::celldb
