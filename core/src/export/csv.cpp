// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/export/csv.h"

#include <algorithm>
#include <array>

namespace yantra::exports {

namespace {

constexpr std::array<std::pair<Column, const char*>, 17> kColumns{{
    {Column::Time, "time"},
    {Column::Floor, "floor"},
    {Column::Position, "position"},
    {Column::PlanPosition, "plan_position"},
    {Column::Cell, "cell"},
    {Column::Channel, "channel"},
    {Column::Frequency, "frequency"},
    {Column::Level, "level"},
    {Column::C1, "c1"},
    {Column::Quality, "quality"},
    {Column::ServingChange, "serving_change"},
    {Column::Neighbours, "neighbours"},
    {Column::Marker, "marker"},
    {Column::AntennaCorrection, "antenna_correction"},
    {Column::Label, "label"},
    {Column::Kind, "kind"},
    {Column::Session, "session"},
}};

bool has(const CsvOptions& o, Column c) { return std::find(o.columns.begin(), o.columns.end(), c) != o.columns.end(); }

void add_unique(std::vector<std::string>& v, const std::string& s) {
    if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

std::string num(const std::optional<double>& v) { return v ? number_text(*v) : std::string(kInvalidValue); }
std::string unum(const std::optional<std::uint64_t>& v) { return v ? std::to_string(*v) : std::string(kInvalidValue); }

std::string quoted(const std::string& s, char sep) {
    if (s.find_first_of(std::string{sep, '"', '\n', '\r'}) == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += '"';
        o += c;
    }
    return o + "\"";
}

}  // namespace

const char* column_id(Column c) noexcept {
    for (const auto& [col, id] : kColumns)
        if (col == c) return id;
    return "";
}

std::optional<Column> column_from_id(std::string_view id) noexcept {
    for (const auto& [col, name] : kColumns)
        if (id == name) return col;
    return std::nullopt;
}

std::vector<Column> all_columns() {
    std::vector<Column> v;
    for (const auto& [col, id] : kColumns) v.push_back(col);
    return v;
}

std::vector<Column> default_columns() {
    return {Column::Time,    Column::Floor,         Column::Position,   Column::Cell,   Column::Channel,
            Column::Level,   Column::C1,            Column::Quality,    Column::ServingChange,
            Column::Neighbours, Column::Marker,     Column::AntennaCorrection};
}

std::string validate(const CsvOptions& o) {
    if (o.columns.empty()) return "no columns selected";
    for (std::size_t i = 0; i < o.columns.size(); ++i)
        for (std::size_t j = i + 1; j < o.columns.size(); ++j)
            if (o.columns[i] == o.columns[j]) return std::string("column '") + column_id(o.columns[i]) + "' twice";
    if (o.separator != ',' && o.separator != ';' && o.separator != '\t' && o.separator != '|')
        return "the separator must be ',', ';', a tab or '|'";
    if (o.neighbours > 32) return "at most 32 neighbours";
    return {};
}

CsvLayout csv_layout(const std::vector<Row>& rows, const CsvOptions& o) {
    CsvLayout l;
    l.cell_fields = o.cell_fields;
    l.quality_fields = o.quality_fields;
    if (l.cell_fields.empty())
        for (const Row& r : rows) {
            if (r.serving)
                for (const auto& [k, v] : r.serving->cell) add_unique(l.cell_fields, k);
            for (std::size_t i = 0; i < r.neighbours.size() && i < o.neighbours; ++i)
                for (const auto& [k, v] : r.neighbours[i].cell) add_unique(l.cell_fields, k);
        }
    if (l.quality_fields.empty())
        for (const Row& r : rows)
            if (r.serving)
                for (const QualityValue& q : r.serving->quality) add_unique(l.quality_fields, q.field);
    auto& h = l.header;
    for (Column c : o.columns) {
        switch (c) {
        case Column::Time: h.push_back(o.local_time ? "time_local" : "time_utc"); break;
        case Column::Floor: h.push_back("floor"); break;
        case Column::Position: h.insert(h.end(), {"x_m", "y_m"}); break;
        case Column::PlanPosition: h.insert(h.end(), {"x_plan", "y_plan"}); break;
        case Column::Cell:
            h.insert(h.end(), l.cell_fields.begin(), l.cell_fields.end());
            break;
        case Column::Channel: h.push_back("channel"); break;
        case Column::Frequency: h.push_back("frequency_hz"); break;
        case Column::Level: h.insert(h.end(), {"rssi", "rssi_unit"}); break;
        case Column::C1: h.push_back("c1_db"); break;
        case Column::Quality: h.insert(h.end(), l.quality_fields.begin(), l.quality_fields.end()); break;
        case Column::ServingChange: h.push_back("serving_change"); break;
        case Column::Neighbours:
            for (std::size_t i = 1; i <= o.neighbours; ++i) {
                const std::string p = "nb" + std::to_string(i) + "_";
                for (const std::string& f : l.cell_fields) h.push_back(p + f);
                h.push_back(p + "channel");
                if (has(o, Column::Frequency)) h.push_back(p + "frequency_hz");
                h.push_back(p + "rssi");
                h.push_back(p + "c1_db");
            }
            break;
        case Column::Marker: h.push_back("marker"); break;
        case Column::AntennaCorrection: h.push_back("antenna_correction_db"); break;
        case Column::Label: h.push_back("label"); break;
        case Column::Kind: h.push_back("kind"); break;
        case Column::Session: h.push_back("session"); break;
        }
    }
    return l;
}

std::vector<std::string> csv_values(const Row& r, const CsvLayout& l, const CsvOptions& o) {
    std::vector<std::string> v;
    v.reserve(l.header.size());
    const CarrierValues* s = r.serving ? &*r.serving : nullptr;
    for (Column c : o.columns) {
        switch (c) {
        case Column::Time: v.push_back(format_time(r.utc_ns, o.local_time)); break;
        case Column::Floor: v.push_back(r.floor_id.empty() ? std::string(kInvalidValue) : r.floor_name); break;
        case Column::Position:
            v.push_back(r.metres ? number_text(r.metres->x) : kInvalidValue);
            v.push_back(r.metres ? number_text(r.metres->y) : kInvalidValue);
            break;
        case Column::PlanPosition:
            v.push_back(r.plan ? number_text(r.plan->x) : kInvalidValue);
            v.push_back(r.plan ? number_text(r.plan->y) : kInvalidValue);
            break;
        case Column::Cell:
            for (const std::string& f : l.cell_fields) {
                const std::string* x = s != nullptr ? s->cell_value(f) : nullptr;
                v.push_back(x != nullptr ? *x : std::string(kInvalidValue));
            }
            break;
        case Column::Channel: v.push_back(s != nullptr ? unum(s->channel) : kInvalidValue); break;
        case Column::Frequency:
            v.push_back(s != nullptr && s->frequency_hz != 0 ? std::to_string(s->frequency_hz) : kInvalidValue);
            break;
        case Column::Level:
            v.push_back(s != nullptr ? num(s->level) : kInvalidValue);
            v.push_back(s != nullptr && s->level ? s->unit : kInvalidValue);
            break;
        case Column::C1: v.push_back(s != nullptr ? num(s->c1) : kInvalidValue); break;
        case Column::Quality:
            for (const std::string& f : l.quality_fields) v.push_back(s != nullptr ? num(s->quality_value(f)) : kInvalidValue);
            break;
        case Column::ServingChange: v.push_back(r.serving_change ? "1" : "0"); break;
        case Column::Neighbours:
            for (std::size_t i = 0; i < o.neighbours; ++i) {
                const CarrierValues* n = i < r.neighbours.size() ? &r.neighbours[i] : nullptr;
                for (const std::string& f : l.cell_fields) {
                    const std::string* x = n != nullptr ? n->cell_value(f) : nullptr;
                    v.push_back(x != nullptr ? *x : std::string(kInvalidValue));
                }
                v.push_back(n != nullptr ? unum(n->channel) : kInvalidValue);
                if (has(o, Column::Frequency))
                    v.push_back(n != nullptr && n->frequency_hz != 0 ? std::to_string(n->frequency_hz) : kInvalidValue);
                v.push_back(n != nullptr ? num(n->level) : kInvalidValue);
                v.push_back(n != nullptr ? num(n->c1) : kInvalidValue);
            }
            break;
        case Column::Marker: v.push_back(r.marker); break;
        case Column::AntennaCorrection: v.push_back(num(r.antenna_correction_db)); break;
        case Column::Label: v.push_back(r.label); break;
        case Column::Kind: v.push_back(r.kind); break;
        case Column::Session: v.push_back(r.session_id != 0 ? std::to_string(r.session_id) : kInvalidValue); break;
        }
    }
    return v;
}

std::string csv_text(const std::vector<Row>& rows, const CsvOptions& o) {
    if (const std::string e = validate(o); !e.empty()) throw ExportError("CSV export: " + e);
    const CsvLayout l = csv_layout(rows, o);
    std::string out;
    auto line = [&](const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (i > 0) out += o.separator;
            out += quoted(cells[i], o.separator);
        }
        out += '\n';
    };
    if (o.header) line(l.header);
    for (const Row& r : rows) line(csv_values(r, l, o));
    return out;
}

void write_csv(const std::filesystem::path& path, const std::vector<Row>& rows, const CsvOptions& o) {
    write_file(path, csv_text(rows, o));
}

std::optional<std::size_t> CsvTable::column(std::string_view name) const {
    for (std::size_t i = 0; i < header.size(); ++i)
        if (header[i] == name) return i;
    return std::nullopt;
}

CsvTable read_csv(std::string_view text, char sep) {
    std::vector<std::vector<std::string>> lines;
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false, any = false;
    std::size_t line_no = 1;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                if (c == '\n') ++line_no;
                field += c;
            }
            continue;
        }
        if (c == '"' && field.empty()) {
            in_quotes = true;
            any = true;
        } else if (c == sep) {
            fields.push_back(std::move(field));
            field.clear();
            any = true;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            fields.push_back(std::move(field));
            field.clear();
            lines.push_back(std::move(fields));
            fields.clear();
            any = false;
            ++line_no;
        } else {
            field += c;
            any = true;
        }
    }
    if (in_quotes) throw ExportError("CSV: a quote is not closed (line " + std::to_string(line_no) + ")");
    if (any || !field.empty()) {
        fields.push_back(std::move(field));
        lines.push_back(std::move(fields));
    }
    CsvTable t;
    if (lines.empty()) return t;
    t.header = std::move(lines.front());
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].size() != t.header.size())
            throw ExportError("CSV: line " + std::to_string(i + 1) + " has " + std::to_string(lines[i].size()) +
                              " fields, the header " + std::to_string(t.header.size()));
        t.rows.push_back(std::move(lines[i]));
    }
    return t;
}

}  // namespace yantra::exports
