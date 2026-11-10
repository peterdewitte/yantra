// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Survey cells, views and the neighbour cross-check (network.h, DB-05).

#include "yantra/core/celldb/network.h"

#include <algorithm>
#include <cmath>

#include "yantra/core/plugin/record.h"

namespace yantra::celldb {

namespace {

FieldMap record_fields(const plugin::Record& r, const plugin::Schema& s) {
    FieldMap m;
    for (std::size_t k = 0; k < s.fields.size() && k < r.values.size(); ++k)
        if (auto v = from_record_value(r.values[k])) m[s.fields[k].name] = std::move(*v);
    return m;
}

std::optional<std::uint64_t> uint_field(const FieldMap& m, std::string_view name) {
    const auto it = m.find(name);
    if (it == m.end()) return std::nullopt;
    const auto* i = std::get_if<std::int64_t>(&it->second);
    if (i == nullptr || *i < 0) return std::nullopt;
    return static_cast<std::uint64_t>(*i);
}

// A neighbour list entry as a reference, by the technology's links (link
// rule of plugin API 1.2, PLG-05): a linked cell KEY field is the entry's
// own value when the entry gives it (e.g. a neighbour's LA, MCC, MNC,
// RAD-22), else the broadcaster's; KEY fields without a link are the
// broadcaster's. Values taken from the broadcaster are inherited (they
// rank a match but never refuse one). An entry without any linked value
// of its own names no cell.
std::optional<NeighbourRef> reference_of(const FieldMap& entry, const FieldMap& broadcaster, const Technology& t) {
    NeighbourRef ref;
    for (const plugin::FieldLink& l : t.links)
        if (const auto it = entry.find(l.neighbour_field); it != entry.end()) ref.fields[l.cell_field] = it->second;
    if (ref.fields.empty()) return std::nullopt;
    for (const std::string& k : t.key_fields()) {
        if (ref.fields.count(k) != 0) continue;
        if (const auto it = broadcaster.find(k); it != broadcaster.end()) {
            ref.fields[k] = it->second;
            ref.inherited.insert(k);
        }
    }
    return ref;
}

std::string view_name(const CellView& v) { return v.label.empty() ? v.id : v.label; }

}  // namespace

Observations::Observations(Technologies technologies) : technologies_(std::move(technologies)) {}

void Observations::add(const measure::MappedRecord& r) {
    if (r.schema == nullptr || (r.flags & measure::sample_flag::kNoSync) != 0) return;
    const Technology* t = find_technology(technologies_, r.provenance.technology_id);
    if (t == nullptr) return;
    if (r.schema_index == t->cell_schema_index) {
        const std::string key = plugin::record_key(r.record, *r.schema);
        if (key.empty()) return;
        auto it = std::find_if(cells_.begin(), cells_.end(),
                               [&](const ObservedCell& c) { return c.technology == t->id && c.key == key; });
        if (it == cells_.end()) {
            ObservedCell c;
            c.technology = t->id;
            c.key = key;
            c.first_ns = r.record.time.monotonic_ns;
            cells_.push_back(std::move(c));
            it = cells_.end() - 1;
        }
        it->fields = record_fields(r.record, *r.schema);
        if (std::isfinite(r.level)) {
            it->level = r.level;
            it->max_level = std::max(it->max_level, r.level);
        }
        ++it->records;
        it->last_ns = r.record.time.monotonic_ns;
        if (r.role == measure::CarrierRole::Serving) {
            it->serving = true;
            if (r.frequency_hz != 0) broadcaster_[{t->id, r.frequency_hz}] = key;
        }
        return;
    }
    if (t->neighbour_schema && r.schema_index == t->neighbour_schema_index && r.frequency_hz != 0) {
        const FieldMap f = record_fields(r.record, *r.schema);
        const auto count = uint_field(f, "entry_count");
        const auto index = uint_field(f, "entry_index");
        if (!count || !index) return;
        List& l = lists_[{t->id, r.frequency_hz}];
        l.entry_count = *count;
        l.entries.erase(l.entries.lower_bound(*count), l.entries.end());
        if (*index < *count && *index < kMaxNeighbours) l.entries[*index] = f;
    }
}

void Observations::add(const measure::MeasurementCycle& cycle) {
    for (const measure::CarrierSample& s : cycle.samples) {
        if (s.records_in_cycle == 0 || (s.flags & measure::sample_flag::kNoSync) != 0) continue;
        add(s.latest);
    }
}

std::vector<ObservedCell> Observations::cells() const {
    std::vector<ObservedCell> out = cells_;
    for (const auto& [where, list] : lists_) {
        const auto b = broadcaster_.find(where);
        if (b == broadcaster_.end()) continue;  // no synced serving cell on that carrier
        const Technology* t = find_technology(technologies_, where.first);
        auto c = std::find_if(out.begin(), out.end(),
                              [&](const ObservedCell& x) { return x.technology == where.first && x.key == b->second; });
        if (t == nullptr || c == out.end()) continue;
        std::vector<NeighbourRef> refs;
        for (const auto& [index, entry] : list.entries)
            if (auto ref = reference_of(entry, c->fields, *t)) refs.push_back(std::move(*ref));
        c->neighbours = std::move(refs);
    }
    return out;
}

std::vector<CellView> database_views(const CellDatabase& db) {
    std::vector<CellView> out;
    for (const CellEntry& e : db.entries()) {
        CellView v;
        v.technology = e.cell.technology_id;
        v.id = e.cell.id;
        v.label = e.display_name();
        v.fields = e.fields();
        if (!e.neighbours.empty()) v.neighbours = e.neighbours;
        v.in_database = true;
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<CellView> observed_views(const std::vector<ObservedCell>& cells) {
    std::vector<CellView> out;
    for (const ObservedCell& c : cells) {
        CellView v;
        v.technology = c.technology;
        v.id = "observed:" + c.key;
        v.label = c.key;
        v.fields = c.fields;
        v.neighbours = c.neighbours;
        v.observed = true;
        out.push_back(std::move(v));
    }
    return out;
}

bool fields_pair(const FieldMap& stored, const FieldMap& received, const std::vector<std::string>& fields) {
    if (fields.empty()) return false;
    for (const std::string& f : fields) {
        const auto a = stored.find(f);
        const auto b = received.find(f);
        if (a == stored.end() || b == received.end() || !values_equal(a->second, b->second)) return false;
    }
    return true;
}

std::vector<CellView> combined_views(const CellDatabase& db, const std::vector<ObservedCell>& cells,
                                     const Technologies& technologies) {
    std::vector<CellView> out = database_views(db);
    std::vector<bool> used(cells.size(), false);
    for (CellView& v : out) {
        const Technology* t = find_technology(technologies, v.technology);
        const std::vector<std::string> mf =
            t != nullptr ? t->match_fields(db.match_fields(v.technology)) : db.match_fields(v.technology);
        for (std::size_t k = 0; k < cells.size(); ++k) {
            if (cells[k].technology != v.technology || !fields_pair(v.fields, cells[k].fields, mf)) continue;
            used[k] = true;
            v.observed = true;
            if (cells[k].neighbours) v.neighbours = cells[k].neighbours;  // the survey's list is the current one
        }
    }
    std::vector<ObservedCell> rest;
    for (std::size_t k = 0; k < cells.size(); ++k)
        if (!used[k]) rest.push_back(cells[k]);
    for (CellView& v : observed_views(rest)) out.push_back(std::move(v));
    return out;
}

std::optional<int> match_score(const NeighbourRef& ref, const FieldMap& fields) {
    int score = 0;
    bool named = false;
    for (const auto& [k, v] : ref.fields) {
        const auto it = fields.find(k);
        if (it == fields.end()) continue;
        const bool agree = values_equal(v, it->second);
        if (ref.inherited.count(k) == 0) {
            if (!agree) return std::nullopt;
            named = true;
            ++score;
        } else if (agree) {
            ++score;
        }
    }
    if (!named) return std::nullopt;
    return score;
}

Resolution resolve(const NeighbourRef& ref, const std::vector<CellView>& views, const std::string& technology) {
    Resolution r;
    int best = -1;
    std::size_t count = 0;
    for (std::size_t k = 0; k < views.size(); ++k) {
        if (views[k].technology != technology) continue;
        const auto s = match_score(ref, views[k].fields);
        if (!s) continue;
        if (*s > best) {
            best = *s;
            count = 1;
            r.index = k;
        } else if (*s == best) {
            ++count;
        }
    }
    r.kind = count == 0 ? Resolution::Kind::None : count == 1 ? Resolution::Kind::Unique : Resolution::Kind::Ambiguous;
    return r;
}

const char* finding_name(CrossCheckFinding::Kind kind) noexcept {
    switch (kind) {
    case CrossCheckFinding::Kind::NotMutual: return "not_mutual";
    case CrossCheckFinding::Kind::ListUnknown: return "list_unknown";
    case CrossCheckFinding::Kind::NotSeen: return "not_seen";
    case CrossCheckFinding::Kind::Ambiguous: return "ambiguous";
    }
    return "not_mutual";
}

std::vector<CrossCheckFinding> cross_check(const std::vector<CellView>& views) {
    std::vector<CrossCheckFinding> out;
    for (std::size_t a = 0; a < views.size(); ++a) {
        const CellView& va = views[a];
        if (!va.neighbours) continue;
        for (std::size_t k = 0; k < va.neighbours->size(); ++k) {
            const NeighbourRef& ref = (*va.neighbours)[k];
            CrossCheckFinding f;
            f.technology = va.technology;
            f.cell = va.id;
            f.entry = k;
            const Resolution r = resolve(ref, views, va.technology);
            if (r.kind == Resolution::Kind::None) {
                f.kind = CrossCheckFinding::Kind::NotSeen;
                f.text = view_name(va) + " lists " + neighbour_text(ref) + ", a cell not seen and not in the database";
                out.push_back(std::move(f));
                continue;
            }
            if (r.kind == Resolution::Kind::Ambiguous) {
                f.kind = CrossCheckFinding::Kind::Ambiguous;
                f.text = view_name(va) + " lists " + neighbour_text(ref) + ", which names more than one cell";
                out.push_back(std::move(f));
                continue;
            }
            if (r.index == a) continue;  // lists itself: nothing to cross-check
            const CellView& vb = views[r.index];
            f.other = vb.id;
            if (!vb.neighbours) {
                f.kind = CrossCheckFinding::Kind::ListUnknown;
                f.text = view_name(va) + " lists " + view_name(vb) + "; the neighbour list of " + view_name(vb) +
                         " is not known";
                out.push_back(std::move(f));
                continue;
            }
            const bool back = std::any_of(vb.neighbours->begin(), vb.neighbours->end(), [&](const NeighbourRef& x) {
                const Resolution rb = resolve(x, views, vb.technology);
                return rb.kind == Resolution::Kind::Unique && rb.index == a;
            });
            if (!back) {
                f.kind = CrossCheckFinding::Kind::NotMutual;
                f.text = view_name(va) + " lists " + view_name(vb) + ", but " + view_name(vb) + " does not list " +
                         view_name(va);
                out.push_back(std::move(f));
            }
        }
    }
    return out;
}

}  // namespace yantra::celldb
