// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Verification, difference log and auto-learn (verify.h). Copied in part
// from yantra-v2 68ea20a: core/src/celldb/verify.cpp, adapted for DB-03/04
// with the plugin's values_match and string entry ids.

#include "yantra/core/celldb/verify.h"

#include <algorithm>
#include <fstream>

namespace yantra::celldb {

namespace {

// The KEY and VERIFY fields of the cell schema the database uses for a
// technology (in database column order), or the database's fields when
// there is no Technology.
std::vector<std::string> compared_fields(const CellDatabase& db, const std::string& tech, const Technology* t) {
    std::vector<std::string> used = db.field_names(tech);
    if (t == nullptr) return used;
    std::vector<std::string> out;
    for (const std::string& f : used) {
        const auto i = t->cell_schema.field_index(f);
        if (i && (t->cell_schema.fields[*i].key || t->cell_schema.fields[*i].verify)) out.push_back(f);
    }
    return out;
}

std::vector<std::string> pairing_fields(const CellDatabase& db, const std::string& tech, const Technology* t) {
    return t != nullptr ? t->match_fields(db.match_fields(tech)) : db.match_fields(tech);
}

std::vector<std::string> learned_fields(const Technology& t, const LearnOptions& o) {
    if (!o.fields.empty()) return o.fields;
    std::vector<std::string> out;
    for (const plugin::Field& f : t.cell_schema.fields)
        if (f.key || f.verify) out.push_back(f.name);
    return out;
}

std::vector<NeighbourCheck> compare_lists(const std::vector<NeighbourRef>& stored,
                                          const std::vector<NeighbourRef>& received) {
    std::vector<NeighbourCheck> out;
    std::vector<bool> used(stored.size(), false);
    for (const NeighbourRef& r : received) {
        // The best unused stored reference this one names.
        int best = -1;
        std::size_t at = 0;
        for (std::size_t k = 0; k < stored.size(); ++k) {
            if (used[k]) continue;
            const auto s = match_score(r, stored[k].fields);
            if (s && *s > best) {
                best = *s;
                at = k;
            }
        }
        NeighbourCheck c;
        c.received = r;
        if (best >= 0) {
            used[at] = true;
            c.database = stored[at];
            c.state = State::Match;
        } else {
            c.state = State::New;
        }
        out.push_back(std::move(c));
    }
    for (std::size_t k = 0; k < stored.size(); ++k)
        if (!used[k]) out.push_back(NeighbourCheck{State::Missing, stored[k], std::nullopt});
    return out;
}

std::string text_of(const std::optional<FieldValue>& v, const std::vector<std::string>& hex, const std::string& f) {
    if (!v) return {};
    return value_text(*v, std::find(hex.begin(), hex.end(), f) != hex.end());
}

std::string csv_value(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

}  // namespace

const char* state_name(State s) noexcept {
    switch (s) {
    case State::Match: return "match";
    case State::Differs: return "differs";
    case State::New: return "new";
    case State::Missing: return "missing";
    }
    return "match";
}

bool CellCheck::all_match() const {
    if (status != Status::Paired) return false;
    for (const FieldCheck& f : fields)
        if (f.state != State::Match) return false;
    for (const NeighbourCheck& n : neighbours)
        if (n.state != State::Match) return false;
    return true;
}

std::size_t Verification::differences() const {
    std::size_t n = 0;
    for (const CellCheck& c : cells) {
        for (const FieldCheck& f : c.fields) n += f.state != State::Match ? 1 : 0;
        for (const NeighbourCheck& x : c.neighbours) n += x.state != State::Match ? 1 : 0;
    }
    return n;
}

Verification verify(const CellDatabase& db, const std::vector<ObservedCell>& received,
                    const Technologies& technologies) {
    Verification v;
    std::set<std::string> seen;
    for (std::size_t k = 0; k < received.size(); ++k) {
        const ObservedCell& r = received[k];
        const Technology* t = find_technology(technologies, r.technology);
        const std::vector<std::string> mf = pairing_fields(db, r.technology, t);
        CellCheck c;
        c.observed = k;
        for (const CellEntry& e : db.entries())
            if (e.cell.technology_id == r.technology && fields_pair(e.fields(), r.fields, mf))
                c.candidates.push_back(e.cell.id);
        if (c.candidates.empty()) {
            c.status = CellCheck::Status::NotInDatabase;
        } else if (c.candidates.size() > 1) {
            c.status = CellCheck::Status::Ambiguous;
            for (const std::string& id : c.candidates) seen.insert(id);
        } else {
            c.entry = c.candidates.front();
            c.candidates.clear();
            seen.insert(c.entry);
            const CellEntry& e = *db.find(c.entry);
            for (const std::string& f : compared_fields(db, r.technology, t)) {
                FieldCheck fc;
                fc.field = f;
                fc.database = e.field(f);
                if (const auto it = r.fields.find(f); it != r.fields.end()) fc.received = it->second;
                if (fc.database && fc.received) {
                    const bool same = t != nullptr ? t->values_match(f, *fc.received, *fc.database)
                                                   : values_equal(*fc.received, *fc.database);
                    fc.state = same ? State::Match : State::Differs;
                } else if (fc.received) {
                    fc.state = State::New;
                } else if (fc.database) {
                    fc.state = State::Missing;
                } else {
                    continue;
                }
                c.fields.push_back(std::move(fc));
            }
            if (r.neighbours && !e.neighbours.empty()) {
                c.neighbours_compared = true;
                c.neighbours = compare_lists(e.neighbours, *r.neighbours);
            }
        }
        v.cells.push_back(std::move(c));
    }
    for (const CellEntry& e : db.entries())
        if (seen.count(e.cell.id) == 0) v.not_seen.push_back(e.cell.id);
    return v;
}

std::vector<LogLine> log_lines(const Verification& v, const CellDatabase& db, const std::vector<ObservedCell>& received,
                               const LogOptions& o) {
    std::vector<LogLine> out;
    for (const CellCheck& c : v.cells) {
        if (c.observed >= received.size()) continue;
        const ObservedCell& r = received[c.observed];
        LogLine base;
        base.time_utc = o.time_utc;
        base.technology = r.technology;
        base.identity = r.key;
        if (c.status != CellCheck::Status::Paired) {
            LogLine l = base;
            l.cell = r.key;
            l.item = "cell";
            l.state = c.status == CellCheck::Status::NotInDatabase ? "not_in_database" : "ambiguous";
            for (const std::string& id : c.candidates) l.database += (l.database.empty() ? "" : " ") + id;
            out.push_back(std::move(l));
            continue;
        }
        const CellEntry* e = db.find(c.entry);
        base.cell = e != nullptr ? e->display_name() : c.entry;
        for (const FieldCheck& f : c.fields) {
            if (f.state == State::Match) continue;
            LogLine l = base;
            l.item = f.field;
            l.state = state_name(f.state);
            l.database = text_of(f.database, o.hex_fields, f.field);
            l.received = text_of(f.received, o.hex_fields, f.field);
            out.push_back(std::move(l));
        }
        for (const NeighbourCheck& n : c.neighbours) {
            if (n.state == State::Match) continue;
            LogLine l = base;
            l.item = "neighbour";
            l.state = state_name(n.state);
            if (n.database) l.database = neighbour_text(*n.database, o.hex_fields);
            if (n.received) l.received = neighbour_text(*n.received, o.hex_fields);
            out.push_back(std::move(l));
        }
    }
    if (o.include_not_seen)
        for (const std::string& id : v.not_seen) {
            const CellEntry* e = db.find(id);
            LogLine l;
            l.time_utc = o.time_utc;
            l.technology = e != nullptr ? e->cell.technology_id : std::string();
            l.cell = e != nullptr ? e->display_name() : id;
            l.identity = id;
            l.item = "cell";
            l.state = "not_seen";
            out.push_back(std::move(l));
        }
    return out;
}

std::string log_header() { return "time_utc,technology,cell,identity,item,state,database,received\r\n"; }

std::string log_csv(const LogLine& l) {
    return csv_value(l.time_utc) + ',' + csv_value(l.technology) + ',' + csv_value(l.cell) + ',' +
           csv_value(l.identity) + ',' + csv_value(l.item) + ',' + csv_value(l.state) + ',' + csv_value(l.database) +
           ',' + csv_value(l.received) + "\r\n";
}

namespace {
// A line without its time: what makes a difference the same one.
std::string dedup_key(LogLine l) {
    l.time_utc.clear();
    return log_csv(l);
}
// The same key from a line of the file ("time,rest").
std::string dedup_key(const std::string& file_line) {
    std::string s = file_line;
    if (!s.empty() && s.back() == '\r') s.pop_back();
    const std::size_t comma = s.find(',');
    return comma == std::string::npos ? std::string() : s.substr(comma) + "\r\n";
}
}  // namespace

DifferenceLog::DifferenceLog(std::filesystem::path path) : path_(std::move(path)) {
    std::ifstream in(path_, std::ios::binary);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) {
            first = false;
            continue;  // header
        }
        if (auto k = dedup_key(line); !k.empty()) written_.insert(std::move(k));
    }
}

std::size_t DifferenceLog::append(const std::vector<LogLine>& lines) {
    std::error_code ec;
    const bool fresh = !std::filesystem::exists(path_, ec) || std::filesystem::file_size(path_, ec) == 0;
    std::ofstream out(path_, std::ios::binary | std::ios::app);
    if (!out) throw CellDbError("cannot write " + path_.string());
    if (fresh) out << log_header();
    std::size_t n = 0;
    for (const LogLine& l : lines) {
        std::string k = dedup_key(l);  // ",technology,...": the time left empty
        if (!written_.insert(k).second) continue;
        out << log_csv(l);
        ++n;
    }
    out.flush();
    if (!out) throw CellDbError("cannot write " + path_.string());
    return n;
}

LearnResult learn(CellDatabase& db, const std::vector<ObservedCell>& received, const Technologies& technologies,
                  const LearnOptions& o) {
    LearnResult res;
    std::vector<CellEntry> added;
    for (const ObservedCell& r : received) {
        const Technology* t = find_technology(technologies, r.technology);
        if (t == nullptr) continue;
        const std::vector<std::string> mf = pairing_fields(db, r.technology, t);
        std::vector<std::string> ids;
        for (const CellEntry& e : db.entries())
            if (e.cell.technology_id == r.technology && fields_pair(e.fields(), r.fields, mf)) ids.push_back(e.cell.id);
        // Also the ones learned in this call (one entry per cell).
        bool in_added = false;
        for (const CellEntry& e : added) in_added = in_added || fields_pair(e.fields(), r.fields, mf);
        if (ids.size() > 1) {
            ++res.ambiguous;
            continue;
        }
        if (ids.empty()) {
            if (!o.add_new || in_added) continue;
            CellEntry e;
            e.cell.technology_id = t->id;
            e.cell.schema_id = t->cell_schema.id;
            e.origin = Origin::Learned;
            e.plugin_version = t->plugin_version;
            for (const std::string& f : learned_fields(*t, o))
                if (const auto it = r.fields.find(f); it != r.fields.end()) e.set_field(f, it->second);
            if (o.neighbours && r.neighbours) {
                e.neighbours = *r.neighbours;
                if (e.neighbours.size() > kMaxNeighbours) e.neighbours.resize(kMaxNeighbours);
            }
            added.push_back(std::move(e));
            continue;
        }
        CellEntry e = *db.find(ids.front());
        bool changed = false;
        if (o.fill_missing)
            for (const std::string& f : learned_fields(*t, o)) {
                const auto it = r.fields.find(f);
                if (it == r.fields.end() || e.field(f)) continue;
                e.set_field(f, it->second);
                changed = true;
            }
        if (o.neighbours && r.neighbours && e.neighbours.empty() && !r.neighbours->empty()) {
            e.neighbours = *r.neighbours;
            if (e.neighbours.size() > kMaxNeighbours) e.neighbours.resize(kMaxNeighbours);
            changed = true;
        }
        if (changed) {
            db.update(e);
            res.extended.push_back(e.cell.id);
        }
    }
    res.added = db.merge(std::move(added));
    return res;
}

}  // namespace yantra::celldb
