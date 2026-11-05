// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cell database model (database.h). Value helpers and validation copied
// from yantra-v2 68ea20a: core/src/celldb/database.cpp, adapted for DB-01
// indoor (no position) and the project's cells (string ids).

#include "yantra/core/celldb/database.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <system_error>

#include "yantra/core/celldb/csv.h"
#include "yantra/core/plugin/schema.h"
#include "yantra/core/util/sha256.h"

namespace yantra::celldb {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

bool valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t n = 0;
        if (c < 0x80) n = 0;
        else if ((c & 0xE0) == 0xC0 && c >= 0xC2) n = 1;
        else if ((c & 0xF0) == 0xE0) n = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) n = 3;
        else return false;
        if (n > 0 && i + n >= s.size()) return false;
        for (std::size_t k = 1; k <= n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n + 1;
    }
    return true;
}

std::string check_text(std::string_view what, std::string_view s, std::size_t limit) {
    if (s.size() > limit) return std::string(what) + " is longer than " + std::to_string(limit) + " bytes";
    if (!valid_utf8(s)) return std::string(what) + " is not valid UTF-8";
    return {};
}

std::string check_name(std::string_view what, std::string_view name) {
    if (!plugin::is_identifier(name)) return std::string(what) + " name '" + std::string(name) + "' is not an identifier";
    if (plugin::names_out_of_scope_data(name))
        return std::string(what) + " name '" + std::string(name) + "' names data outside Yantra's scope";
    return {};
}

std::string check_fields(std::string_view what, const FieldMap& m) {
    for (const auto& [name, v] : m) {
        if (auto e = check_name(what, name); !e.empty()) return e;
        if (const auto* d = std::get_if<double>(&v); d && !std::isfinite(*d))
            return std::string(what) + " '" + name + "' is not a finite number";
        if (const auto* s = std::get_if<std::string>(&v))
            if (auto e = check_text(std::string(what) + " '" + name + "'", *s, kMaxTextBytes); !e.empty()) return e;
    }
    return {};
}

// n of "cell-<n>", 0 for other ids.
std::uint64_t id_number(std::string_view id) {
    constexpr std::string_view p = "cell-";
    if (id.substr(0, p.size()) != p) return 0;
    std::uint64_t n = 0;
    const auto r = std::from_chars(id.data() + p.size(), id.data() + id.size(), n);
    return r.ec == std::errc() && r.ptr == id.data() + id.size() ? n : 0;
}

}  // namespace

bool is_identity_field(std::string_view name) noexcept {
    return name == "mcc" || name == "mnc" || name == "la" || name == "colour_code";
}

bool values_equal(const FieldValue& a, const FieldValue& b) {
    const auto* sa = std::get_if<std::string>(&a);
    const auto* sb = std::get_if<std::string>(&b);
    if (sa || sb) return sa && sb && *sa == *sb;
    const auto* ia = std::get_if<std::int64_t>(&a);
    const auto* ib = std::get_if<std::int64_t>(&b);
    if (ia && ib) return *ia == *ib;
    const double x = ia ? static_cast<double>(*ia) : std::get<double>(a);
    const double y = ib ? static_cast<double>(*ib) : std::get<double>(b);
    return std::abs(x - y) <= 1e-9 * std::max({1.0, std::abs(x), std::abs(y)});
}

std::string value_text(const FieldValue& v, bool hex) {
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        if (hex && *i >= 0) {
            char b[32];
            std::snprintf(b, sizeof b, "0x%llX", static_cast<unsigned long long>(*i));
            return b;
        }
        return std::to_string(*i);
    }
    if (const auto* d = std::get_if<double>(&v)) {
        char b[64];
        const auto r = std::to_chars(b, b + sizeof b, *d);
        std::string s(b, r.ptr);
        if (s.find_first_of(".eEni") == std::string::npos) s += ".0";  // stays a real when read back
        return s;
    }
    return std::get<std::string>(v);
}

std::optional<FieldValue> parse_value(std::string_view text) {
    const std::string_view t = trim(text);
    if (t.empty()) return std::nullopt;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        std::uint64_t u = 0;
        const auto r = std::from_chars(t.data() + 2, t.data() + t.size(), u, 16);
        if (r.ec == std::errc() && r.ptr == t.data() + t.size() &&
            u <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return FieldValue{static_cast<std::int64_t>(u)};
        return FieldValue{std::string(t)};
    }
    const char* b = t.data();
    if (*b == '+') ++b;
    {
        std::int64_t i = 0;
        const auto r = std::from_chars(b, t.data() + t.size(), i);
        if (r.ec == std::errc() && r.ptr == t.data() + t.size()) return FieldValue{i};
    }
    {
        double d = 0.0;
        const auto r = std::from_chars(b, t.data() + t.size(), d);
        if (r.ec == std::errc() && r.ptr == t.data() + t.size() && std::isfinite(d)) return FieldValue{d};
    }
    return FieldValue{std::string(t)};
}

std::optional<FieldValue> from_record_value(const plugin::Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) return FieldValue{std::int64_t{*b ? 1 : 0}};
    if (const auto* i = std::get_if<std::int64_t>(&v)) return FieldValue{*i};
    if (const auto* u = std::get_if<std::uint64_t>(&v)) {
        if (*u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return FieldValue{static_cast<double>(*u)};
        return FieldValue{static_cast<std::int64_t>(*u)};
    }
    if (const auto* d = std::get_if<double>(&v)) return FieldValue{*d};
    if (const auto* s = std::get_if<std::string>(&v)) return FieldValue{*s};
    if (const auto* e = std::get_if<plugin::EnumValue>(&v)) return FieldValue{e->value};
    return std::nullopt;
}

std::string neighbour_text(const NeighbourRef& ref, const std::vector<std::string>& hex_fields) {
    std::string out;
    for (const auto& [k, v] : ref.fields) {
        if (ref.inherited.count(k) != 0) continue;
        if (!out.empty()) out += '/';
        const bool hex = std::find(hex_fields.begin(), hex_fields.end(), k) != hex_fields.end();
        out += k + "=" + value_text(v, hex);
    }
    return out;
}

const char* origin_name(Origin o) noexcept {
    switch (o) {
    case Origin::Manual: return "manual";
    case Origin::Import: return "import";
    case Origin::Learned: return "learned";
    }
    return "manual";
}

std::optional<Origin> origin_from_name(std::string_view name) noexcept {
    if (name == "manual") return Origin::Manual;
    if (name == "import") return Origin::Import;
    if (name == "learned") return Origin::Learned;
    return std::nullopt;
}

// ---- CellEntry ----------------------------------------------------------------

FieldMap CellEntry::fields() const {
    FieldMap m = parameters;
    for (const auto& [k, v] : cell.identity) m[k] = v;
    if (cell.carrier_hz != 0 && cell.carrier_hz <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        m[kCarrierField] = static_cast<std::int64_t>(cell.carrier_hz);
    return m;
}

std::optional<FieldValue> CellEntry::field(std::string_view name) const {
    if (name == kCarrierField) {
        if (cell.carrier_hz == 0) return std::nullopt;
        return FieldValue{static_cast<std::int64_t>(cell.carrier_hz)};
    }
    if (auto it = cell.identity.find(std::string(name)); it != cell.identity.end()) return FieldValue{it->second};
    if (auto it = parameters.find(name); it != parameters.end()) return it->second;
    return std::nullopt;
}

void CellEntry::set_field(const std::string& name, FieldValue value) {
    erase_field(name);
    if (name == kCarrierField) {
        const auto* i = std::get_if<std::int64_t>(&value);
        if (i == nullptr || *i < 0) throw CellDbError("the carrier frequency is not a whole number of Hz");
        cell.carrier_hz = static_cast<std::uint64_t>(*i);
        return;
    }
    if (const auto* i = std::get_if<std::int64_t>(&value); i != nullptr && is_identity_field(name)) {
        cell.identity[name] = *i;
        return;
    }
    parameters[name] = std::move(value);
}

void CellEntry::erase_field(std::string_view name) {
    if (name == kCarrierField) cell.carrier_hz = 0;
    cell.identity.erase(std::string(name));
    if (auto it = parameters.find(name); it != parameters.end()) parameters.erase(it);
}

std::string CellEntry::display_name() const {
    if (!cell.label.empty()) return cell.label;
    std::string s = site_name;
    if (!sector.empty()) s += (s.empty() ? "" : " ") + sector;
    return s.empty() ? cell.id : s;
}

std::string CellEntry::validate() const {
    if (!cell.technology_id.empty() && !plugin::is_identifier(cell.technology_id))
        return "the technology id '" + cell.technology_id + "' is not an identifier";
    if (!cell.schema_id.empty() && !plugin::is_identifier(cell.schema_id))
        return "the schema id '" + cell.schema_id + "' is not an identifier";
    if (auto e = check_text("the id", cell.id, kMaxTextBytes); !e.empty()) return e;
    if (auto e = check_text("the label", cell.label, kMaxTextBytes); !e.empty()) return e;
    if (auto e = check_text("the plugin version", plugin_version, kMaxTextBytes); !e.empty()) return e;
    if (auto e = check_text("the site name", site_name, kMaxTextBytes); !e.empty()) return e;
    if (auto e = check_text("the sector", sector, kMaxTextBytes); !e.empty()) return e;
    if (auto e = check_text("the notes", notes, kMaxNotesBytes); !e.empty()) return e;
    if (azimuth_deg && (!std::isfinite(*azimuth_deg) || *azimuth_deg < 0.0 || *azimuth_deg >= 360.0))
        return "the azimuth is not within 0..360 degrees";
    if (!colour.empty()) {
        bool ok = colour.size() == 7 && colour[0] == '#';
        for (std::size_t i = 1; ok && i < colour.size(); ++i) ok = std::isxdigit(static_cast<unsigned char>(colour[i])) != 0;
        if (!ok) return "the colour '" + colour + "' is not #rrggbb";
    }
    for (const auto& [name, v] : cell.identity) {
        (void)v;
        if (auto e = check_name("identity field", name); !e.empty()) return e;
    }
    if (auto e = check_fields("field", parameters); !e.empty()) return e;
    for (const auto& [name, v] : parameters) {
        (void)v;
        if (name == kCarrierField) return "the carrier frequency is kept as the main carrier, not as a field";
    }
    if (neighbours.size() > kMaxNeighbours) return "more than " + std::to_string(kMaxNeighbours) + " neighbours";
    for (const NeighbourRef& n : neighbours) {
        if (n.fields.empty()) return "a neighbour reference is empty";
        if (auto e = check_fields("neighbour field", n.fields); !e.empty()) return e;
    }
    return {};
}

// ---- CellDatabase ---------------------------------------------------------------

const CellEntry* CellDatabase::find(std::string_view id) const {
    for (const CellEntry& e : entries_)
        if (e.cell.id == id) return &e;
    return nullptr;
}

std::string CellDatabase::add(CellEntry entry) {
    if (entries_.size() >= kMaxEntries) throw CellDbError("more than " + std::to_string(kMaxEntries) + " cells");
    if (entry.cell.id.empty()) {
        std::uint64_t n = 0;
        for (const CellEntry& e : entries_) n = std::max(n, id_number(e.cell.id));
        do {
            entry.cell.id = "cell-" + std::to_string(++n);
        } while (find(entry.cell.id) != nullptr);
    } else if (find(entry.cell.id) != nullptr) {
        throw CellDbError("the cell id " + entry.cell.id + " is used");
    }
    if (auto e = entry.validate(); !e.empty()) throw CellDbError(entry.cell.id + ": " + e);
    entries_.push_back(std::move(entry));
    return entries_.back().cell.id;
}

void CellDatabase::update(const CellEntry& entry) {
    if (auto e = entry.validate(); !e.empty()) throw CellDbError(entry.cell.id + ": " + e);
    for (CellEntry& e : entries_)
        if (e.cell.id == entry.cell.id) {
            e = entry;
            return;
        }
    throw CellDbError("no cell " + entry.cell.id);
}

bool CellDatabase::remove(std::string_view id) {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const CellEntry& e) { return e.cell.id == id; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
}

void CellDatabase::clear() {
    entries_.clear();
    match_.clear();
}

std::vector<std::string> CellDatabase::merge(std::vector<CellEntry> entries) {
    CellDatabase next = *this;
    std::vector<std::string> ids;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        CellEntry& e = entries[i];
        try {
            if (!e.cell.id.empty() && next.find(e.cell.id) != nullptr) {
                next.update(e);
                ids.push_back(e.cell.id);
            } else {
                ids.push_back(next.add(std::move(e)));
            }
        } catch (const CellDbError& err) {
            throw CellDbError("entry " + std::to_string(i + 1) + ": " + err.what());
        }
    }
    *this = std::move(next);
    return ids;
}

void CellDatabase::set_match_fields(const std::string& technology, std::vector<std::string> fields) {
    for (const std::string& f : fields)
        if (!plugin::is_identifier(f)) throw CellDbError("match field '" + f + "' is not an identifier");
    if (fields.empty()) match_.erase(technology);
    else match_[technology] = std::move(fields);
}

std::vector<std::string> CellDatabase::match_fields(std::string_view technology) const {
    const auto it = match_.find(technology);
    return it == match_.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> CellDatabase::technologies() const {
    std::vector<std::string> out;
    for (const CellEntry& e : entries_)
        if (std::find(out.begin(), out.end(), e.cell.technology_id) == out.end()) out.push_back(e.cell.technology_id);
    return out;
}

std::vector<std::string> CellDatabase::field_names(std::string_view technology) const {
    bool carrier = false;
    std::set<std::string> identity;
    std::set<std::string> other;
    for (const CellEntry& e : entries_) {
        if (e.cell.technology_id != technology) continue;
        carrier = carrier || e.cell.carrier_hz != 0;
        for (const auto& [k, v] : e.cell.identity) identity.insert(k);
        for (const auto& [k, v] : e.parameters) other.insert(k);
    }
    std::vector<std::string> out;
    if (carrier) out.emplace_back(kCarrierField);
    for (const char* k : {"mcc", "mnc", "la", "colour_code"})
        if (identity.count(k) != 0) out.emplace_back(k);
    for (const std::string& k : identity)
        if (!is_identity_field(k)) out.push_back(k);
    for (const std::string& k : other)
        if (identity.count(k) == 0) out.push_back(k);
    return out;
}

std::vector<std::string> CellDatabase::neighbour_field_names(std::string_view technology) const {
    std::vector<std::string> out;
    for (const CellEntry& e : entries_) {
        if (e.cell.technology_id != technology) continue;
        for (const NeighbourRef& r : e.neighbours)
            for (const auto& [k, v] : r.fields)
                if (r.inherited.count(k) == 0 && std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    }
    // frequency_hz and the identity first, as the cell columns.
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        auto rank = [](const std::string& s) { return s == kCarrierField ? 0 : is_identity_field(s) ? 1 : 2; };
        return rank(a) < rank(b);
    });
    return out;
}

std::string fingerprint(const CellDatabase& db) {
    return util::Sha256::of(export_csv(db));
}

std::string describe(const CellDatabase& db, const std::string& name) {
    if (db.entries().empty() && name.empty()) return {};
    const std::string n = name.empty() ? std::string("cell database") : name;
    return n + " (" + std::to_string(db.entries().size()) + " cells, sha256 " + fingerprint(db).substr(0, 12) + ")";
}

}  // namespace yantra::celldb
