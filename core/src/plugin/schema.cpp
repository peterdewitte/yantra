// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Copied from yantra-v2 68ea20a: core/src/plugin/schema.cpp, adapted for
// plugin API 1.0 (levels in dBm, VERIFY flag, schema kinds of the radio
// API, required core fields, radio schema check).

#include "yantra/core/plugin/schema.h"

#include <yantra/radio.h>

#include <algorithm>
#include <bit>
#include <iterator>
#include <set>

namespace yantra::plugin {

namespace {

// Words that name data outside the scope boundary (spec 16.5, PLG-08):
// the radio API's list (yantra/radio.hpp is_out_of_scope_name, RAD-17)
// plus identity and content words of the earlier Yantra.
constexpr std::string_view kOutOfScopeWords[] = {
    "ssi",     "issi",    "gssi",     "itsi",      "tsi",        "imsi",       "imei",
    "tmsi",    "msisdn",  "suci",     "supi",      "subscriber", "address",    "addr",
    "sds",     "sms",     "message",  "msg",       "text",       "voice",      "speech",
    "audio",   "codec",   "plaintext", "ciphertext", "decrypted", "keystream", "key",
    "keys",    "ck",      "cck",      "sck",       "gck",        "dck",        "iq",
    "sample",  "samples", "symbols",  "burst",     "bursts",     "pdu",        "pdus"};

bool is_known_type(std::uint32_t t) noexcept {
    return t >= YANTRA_TYPE_BOOL && t <= YANTRA_TYPE_ENUM;
}

bool is_known_kind(std::uint32_t k) noexcept {
    return k >= YANTRA_SCHEMA_MEASUREMENT && k <= YANTRA_SCHEMA_RECORDING;
}

bool is_numeric(FieldType t) noexcept {
    return t == FieldType::Int || t == FieldType::UInt || t == FieldType::Double;
}

void parse_field(const yantra_field_def* raw, std::uint32_t declared, const std::string& where,
                 std::vector<std::string>& errors, std::vector<Field>& out) {
    const auto def = read_versioned(raw);
    if (!def) {
        errors.push_back(where + ": a field is missing or its struct_size is too small");
        return;
    }
    Field f;
    f.name = text_of(def->name);
    f.label = text_of(def->label);
    f.unit = text_of(def->unit);
    f.help = text_of(def->help);
    const std::string at = where + " field '" + f.name + "'";
    const std::size_t errors_before = errors.size();
    if (!is_identifier(f.name)) {
        errors.push_back(at + ": name must match [a-z][a-z0-9_]* (at most 64)");
    } else if (names_out_of_scope_data(f.name)) {
        errors.push_back(at + ": names data outside the scope boundary (PLG-08)");
    }
    if (f.label.empty() || f.help.empty()) {
        errors.push_back(at + ": needs a label and a help text (DOC-07)");
    }
    if (def->reserved != 0) {
        errors.push_back(at + ": reserved is not zero");
    }
    if (def->unit == nullptr) {
        errors.push_back(at + ": unit is NULL (use \"\" for none)");
    }
    if (!is_known_type(def->type)) {
        errors.push_back(at + ": unknown type " + std::to_string(def->type));
    } else {
        f.type = static_cast<FieldType>(def->type);
    }
    const std::uint32_t cat = def->category;
    if (std::popcount(cat) != 1 || (cat & ~YANTRA_CATEGORY_ALL_MASK) != 0) {
        errors.push_back(at + ": category must be exactly one known YANTRA_CATEGORY_* bit");
    } else if ((cat & declared) == 0) {
        errors.push_back(at + ": category not declared by the plugin (PLG-08)");
    } else {
        f.category = static_cast<Category>(cat);
    }
    if ((def->flags & ~YANTRA_FIELD_ALL_MASK) != 0) {
        errors.push_back(at + ": unknown flag bits");
    }
    f.key = (def->flags & YANTRA_FIELD_KEY) != 0;
    f.shown_by_default = (def->flags & YANTRA_FIELD_DEFAULT) != 0;
    f.optional = (def->flags & YANTRA_FIELD_OPTIONAL) != 0;
    f.verify = (def->flags & YANTRA_FIELD_VERIFY) != 0;
    if (errors.size() != errors_before) {
        return;  // type/category unknown: the checks below would mislead
    }

    if (f.type == FieldType::Enum) {
        if (def->enum_count == 0 || def->enum_items == nullptr) {
            errors.push_back(at + ": ENUM field without items");
        } else {
            std::set<std::int64_t> values;
            std::set<std::string> names;
            for (std::uint32_t i = 0; i < def->enum_count; ++i) {
                const yantra_enum_item& item = def->enum_items[i];
                EnumItem e{item.value, text_of(item.name), text_of(item.label)};
                if (!is_item_name(e.name) || e.label.empty()) {
                    errors.push_back(at + ": enum item " + std::to_string(i) +
                                     " needs a name [a-z0-9_]+ and a label");
                }
                if (!values.insert(e.value).second || !names.insert(e.name).second) {
                    errors.push_back(at + ": enum item " + std::to_string(i) +
                                     " repeats a value or name");
                }
                f.enum_items.push_back(std::move(e));
            }
        }
    } else if (def->enum_count != 0 || def->enum_items != nullptr) {
        errors.push_back(at + ": enum items on a field that is not ENUM");
    }
    if (f.category == Category::Level) {
        if (f.unit != "dBm" && f.unit != "dB") {
            errors.push_back(at + ": a LEVEL field is in dBm (a level difference in dB); "
                                  "Yantra calibrates radio levels (PLG-05)");
        }
        if (!is_numeric(f.type)) {
            errors.push_back(at + ": a LEVEL field is numeric");
        }
    }
    if (f.category == Category::Quality) {
        if (f.unit.empty()) {
            errors.push_back(at + ": a QUALITY field needs a unit (\"1\" if dimensionless)");
        }
        if (!is_numeric(f.type)) {
            errors.push_back(at + ": a QUALITY field is numeric");
        }
    }
    if (f.key && (f.optional || f.type == FieldType::Double)) {
        errors.push_back(at + ": a KEY field is neither OPTIONAL nor DOUBLE");
    }
    out.push_back(std::move(f));
}

struct RequiredField {
    std::string_view name;
    FieldType type;
    std::string_view unit;  // empty: any
};

// Fields every radio record of a kind carries (RAD-21, RAD-22), under the
// radio API's names; the technology schema keeps them.
constexpr RequiredField kMeasurementFields[] = {
    {"frequency_hz", FieldType::UInt, "Hz"},
    {"role", FieldType::Enum, ""},
    {"level", FieldType::Double, "dBm"},
};
constexpr RequiredField kNeighbourFields[] = {
    {"frequency_hz", FieldType::UInt, "Hz"},
    {"entry_index", FieldType::UInt, ""},
    {"entry_count", FieldType::UInt, ""},
};

template <std::size_t N>
void require_fields(const Schema& s, const RequiredField (&list)[N], const std::string& where,
                    std::vector<std::string>& errors) {
    for (const RequiredField& r : list) {
        const auto i = s.field_index(r.name);
        if (!i) {
            errors.push_back(where + ": needs the field '" + std::string(r.name) + "'");
            continue;
        }
        const Field& f = s.fields[*i];
        if (f.type != r.type || (!r.unit.empty() && f.unit != r.unit) || f.optional) {
            errors.push_back(where + ": field '" + std::string(r.name) +
                             "' must keep the radio API's type and unit and not be OPTIONAL");
        }
    }
}

}  // namespace

const EnumItem* Field::find_enum(std::int64_t value) const noexcept {
    for (const EnumItem& e : enum_items) {
        if (e.value == value) {
            return &e;
        }
    }
    return nullptr;
}

std::optional<std::size_t> Schema::field_index(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<std::size_t> Schema::key_fields() const {
    std::vector<std::size_t> keys;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].key) {
            keys.push_back(i);
        }
    }
    return keys;
}

bool is_identifier(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64 || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool is_item_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool names_out_of_scope_data(std::string_view name) noexcept {
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t end = name.find('_', start);
        if (end == std::string_view::npos) {
            end = name.size();
        }
        const std::string_view word = name.substr(start, end - start);
        // "subscriber_class" is the cell's broadcast access-class bitmap
        // (D-MLE-SYSINFO; DB-01, DB-07), not a subscriber identity.
        if (word == "subscriber" && name.substr(end).starts_with("_class") &&
            (end + 6 == name.size() || name[end + 6] == '_')) {
            start = end + 7;
            continue;
        }
        if (std::find(std::begin(kOutOfScopeWords), std::end(kOutOfScopeWords), word) !=
            std::end(kOutOfScopeWords)) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

Parsed<Schema> parse_schema(const yantra_schema* raw, std::uint32_t declared_categories) {
    Parsed<Schema> result;
    const auto def = read_versioned(raw);
    if (!def) {
        result.errors.push_back("schema: missing or struct_size too small");
        return result;
    }
    Schema s;
    s.id = text_of(def->id);
    s.label = text_of(def->label);
    s.version = def->version;
    const std::string where = "schema '" + s.id + "'";
    auto& errors = result.errors;
    if (!is_identifier(s.id)) {
        errors.push_back(where + ": id must match [a-z][a-z0-9_]* (at most 64)");
    } else if (names_out_of_scope_data(s.id)) {
        errors.push_back(where + ": names data outside the scope boundary (PLG-08)");
    }
    if (s.label.empty()) {
        errors.push_back(where + ": label is empty");
    }
    if (!is_known_kind(def->kind)) {
        errors.push_back(where + ": unknown kind " + std::to_string(def->kind));
    } else {
        s.kind = static_cast<SchemaKind>(def->kind);
    }
    if (def->version == 0) {
        errors.push_back(where + ": version starts at 1");
    }
    if (def->field_count == 0 || def->fields == nullptr) {
        errors.push_back(where + ": has no fields");
    } else {
        std::set<std::string> names;
        for (std::uint32_t i = 0; i < def->field_count; ++i) {
            parse_field(def->fields[i], declared_categories, where, errors, s.fields);
        }
        for (const Field& f : s.fields) {
            if (!names.insert(f.name).second) {
                errors.push_back(where + ": field name '" + f.name + "' appears twice");
            }
        }
    }
    if (errors.empty()) {
        if ((s.kind == SchemaKind::Measurement || s.kind == SchemaKind::Neighbour) &&
            s.key_fields().empty()) {
            errors.push_back(where + ": a MEASUREMENT or NEIGHBOUR schema needs KEY fields");
        }
        if (s.kind == SchemaKind::Measurement || s.kind == SchemaKind::Scan) {
            require_fields(s, kMeasurementFields, where, errors);
        } else if (s.kind == SchemaKind::Neighbour) {
            require_fields(s, kNeighbourFields, where, errors);
        }
    }
    if (errors.empty()) {
        result.value = std::move(s);
    }
    return result;
}

Parsed<std::vector<Schema>> parse_schemas(const yantra_schema* const* schemas,
                                          std::uint32_t count,
                                          std::uint32_t declared_categories) {
    Parsed<std::vector<Schema>> result;
    if (count > 0 && schemas == nullptr) {
        result.errors.push_back("schema list is NULL");
        return result;
    }
    std::vector<Schema> list;
    std::set<std::string> ids;
    for (std::uint32_t i = 0; i < count; ++i) {
        Parsed<Schema> one = parse_schema(schemas[i], declared_categories);
        result.errors.insert(result.errors.end(), one.errors.begin(), one.errors.end());
        if (one.value) {
            if (!ids.insert(one.value->id).second) {
                result.errors.push_back("schema '" + one.value->id + "' declared twice");
            }
            list.push_back(std::move(*one.value));
        }
    }
    if (result.errors.empty()) {
        result.value = std::move(list);
    }
    return result;
}

std::vector<std::string> check_schema_upgrade(const Schema& previous, const Schema& next) {
    std::vector<std::string> problems;
    const std::string where = "schema '" + next.id + "'";
    if (previous.id != next.id) {
        problems.push_back(where + ": id changed from '" + previous.id + "'");
        return problems;
    }
    if (next.version < previous.version) {
        problems.push_back(where + ": version went down");
    }
    if (next.fields.size() < previous.fields.size()) {
        problems.push_back(where + ": fields were removed");
    }
    const std::size_t common = std::min(previous.fields.size(), next.fields.size());
    for (std::size_t i = 0; i < common; ++i) {
        const Field& a = previous.fields[i];
        const Field& b = next.fields[i];
        if (a.name != b.name || a.type != b.type || a.unit != b.unit ||
            a.category != b.category || a.key != b.key) {
            problems.push_back(where + ": field " + std::to_string(i) + " ('" + a.name +
                               "') changed name, type, unit, category or key flag");
        }
    }
    for (std::size_t i = common; i < next.fields.size(); ++i) {
        if (next.fields[i].key) {
            problems.push_back(where + ": appended field '" + next.fields[i].name +
                               "' cannot be a KEY field");
        }
    }
    if (problems.empty() && next.version == previous.version && next != previous) {
        problems.push_back(where + ": fields changed without a version step");
    }
    return problems;
}

std::vector<std::string> check_radio_schema(const Schema& tech, const yr_schema& radio) {
    std::vector<std::string> problems;
    const std::string where = "schema '" + tech.id + "'";
    const std::string radio_id = text_of(radio.id);
    if (radio_id != tech.id) {
        problems.push_back(where + ": the radio schema is '" + radio_id + "'");
        return problems;
    }
    if (radio.kind != static_cast<std::uint32_t>(tech.kind)) {
        problems.push_back(where + ": the radio schema has kind " + std::to_string(radio.kind) +
                           ", the technology schema kind " +
                           std::to_string(static_cast<std::uint32_t>(tech.kind)));
    }
    std::vector<bool> delivered(tech.fields.size(), false);
    for (std::uint32_t i = 0; i < radio.field_count && radio.fields != nullptr; ++i) {
        const yr_field_def* rf = radio.fields[i];
        if (rf == nullptr) {
            problems.push_back(where + ": radio field " + std::to_string(i) + " is NULL");
            continue;
        }
        const std::string name = text_of(rf->name);
        const auto ti = tech.field_index(name);
        if (!ti) {
            problems.push_back(where + ": the radio delivers '" + name +
                               "', which the technology schema lacks");
            continue;
        }
        delivered[*ti] = true;
        const Field& tf = tech.fields[*ti];
        const std::string at = where + " field '" + name + "'";
        if (rf->type != static_cast<std::uint32_t>(tf.type)) {
            problems.push_back(at + ": radio type " + std::to_string(rf->type) +
                               " differs from the technology type");
        }
        const std::string unit = text_of(rf->unit);
        const bool unit_ok = tf.category == Category::Level && tf.unit == "dBm"
                                 ? (unit == "dBFS" || unit == "dBm")
                                 : unit == tf.unit;
        if (!unit_ok) {
            problems.push_back(at + ": radio unit '" + unit + "' differs from '" + tf.unit + "'");
        }
        // Broadcast values in dBm (access thresholds, MS power limits) are
        // not level readings: no calibration or antenna correction (RF-02,
        // HW-09) may touch them, so they are SYSTEM_INFO.
        if (tf.category == Category::Level &&
            (rf->group == YR_GROUP_BROADCAST || rf->group == YR_GROUP_NEIGHBOURS)) {
            problems.push_back(at + ": a broadcast value of the radio is SYSTEM_INFO, not LEVEL "
                                    "(levels are calibrated and corrected, thresholds are not)");
        }
        for (std::uint32_t k = 0; k < rf->enum_count && rf->enum_items != nullptr; ++k) {
            if (tf.find_enum(rf->enum_items[k].value) == nullptr) {
                problems.push_back(at + ": radio enum value " +
                                   std::to_string(rf->enum_items[k].value) +
                                   " is not a technology enum item");
            }
        }
    }
    for (std::size_t i = 0; i < tech.fields.size(); ++i) {
        if (!delivered[i] && !tech.fields[i].optional) {
            problems.push_back(where + " field '" + tech.fields[i].name +
                               "': not delivered by the radio, so it must be OPTIONAL");
        }
    }
    return problems;
}

SchemaView::SchemaView(Schema schema) : schema_(std::move(schema)) {
    items_.reserve(schema_.fields.size());
    defs_.reserve(schema_.fields.size());
    for (const Field& f : schema_.fields) {
        std::vector<yantra_enum_item> items;
        for (const EnumItem& e : f.enum_items) items.push_back({e.value, e.name.c_str(), e.label.c_str()});
        items_.push_back(std::move(items));
        yantra_field_def d{};
        d.struct_size = sizeof(yantra_field_def);
        d.type = static_cast<std::uint32_t>(f.type);
        d.category = static_cast<std::uint32_t>(f.category);
        d.flags = (f.key ? YANTRA_FIELD_KEY : 0u) | (f.shown_by_default ? YANTRA_FIELD_DEFAULT : 0u) |
                  (f.optional ? YANTRA_FIELD_OPTIONAL : 0u) | (f.verify ? YANTRA_FIELD_VERIFY : 0u);
        d.name = f.name.c_str();
        d.label = f.label.c_str();
        d.unit = f.unit.c_str();
        d.help = f.help.empty() ? nullptr : f.help.c_str();
        d.enum_items = items_.back().empty() ? nullptr : items_.back().data();
        d.enum_count = static_cast<std::uint32_t>(items_.back().size());
        defs_.push_back(d);
    }
    for (const auto& d : defs_) ptrs_.push_back(&d);
    c_.struct_size = sizeof(yantra_schema);
    c_.kind = static_cast<std::uint32_t>(schema_.kind);
    c_.id = schema_.id.c_str();
    c_.label = schema_.label.c_str();
    c_.version = schema_.version;
    c_.field_count = static_cast<std::uint32_t>(defs_.size());
    c_.fields = ptrs_.data();
}

}  // namespace yantra::plugin
