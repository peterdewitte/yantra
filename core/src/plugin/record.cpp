// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Copied from yantra-v2 68ea20a: core/src/plugin/record.cpp, adapted for
// plugin API 1.0 (record flags, KEY fields of NO_SYNC records).

#include "yantra/core/plugin/record.h"

#include <cmath>
#include <cstring>
#include <sstream>

namespace yantra::plugin {

namespace {

// Strict UTF-8 check (no overlong forms, no surrogates, max U+10FFFF).
bool is_utf8(const unsigned char* s, std::size_t n) noexcept {
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = s[i];
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + len > n) {
            return false;
        }
        for (std::size_t k = 1; k < len; ++k) {
            if ((s[i + k] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (s[i + k] & 0x3Fu);
        }
        const std::uint32_t min_cp = len == 2 ? 0x80u : len == 3 ? 0x800u : 0x10000u;
        if (cp < min_cp || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            return false;
        }
        i += len;
    }
    return true;
}

std::string text(const char* s) {
    return s == nullptr ? std::string{} : std::string{s};
}

}  // namespace

std::string value_text(const Value& value, const Field& field) {
    struct Visitor {
        const Field& field;
        std::string operator()(std::monostate) const { return {}; }
        std::string operator()(bool b) const { return b ? "yes" : "no"; }
        std::string operator()(std::int64_t v) const { return std::to_string(v); }
        std::string operator()(std::uint64_t v) const { return std::to_string(v); }
        std::string operator()(double v) const {
            std::ostringstream out;
            out.precision(10);
            out << v;
            return out.str();
        }
        std::string operator()(const std::string& s) const { return s; }
        std::string operator()(EnumValue e) const {
            const EnumItem* item = field.find_enum(e.value);
            return item != nullptr ? item->label : std::to_string(e.value);
        }
    };
    return std::visit(Visitor{field}, value);
}

RecordCheck parse_record(const yantra_record* raw, const Schema& schema) {
    RecordCheck check;
    auto& errors = check.parsed.errors;
    const auto rec = read_versioned(raw);
    const std::string where = "record of schema '" + schema.id + "'";
    if (!rec) {
        errors.push_back(where + ": missing or struct_size too small");
        return check;
    }
    if (rec->value_count > schema.fields.size()) {
        errors.push_back(where + ": " + std::to_string(rec->value_count) + " values for " +
                         std::to_string(schema.fields.size()) + " fields");
        return check;
    }
    if (rec->value_count > 0 && rec->values == nullptr) {
        errors.push_back(where + ": values is NULL");
        return check;
    }
    if ((rec->flags & ~YANTRA_RECORD_ALL_MASK) != 0) {
        errors.push_back(where + ": unknown flag bits");
        return check;
    }
    const bool no_sync = (rec->flags & YANTRA_RECORD_NO_SYNC) != 0;
    Record out;
    out.flags = rec->flags;
    out.schema_id = schema.id;
    out.schema_version = schema.version;
    out.time = rec->time;
    out.values.resize(schema.fields.size());
    for (std::size_t i = 0; i < schema.fields.size(); ++i) {
        const Field& f = schema.fields[i];
        const std::string at = where + " field '" + f.name + "'";
        const yantra_value v = i < rec->value_count ? rec->values[i] : yantra_value{};
        if (v.type == YANTRA_TYPE_NONE) {
            // A NO_SYNC record (level only) has no identity: its KEY
            // fields may be absent; every other non-OPTIONAL field is set.
            if (!f.optional && !(f.key && no_sync)) {
                errors.push_back(at + ": value missing (field is not OPTIONAL)");
            }
            continue;
        }
        if (v.type != static_cast<std::uint32_t>(f.type)) {
            errors.push_back(at + ": value type " + std::to_string(v.type) +
                             " does not match the field");
            continue;
        }
        switch (f.type) {
        case FieldType::Bool:
            if (v.u.i64 != 0 && v.u.i64 != 1) {
                errors.push_back(at + ": BOOL is not 0 or 1");
            } else {
                out.values[i] = v.u.i64 == 1;
            }
            break;
        case FieldType::Int:
            out.values[i] = v.u.i64;
            break;
        case FieldType::UInt:
            out.values[i] = v.u.u64;
            break;
        case FieldType::Double:
            if (!std::isfinite(v.u.f64)) {
                errors.push_back(at + ": DOUBLE is not finite");
            } else {
                out.values[i] = v.u.f64;
            }
            break;
        case FieldType::Enum:
            if (f.find_enum(v.u.i64) == nullptr) {
                errors.push_back(at + ": unknown enum value " + std::to_string(v.u.i64));
            } else {
                out.values[i] = EnumValue{v.u.i64};
            }
            break;
        case FieldType::String: {
            if (v.u.str == nullptr) {
                errors.push_back(at + ": STRING is NULL");
                break;
            }
            // Never scan further than the limit (+1 to detect overlength).
            const std::size_t n = ::strnlen(v.u.str, YANTRA_MAX_STRING_VALUE_BYTES + 1);
            if (n > YANTRA_MAX_STRING_VALUE_BYTES) {
                errors.push_back(at + ": STRING longer than " +
                                 std::to_string(YANTRA_MAX_STRING_VALUE_BYTES) +
                                 " bytes (PLG-08)");
                check.scope_violation = true;
            } else if (!is_utf8(reinterpret_cast<const unsigned char*>(v.u.str), n)) {
                errors.push_back(at + ": STRING is not valid UTF-8");
            } else {
                out.values[i] = std::string{v.u.str, n};
            }
            break;
        }
        }
    }
    if (errors.empty()) {
        check.parsed.value = std::move(out);
    }
    return check;
}

std::string record_key(const Record& record, const Schema& schema) {
    std::string key;
    for (std::size_t i : schema.key_fields()) {
        if (!key.empty()) {
            key += ';';
        }
        key += schema.fields[i].name;
        key += '=';
        if (i < record.values.size()) {
            const Value& v = record.values[i];
            // Enum keys use the stored number, not the (translatable) label.
            if (const auto* e = std::get_if<EnumValue>(&v)) {
                key += std::to_string(e->value);
            } else {
                key += value_text(v, schema.fields[i]);
            }
        }
    }
    return key;
}

Parsed<Provenance> parse_provenance(const yantra_provenance* raw) {
    Parsed<Provenance> result;
    const auto p = read_versioned(raw);
    if (!p) {
        result.errors.push_back("provenance: missing or struct_size too small");
        return result;
    }
    Provenance out{text(p->technology_id), text(p->plugin_id), text(p->plugin_version),
                   text(p->schema_id), p->schema_version};
    if (out.plugin_id.empty() || out.schema_id.empty() || out.schema_version == 0) {
        result.errors.push_back("provenance: plugin id, schema id and version are required");
        return result;
    }
    result.value = std::move(out);
    return result;
}

yantra_provenance to_c(const Provenance& p) noexcept {
    yantra_provenance c{};
    c.struct_size = sizeof c;
    c.schema_version = p.schema_version;
    c.technology_id = p.technology_id.c_str();
    c.plugin_id = p.plugin_id.c_str();
    c.plugin_version = p.plugin_version.c_str();
    c.schema_id = p.schema_id.c_str();
    return c;
}

OpenMode open_mode(const Provenance& data, const Schema* loaded) noexcept {
    if (loaded == nullptr || loaded->id != data.schema_id ||
        loaded->version < data.schema_version) {
        return OpenMode::ReadOnlyGeneric;
    }
    return OpenMode::Full;
}

}  // namespace yantra::plugin
