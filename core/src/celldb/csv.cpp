// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cell database CSV (csv.h). The RFC 4180 reader and the column rules are
// copied from yantra-v2 68ea20a: core/src/celldb/io.cpp, adapted for
// DB-02 indoor (no coordinates, no KML), the format line, channel columns
// and the project's cell ids.

#include "yantra/core/celldb/csv.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "yantra/core/platform/file.h"
#include "yantra/core/plugin/schema.h"

namespace yantra::celldb {

namespace {

const std::vector<std::string> kGeneric = {"id",     "label", "technology", "schema", "site_name",
                                           "sector", "azimuth_deg", "colour", "notes", "origin",
                                           "plugin_version"};
constexpr std::string_view kChannel = "channel";
constexpr std::string_view kFieldPrefix = "field:";
constexpr std::string_view kNeighbourPrefix = "neighbours.";
constexpr char kListSeparator = '|';

bool is_generic(std::string_view c) {
    return std::find(kGeneric.begin(), kGeneric.end(), c) != kGeneric.end();
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

bool is_hex(const CsvOptions& o, const std::string& f) {
    return std::find(o.hex_fields.begin(), o.hex_fields.end(), f) != o.hex_fields.end();
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t p = s.find(sep, start);
        out.emplace_back(s.substr(start, p == std::string_view::npos ? std::string_view::npos : p - start));
        if (p == std::string_view::npos) break;
        start = p + 1;
    }
    return out;
}

std::string csv_cell(const std::string& s, char sep) {
    if (s.find_first_of(std::string("\"\r\n") + sep) == std::string::npos) return s;
    std::string out = "\"";
    for (const char ch : s) {
        if (ch == '"') out += '"';
        out += ch;
    }
    return out + "\"";
}

struct CsvRow {
    std::size_t line = 0;
    std::vector<std::string> cells;
};

std::vector<CsvRow> parse_csv(std::string_view text, char sep, std::size_t first_line) {
    std::vector<CsvRow> rows;
    std::size_t line = first_line;
    std::size_t i = 0;
    while (i < text.size()) {
        CsvRow row;
        row.line = line;
        std::string cell;
        bool quoted = false, was_quoted = false, end = false;
        while (i < text.size() && !end) {
            const char ch = text[i];
            if (quoted) {
                if (ch == '"') {
                    if (i + 1 < text.size() && text[i + 1] == '"') {
                        cell += '"';
                        i += 2;
                        continue;
                    }
                    quoted = false;
                } else {
                    if (ch == '\n') ++line;
                    cell += ch;
                }
                ++i;
                continue;
            }
            if (ch == '"' && trim(cell).empty() && !was_quoted) {
                cell.clear();
                quoted = was_quoted = true;
            } else if (ch == sep) {
                row.cells.push_back(std::move(cell));
                cell.clear();
                was_quoted = false;
            } else if (ch == '\n' || ch == '\r') {
                if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
                ++line;
                end = true;
            } else {
                cell += ch;
            }
            ++i;
        }
        if (quoted) throw CellDbError("line " + std::to_string(row.line) + ": a quoted value is not closed");
        row.cells.push_back(std::move(cell));
        const bool blank = row.cells.size() == 1 && trim(row.cells[0]).empty();
        if (!blank) rows.push_back(std::move(row));
    }
    return rows;
}

// ',' or ';': the one that occurs more often outside quotes in the first line.
char detect_separator(std::string_view text) {
    std::size_t commas = 0, semicolons = 0;
    bool quoted = false;
    for (const char ch : text) {
        if (ch == '"') quoted = !quoted;
        else if (!quoted && (ch == '\n' || ch == '\r')) break;
        else if (!quoted && ch == ',') ++commas;
        else if (!quoted && ch == ';') ++semicolons;
    }
    return semicolons > commas ? ';' : ',';
}

// The columns of an export.
struct Columns {
    bool channel = false;
    std::vector<std::string> fields;      // technology fields (frequency_hz first when used)
    std::vector<std::string> neighbours;  // neighbour fields
    bool neighbour_channel = false;
};

Columns columns_of(const CellDatabase& db, const CsvOptions& o) {
    Columns c;
    for (const std::string& t : db.technologies()) {
        for (const std::string& f : db.field_names(t))
            if (std::find(c.fields.begin(), c.fields.end(), f) == c.fields.end()) c.fields.push_back(f);
        for (const std::string& f : db.neighbour_field_names(t))
            if (std::find(c.neighbours.begin(), c.neighbours.end(), f) == c.neighbours.end()) c.neighbours.push_back(f);
        if (o.technologies != nullptr && find_technology(*o.technologies, t) != nullptr) {
            c.channel = true;
            c.neighbour_channel = true;
        }
    }
    // frequency_hz always has its column (the main carrier, DB-01).
    if (std::find(c.fields.begin(), c.fields.end(), kCarrierField) == c.fields.end())
        c.fields.insert(c.fields.begin(), kCarrierField);
    if (std::find(c.neighbours.begin(), c.neighbours.end(), kCarrierField) == c.neighbours.end())
        c.neighbour_channel = false;
    return c;
}

std::string field_column(const std::string& f) {
    return is_generic(f) || f == kChannel ? std::string(kFieldPrefix) + f : f;
}

std::string real_text(double v) { return value_text(FieldValue{v}); }

std::string channel_text(const CsvOptions& o, const std::string& tech, const std::optional<FieldValue>& hz) {
    if (!hz || o.technologies == nullptr) return {};
    const Technology* t = find_technology(*o.technologies, tech);
    const auto* i = std::get_if<std::int64_t>(&*hz);
    if (t == nullptr || i == nullptr || *i <= 0) return {};
    const auto ch = t->channel_of_frequency(static_cast<std::uint64_t>(*i));
    return ch ? std::to_string(*ch) : std::string();
}

// One entry as (column, text) pairs, in column order.
std::vector<std::pair<std::string, std::string>> entry_values(const CellEntry& e, const Columns& c, const CsvOptions& o) {
    std::vector<std::pair<std::string, std::string>> v;
    v.emplace_back("id", e.cell.id);
    v.emplace_back("label", e.cell.label);
    v.emplace_back("technology", e.cell.technology_id);
    v.emplace_back("schema", e.cell.schema_id);
    v.emplace_back("site_name", e.site_name);
    v.emplace_back("sector", e.sector);
    v.emplace_back("azimuth_deg", e.azimuth_deg ? real_text(*e.azimuth_deg) : "");
    v.emplace_back("colour", e.colour);
    v.emplace_back("notes", e.notes);
    v.emplace_back("origin", e.cell.id.empty() && e.site_name.empty() ? "" : origin_name(e.origin));
    v.emplace_back("plugin_version", e.plugin_version);
    for (const std::string& f : c.fields) {
        const auto val = e.field(f);
        v.emplace_back(field_column(f), val ? value_text(*val, is_hex(o, f)) : "");
        if (f == kCarrierField && c.channel)
            v.emplace_back(std::string(kChannel), channel_text(o, e.cell.technology_id, val));
    }
    auto list_of = [&](auto&& text_of) {
        std::string list;
        for (std::size_t k = 0; k < e.neighbours.size(); ++k) {
            if (k) list += kListSeparator;
            list += text_of(e.neighbours[k]);
        }
        return list;
    };
    for (const std::string& f : c.neighbours) {
        v.emplace_back(std::string(kNeighbourPrefix) + f, list_of([&](const NeighbourRef& r) {
            const auto it = r.fields.find(f);
            return it == r.fields.end() || r.inherited.count(f) != 0 ? std::string() : value_text(it->second, is_hex(o, f));
        }));
        if (f == kCarrierField && c.neighbour_channel)
            v.emplace_back(std::string(kNeighbourPrefix) + std::string(kChannel), list_of([&](const NeighbourRef& r) {
                const auto it = r.fields.find(f);
                return it == r.fields.end() ? std::string() : channel_text(o, e.cell.technology_id, it->second);
            }));
    }
    return v;
}

// The frequency of a channel text of technology `tech`; throws with the reason.
std::int64_t frequency_of(const CsvOptions& o, const std::string& tech, std::string_view text) {
    const auto v = parse_value(text);
    const auto* ch = v ? std::get_if<std::int64_t>(&*v) : nullptr;
    if (ch == nullptr || *ch < 0) throw CellDbError("channel '" + std::string(trim(text)) + "' is not a channel number");
    const Technology* t = o.technologies != nullptr ? find_technology(*o.technologies, tech) : nullptr;
    if (t == nullptr) throw CellDbError("a channel number needs the technology plugin of '" + tech + "'");
    const auto hz = t->frequency_of_channel(static_cast<std::uint64_t>(*ch));
    if (!hz) throw CellDbError("channel " + std::to_string(*ch) + " is not a channel of " + tech);
    return static_cast<std::int64_t>(*hz);
}

// A frequency and a channel given together must name the same carrier.
void check_channel(const CsvOptions& o, const std::string& tech, std::int64_t hz, std::string_view channel) {
    const Technology* t = o.technologies != nullptr ? find_technology(*o.technologies, tech) : nullptr;
    if (t == nullptr || hz <= 0) return;  // nothing to check against
    const auto ch = t->channel_of_frequency(static_cast<std::uint64_t>(hz));
    const auto given = parse_value(channel);
    const auto* g = given ? std::get_if<std::int64_t>(&*given) : nullptr;
    if (!ch || g == nullptr || static_cast<std::uint64_t>(*g) != *ch)
        throw CellDbError("channel " + std::string(trim(channel)) + " is not the carrier " + std::to_string(hz) + " Hz");
}

// An entry from (column, text) pairs; throws CellDbError with the reason.
CellEntry entry_from(const std::map<std::string, std::string>& row, const CsvOptions& o) {
    CellEntry e;
    e.origin = Origin::Import;
    auto get = [&](std::string_view k) -> std::string {
        const auto it = row.find(std::string(k));
        return it == row.end() ? std::string() : std::string(trim(it->second));
    };
    e.cell.id = get("id");
    e.cell.label = get("label");
    e.cell.technology_id = get("technology");
    if (e.cell.technology_id.empty()) e.cell.technology_id = o.technology;
    e.cell.schema_id = get("schema");
    if (e.cell.schema_id.empty()) e.cell.schema_id = o.schema;
    e.site_name = get("site_name");
    e.sector = get("sector");
    e.colour = get("colour");
    if (const auto it = row.find("notes"); it != row.end()) e.notes = it->second;
    e.plugin_version = get("plugin_version");
    if (const std::string origin = get("origin"); !origin.empty()) {
        const auto og = origin_from_name(origin);
        if (!og) throw CellDbError("origin '" + origin + "' is not manual, import or learned");
        e.origin = *og;
    }
    if (const std::string az = get("azimuth_deg"); !az.empty()) {
        const auto v = parse_value(az);
        if (!v || std::holds_alternative<std::string>(*v)) throw CellDbError("azimuth '" + az + "' is not a number");
        e.azimuth_deg = std::holds_alternative<double>(*v) ? std::get<double>(*v)
                                                         : static_cast<double>(std::get<std::int64_t>(*v));
    }
    const std::string& tech = e.cell.technology_id;
    std::map<std::string, std::vector<std::string>> lists;
    std::string channel;
    for (const auto& [col, text] : row) {
        if (is_generic(col)) continue;
        if (col == kChannel) {
            channel = std::string(trim(text));
            continue;
        }
        if (col.rfind(kNeighbourPrefix, 0) == 0) {
            if (const std::string_view t = trim(text); !t.empty())
                lists[col.substr(kNeighbourPrefix.size())] = split(t, kListSeparator);
            continue;
        }
        const std::string name = col.rfind(kFieldPrefix, 0) == 0 ? col.substr(kFieldPrefix.size()) : col;
        auto v = parse_value(text);
        if (!v) continue;
        if (name == kCarrierField) {
            const auto* hz = std::get_if<std::int64_t>(&*v);
            if (hz == nullptr || *hz < 0) throw CellDbError("frequency '" + std::string(trim(text)) + "' is not a whole number of Hz");
        }
        e.set_field(name, std::move(*v));
    }
    if (!channel.empty()) {
        if (e.cell.carrier_hz != 0) check_channel(o, tech, static_cast<std::int64_t>(e.cell.carrier_hz), channel);
        else e.cell.carrier_hz = static_cast<std::uint64_t>(frequency_of(o, tech, channel));
    }
    std::size_t n = 0;
    for (const auto& [f, l] : lists) n = std::max(n, l.size());
    if (n > kMaxNeighbours) throw CellDbError("more than " + std::to_string(kMaxNeighbours) + " neighbours");
    const auto ch_list = lists.find(std::string(kChannel));
    for (std::size_t k = 0; k < n; ++k) {
        NeighbourRef r;
        for (const auto& [f, l] : lists) {
            if (f == kChannel || k >= l.size()) continue;
            if (auto v = parse_value(l[k])) r.fields[f] = std::move(*v);
        }
        if (ch_list != lists.end() && k < ch_list->second.size() && !trim(ch_list->second[k]).empty()) {
            const auto hz = r.fields.find(kCarrierField);
            if (hz != r.fields.end()) {
                const auto* i = std::get_if<std::int64_t>(&hz->second);
                if (i == nullptr) throw CellDbError("neighbour " + std::to_string(k + 1) + ": the frequency is not a number of Hz");
                check_channel(o, tech, *i, ch_list->second[k]);
            } else {
                r.fields[kCarrierField] = frequency_of(o, tech, ch_list->second[k]);
            }
        }
        if (r.fields.empty()) throw CellDbError("neighbour " + std::to_string(k + 1) + " is empty");
        e.neighbours.push_back(std::move(r));
    }
    if (auto err = e.validate(); !err.empty()) throw CellDbError(err);
    return e;
}

// "" when a column is usable, else why it is ignored.
std::string check_column(const std::string& c) {
    if (is_generic(c) || c == kChannel) return {};
    std::string name = c;
    if (c.rfind(kNeighbourPrefix, 0) == 0) name = c.substr(kNeighbourPrefix.size());
    else if (c.rfind(kFieldPrefix, 0) == 0) name = c.substr(kFieldPrefix.size());
    if (!plugin::is_identifier(name)) return "not an identifier";
    if (plugin::names_out_of_scope_data(name)) return "names data outside Yantra's scope";
    return {};
}

}  // namespace

std::string export_csv(const CellDatabase& db, const CsvOptions& o) {
    const char sep = o.separator == 0 ? ',' : o.separator;
    if (sep != ',' && sep != ';') throw CellDbError("the separator must be ',' or ';'");
    const Columns c = columns_of(db, o);
    const std::string s(1, sep);
    std::string out = std::string(kCsvFormat) + s + std::to_string(kCsvVersion);
    out.insert(0, "#");
    out += "\r\n";
    const auto header = entry_values(CellEntry{}, c, o);
    for (std::size_t k = 0; k < header.size(); ++k) out += (k ? s : "") + header[k].first;
    out += "\r\n";
    for (const CellEntry& e : db.entries()) {
        const auto v = entry_values(e, c, o);
        for (std::size_t k = 0; k < v.size(); ++k) out += (k ? s : "") + csv_cell(v[k].second, sep);
        out += "\r\n";
    }
    return out;
}

CsvImport import_csv(std::string_view text, const CsvOptions& o) {
    if (o.separator != 0 && o.separator != ',' && o.separator != ';')
        throw CellDbError("the separator must be ',' or ';'");
    if (text.size() > o.max_bytes) throw CellDbError("the file is larger than " + std::to_string(o.max_bytes) + " bytes");
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    std::size_t first_line = 1;
    if (!text.empty() && text.front() == '#') {
        const std::size_t eol = text.find_first_of("\r\n");
        const std::string_view head = text.substr(1, eol == std::string_view::npos ? std::string_view::npos : eol - 1);
        const auto parts = split(head, head.find(';') != std::string_view::npos ? ';' : ',');
        if (trim(parts[0]) == kCsvFormat) {
            const auto v = parts.size() > 1 ? parse_value(parts[1]) : std::nullopt;
            const auto* ver = v ? std::get_if<std::int64_t>(&*v) : nullptr;
            if (ver == nullptr || *ver < 1) throw CellDbError("line 1: the format version is missing");
            if (*ver > kCsvVersion)
                throw CellDbError("written by a newer Yantra (cell database format " + std::to_string(*ver) +
                                  ", this version reads up to " + std::to_string(kCsvVersion) + ")");
            text.remove_prefix(eol == std::string_view::npos ? text.size() : eol);
            first_line = 1;  // the line break that follows counts the line
            if (!text.empty() && text.front() == '\r') text.remove_prefix(1);
            if (!text.empty() && text.front() == '\n') text.remove_prefix(1);
            first_line = 2;
        }
    }
    const char sep = o.separator != 0 ? o.separator : detect_separator(text);
    const std::vector<CsvRow> rows = parse_csv(text, sep, first_line);
    if (rows.empty()) throw CellDbError("the file has no header line");
    std::vector<std::string> cols;
    std::vector<bool> use;
    CsvImport res;
    for (const std::string& raw : rows[0].cells) {
        std::string c(trim(raw));
        if (std::find(cols.begin(), cols.end(), c) != cols.end()) throw CellDbError("column '" + c + "' appears twice");
        const std::string why = check_column(c);
        if (!why.empty()) res.problems.push_back("column '" + c + "' ignored: " + why);
        use.push_back(why.empty());
        cols.push_back(std::move(c));
    }
    if (rows.size() - 1 > kMaxEntries) throw CellDbError("more than " + std::to_string(kMaxEntries) + " cells");
    std::set<std::string> ids;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const CsvRow& row = rows[r];
        const std::string where = "line " + std::to_string(row.line) + ": ";
        if (row.cells.size() > cols.size()) {
            res.problems.push_back(where + "more values than columns");
            continue;
        }
        std::map<std::string, std::string> m;
        for (std::size_t k = 0; k < row.cells.size(); ++k)
            if (use[k]) m[cols[k]] = row.cells[k];
        try {
            CellEntry e = entry_from(m, o);
            if (!e.cell.id.empty() && !ids.insert(e.cell.id).second)
                throw CellDbError("the id " + e.cell.id + " is used by an earlier row");
            res.entries.push_back(std::move(e));
        } catch (const CellDbError& e) {
            res.problems.push_back(where + e.what());
        }
    }
    return res;
}

CellDatabase read_csv_file(const std::string& path, const CsvOptions& options, std::vector<std::string>* problems) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw CellDbError("cannot read " + path);
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > options.max_bytes)
        throw CellDbError(path + ": the file is larger than " + std::to_string(options.max_bytes) + " bytes");
    in.seekg(0);
    std::string text(static_cast<std::size_t>(size), '\0');
    in.read(text.data(), size);
    CsvImport imp = import_csv(text, options);
    CellDatabase db;
    db.merge(std::move(imp.entries));
    if (problems != nullptr) *problems = std::move(imp.problems);
    return db;
}

void write_csv_file(const CellDatabase& db, const std::string& path, const CsvOptions& options) {
    const std::string text = export_csv(db, options);
    const std::string tmp = path + ".part";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw CellDbError("cannot write " + tmp);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) throw CellDbError("cannot write " + tmp);
    }
    std::string err;
    if (!platform::sync_file(tmp, err) || !platform::rename_replace(tmp, path, err)) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        throw CellDbError(path + ": " + err);
    }
}

}  // namespace yantra::celldb
