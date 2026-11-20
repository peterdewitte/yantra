// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * \file yantra/radio.hpp
 * \brief Header-only C++20 helpers for the radio API (yantra/radio.h):
 *        version refusal (RAD-11), validation of descriptors, schemas,
 *        records, spectra and settings (RAD-12, RAD-17, RAD-20..23,
 *        RAD-40, RAD-140), and
 *        text copying by the buffer rules of RAD-10.
 *
 * Used by the radio test host and conformance kit (RAD-91), by C++
 * plugins and by Yantra. Nothing here crosses the ABI: the helpers work on
 * the C structs and return C++ values to C++ callers only. They may throw
 * std::bad_alloc; a plugin catches it like every other exception (RAD-10).
 */
#ifndef YANTRA_RADIO_HPP
#define YANTRA_RADIO_HPP

#include <yantra/radio.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::radio {

/** "1.0" style text of a version. */
inline std::string version_text(yr_api_version v)
{
    return std::to_string(v.major) + "." + std::to_string(v.minor);
}

/** Size of the frozen descriptor prefix (struct_size, api_version,
 *  capabilities) that every major version keeps (RAD-11). */
inline constexpr uint32_t descriptor_prefix_size = 16;

/**
 * Why a host built for `host` must refuse the plugin descriptor `d`, or ""
 * when it may load it (RAD-11, Yantra PLG-04). Reads only the frozen
 * prefix before deciding, so it is safe for any major version.
 */
inline std::string refusal_reason(yr_api_version host, const yr_descriptor* d)
{
    if (d == nullptr)
        return "the plugin returned no descriptor";
    if (d->struct_size < descriptor_prefix_size)
        return "the plugin descriptor is malformed (struct_size " + std::to_string(d->struct_size) +
               ")";
    const yr_api_version p = d->api_version;
    if (p.major != host.major)
        return "the plugin was built for radio API " + version_text(p) +
               ", this program supports radio API " + std::to_string(host.major) +
               ".x; use a plugin built for radio API " + std::to_string(host.major) + ".x";
    if (p.minor > host.minor)
        return "the plugin needs radio API " + version_text(p) +
               ", newer than this program's " + version_text(host) +
               "; update this program or use an older plugin";
    if (!YR_HAS_FIELD(d, yr_descriptor, on_context_event))
        return "the plugin descriptor is shorter than radio API 1.0 allows (struct_size " +
               std::to_string(d->struct_size) + ")";
    return {};
}

/** True for a non-NULL, non-empty C string. */
inline bool has_text(const char* s)
{
    return s != nullptr && s[0] != '\0';
}

namespace detail {
/** [a-z0-9]. */
inline bool is_lower_or_digit(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
/** True when `ok` holds for every character of `s`. */
inline bool all_of(std::string_view s, bool (*ok)(char))
{
    for (char c : s)
        if (!ok(c))
            return false;
    return true;
}
} // namespace detail

/** Plugin id `[a-z0-9][a-z0-9._-]*` (RAD-12). */
inline bool valid_plugin_id(std::string_view s)
{
    return !s.empty() && detail::is_lower_or_digit(s[0]) &&
           detail::all_of(s, [](char c) {
               return detail::is_lower_or_digit(c) || c == '.' || c == '_' || c == '-';
           });
}

/** Schema id, field name and technology id `[a-z][a-z0-9_]*`. */
inline bool valid_name(std::string_view s)
{
    return !s.empty() && s[0] >= 'a' && s[0] <= 'z' &&
           detail::all_of(s, [](char c) { return detail::is_lower_or_digit(c) || c == '_'; });
}

/** Setting, sensor, preset and self-test key `[a-z0-9_.]+` (RAD-40). */
inline bool valid_key(std::string_view s)
{
    return !s.empty() && detail::all_of(s, [](char c) {
        return detail::is_lower_or_digit(c) || c == '_' || c == '.';
    });
}

/**
 * True when a field name names data outside the scope boundary (RAD-17,
 * RAD-26): subscriber identities or addresses, message content, speech,
 * key material, or raw signal data (IQ, symbols, bursts, PDUs). Checked
 * per `_`-separated word, so "security_class" or "encrypted_blocks" pass.
 */
inline bool is_out_of_scope_name(std::string_view name)
{
    static constexpr std::array<std::string_view, 32> forbidden = {
        "ssi",   "issi",   "gssi",    "itsi",    "tsi",   "imsi",  "msisdn", "address",
        "addr",  "sds",    "message", "msg",     "text",  "voice", "speech", "audio",
        "codec", "key",    "keys",    "ck",      "cck",   "sck",   "gck",    "dck",
        "iq",    "sample", "samples", "symbols", "burst", "bursts", "pdu",   "pdus"};
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('_', start);
        if (end == std::string_view::npos)
            end = name.size();
        const std::string_view word = name.substr(start, end - start);
        for (std::string_view f : forbidden)
            if (word == f)
                return true;
        start = end + 1;
    }
    return false;
}

/**
 * True when every licence in the SPDX expression `expr` is one this
 * project accepts as GPL-3.0-compatible (RAD-05, Yantra PLG-10). Words
 * AND, OR, WITH and parentheses are skipped.
 */
inline bool licence_is_gpl3_compatible(std::string_view expr)
{
    static constexpr std::array<std::string_view, 12> allowed = {
        "GPL-3.0-or-later", "GPL-3.0-only",     "AGPL-3.0-only", "AGPL-3.0-or-later",
        "GPL-2.0-or-later", "LGPL-2.1-or-later", "LGPL-3.0-or-later", "MIT",
        "BSD-2-Clause",     "BSD-3-Clause",     "BSL-1.0",       "Apache-2.0"};
    bool any = false;
    size_t i = 0;
    while (i < expr.size()) {
        while (i < expr.size() && (expr[i] == ' ' || expr[i] == '(' || expr[i] == ')'))
            ++i;
        size_t j = i;
        while (j < expr.size() && expr[j] != ' ' && expr[j] != '(' && expr[j] != ')')
            ++j;
        const std::string_view word = expr.substr(i, j - i);
        i = j;
        if (word.empty() || word == "AND" || word == "OR")
            continue;
        if (word == "WITH")
            return false; // no exceptions are expected in this project
        bool ok = false;
        for (std::string_view a : allowed)
            ok = ok || word == a;
        if (!ok)
            return false;
        any = true;
    }
    return any;
}

/**
 * Copies `text` into a caller buffer by the RAD-10 rules: always
 * NUL-terminated; #YR_ERR_BUFFER_TOO_SMALL (with as much as fits) when it
 * does not fit. `out_length` (may be NULL) gets the full length.
 */
inline yr_status copy_text(std::string_view text, char* buffer, uint32_t buffer_size,
                           uint32_t* out_length = nullptr)
{
    if (out_length != nullptr)
        *out_length = static_cast<uint32_t>(text.size());
    if (buffer == nullptr || buffer_size == 0)
        return YR_ERR_BUFFER_TOO_SMALL;
    const size_t n = text.size() < buffer_size ? text.size() : buffer_size - 1;
    if (n > 0)
        std::memcpy(buffer, text.data(), n);
    buffer[n] = '\0';
    return n == text.size() ? YR_OK : YR_ERR_BUFFER_TOO_SMALL;
}

/** Index of field `name` in `schema`, or -1. */
inline int field_index(const yr_schema& schema, std::string_view name)
{
    for (uint32_t i = 0; i < schema.field_count; ++i) {
        const yr_field_def* f = schema.fields[i];
        if (f != nullptr && f->name != nullptr && name == f->name)
            return static_cast<int>(i);
    }
    return -1;
}

/** Short text of a number for messages ("2e+06", "0.5"). */
inline std::string number_text(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

/** Number of set bits. */
inline unsigned bit_count(uint32_t v)
{
    unsigned n = 0;
    for (; v != 0; v &= v - 1)
        ++n;
    return n;
}

/** One expected field of a fixed layout or a standard name. */
struct field_rule {
    std::string_view name; /**< Field name. */
    uint32_t type;         /**< YR_TYPE_*. */
    uint32_t group;        /**< YR_GROUP_*. */
};

/** The core layout of MEASUREMENT and SCAN schemas (RAD-21), by YR_CORE_*. */
inline constexpr std::array<field_rule, YR_CORE_FIELD_COUNT> core_layout = {{
    {YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE},
    {YR_FIELD_CHANNEL, YR_TYPE_UINT, YR_GROUP_CORE},
    {YR_FIELD_ROLE, YR_TYPE_ENUM, YR_GROUP_CORE},
    {YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE},
    {YR_FIELD_MCC, YR_TYPE_UINT, YR_GROUP_CORE},
    {YR_FIELD_MNC, YR_TYPE_UINT, YR_GROUP_CORE},
    {YR_FIELD_LA, YR_TYPE_UINT, YR_GROUP_CORE},
    {YR_FIELD_COLOUR_CODE, YR_TYPE_UINT, YR_GROUP_CORE},
}};

/** The leading fields of NEIGHBOUR schemas, by YR_NEIGHBOUR_*. */
inline constexpr std::array<field_rule, YR_NEIGHBOUR_FIELD_COUNT> neighbour_layout = {{
    {YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE},
    {YR_FIELD_ENTRY_INDEX, YR_TYPE_UINT, YR_GROUP_CORE},
    {YR_FIELD_ENTRY_COUNT, YR_TYPE_UINT, YR_GROUP_CORE},
}};

/** Standard optional field names with their type and group (RAD-22). */
inline constexpr std::array<field_rule, 14> standard_optional_fields = {{
    {YR_FIELD_C1_DB, YR_TYPE_DOUBLE, YR_GROUP_C1},
    {YR_FIELD_HYPERFRAME, YR_TYPE_UINT, YR_GROUP_FRAME},
    {YR_FIELD_MULTIFRAME, YR_TYPE_UINT, YR_GROUP_FRAME},
    {YR_FIELD_FRAME, YR_TYPE_UINT, YR_GROUP_FRAME},
    {YR_FIELD_SLOT, YR_TYPE_UINT, YR_GROUP_FRAME},
    {YR_FIELD_MER_PCT, YR_TYPE_DOUBLE, YR_GROUP_QUALITY},
    {YR_FIELD_BER_PCT, YR_TYPE_DOUBLE, YR_GROUP_QUALITY},
    {YR_FIELD_SNR_DB, YR_TYPE_DOUBLE, YR_GROUP_QUALITY},
    {YR_FIELD_EVM_MER_DB, YR_TYPE_DOUBLE, YR_GROUP_QUALITY},
    {YR_FIELD_FREQ_ERROR_HZ, YR_TYPE_DOUBLE, YR_GROUP_QUALITY},
    {YR_FIELD_FREQ_ERROR_PPM, YR_TYPE_DOUBLE, YR_GROUP_QUALITY},
    {YR_FIELD_SIM_FLOOR_ID, YR_TYPE_STRING, YR_GROUP_SIM_POSITION},
    {YR_FIELD_SIM_X_M, YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION},
    {YR_FIELD_SIM_Y_M, YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION},
}};

/**
 * Problems of one schema (empty when valid). `capabilities` are the
 * descriptor's YR_CAP_* bits (level unit, sim_position).
 */
inline std::vector<std::string> validate_schema(const yr_schema& s, uint32_t capabilities)
{
    std::vector<std::string> out;
    const std::string sid = has_text(s.id) ? std::string(s.id) : std::string("?");
    auto bad = [&](const std::string& what) { out.push_back("schema " + sid + ": " + what); };
    if (!YR_HAS_FIELD(&s, yr_schema, reserved)) {
        bad("struct_size too small");
        return out;
    }
    if (s.kind < YR_SCHEMA_MEASUREMENT || s.kind > YR_SCHEMA_NEIGHBOUR)
        bad("unknown kind " + std::to_string(s.kind));
    if (!has_text(s.id) || !valid_name(s.id))
        bad("id must match [a-z][a-z0-9_]*");
    if (!has_text(s.label))
        bad("label is empty");
    if (s.version < 1)
        bad("version must start at 1");
    if ((s.groups & ~YR_GROUP_ALL_MASK) != 0)
        bad("unknown group bits");
    if ((s.groups & YR_GROUP_SIM_POSITION) != 0 && (capabilities & YR_CAP_SIMULATED) == 0)
        bad("sim_position only for SIMULATED plugins");
    if (s.field_count == 0 || s.fields == nullptr) {
        bad("no fields");
        return out;
    }
    std::vector<std::string_view> names;
    for (uint32_t i = 0; i < s.field_count; ++i) {
        const yr_field_def* f = s.fields[i];
        const std::string at = "field " + std::to_string(i);
        if (f == nullptr || !YR_HAS_FIELD(f, yr_field_def, reserved)) {
            bad(at + ": missing or struct_size too small");
            continue;
        }
        const std::string fname = has_text(f->name) ? std::string(f->name) : at;
        auto fbad = [&](const std::string& what) { bad("field " + fname + ": " + what); };
        if (!has_text(f->name) || !valid_name(f->name))
            fbad("name must match [a-z][a-z0-9_]*");
        else {
            for (std::string_view n : names)
                if (n == f->name)
                    fbad("duplicate name");
            names.emplace_back(f->name);
            if (is_out_of_scope_name(f->name))
                fbad("name is outside the scope boundary");
        }
        if (f->type < YR_TYPE_BOOL || f->type > YR_TYPE_ENUM)
            fbad("invalid type " + std::to_string(f->type));
        if (f->group != YR_GROUP_CORE && (bit_count(f->group) != 1 || (f->group & s.groups) == 0))
            fbad("group must be CORE or one optional group the schema declares");
        if ((f->flags & ~YR_FIELD_ALL_MASK) != 0)
            fbad("unknown flag bits");
        if (!has_text(f->label))
            fbad("label is empty");
        if (f->unit == nullptr)
            fbad("unit is NULL (use \"\")");
        if (!has_text(f->help))
            fbad("help text is empty");
        if (f->type == YR_TYPE_ENUM) {
            if (f->enum_count == 0 || f->enum_items == nullptr)
                fbad("ENUM without items");
            else
                for (uint32_t k = 0; k < f->enum_count; ++k)
                    if (!has_text(f->enum_items[k].name) || !valid_key(f->enum_items[k].name) ||
                        !has_text(f->enum_items[k].label))
                        fbad("enum item " + std::to_string(k) + " needs a name and a label");
        }
        // Standard optional names keep their type and group.
        if (has_text(f->name))
            for (const field_rule& r : standard_optional_fields)
                if (r.name == f->name && (r.type != f->type || r.group != f->group))
                    fbad("standard field must have its standard type and group");
    }
    // Fixed leading layouts.
    auto check_layout = [&](const field_rule* rules, size_t count) {
        if (s.field_count < count) {
            bad("fewer fields than the " + std::to_string(count) + " core fields");
            return;
        }
        for (size_t i = 0; i < count; ++i) {
            const yr_field_def* f = s.fields[i];
            if (f == nullptr || !has_text(f->name))
                continue;
            if (rules[i].name != f->name || rules[i].type != f->type ||
                f->group != YR_GROUP_CORE)
                bad("core field " + std::to_string(i) + " must be " + std::string(rules[i].name));
        }
        for (uint32_t i = static_cast<uint32_t>(count); i < s.field_count; ++i)
            if (s.fields[i] != nullptr && s.fields[i]->group == YR_GROUP_CORE)
                bad("field " + std::to_string(i) + " after the core fields has group CORE");
    };
    if (s.kind == YR_SCHEMA_MEASUREMENT || s.kind == YR_SCHEMA_SCAN) {
        check_layout(core_layout.data(), core_layout.size());
        if (s.field_count > YR_CORE_LEVEL && s.fields[YR_CORE_LEVEL] != nullptr) {
            const char* unit = s.fields[YR_CORE_LEVEL]->unit;
            const char* want = (capabilities & YR_CAP_SELF_CALIBRATED) != 0 ? "dBm" : "dBFS";
            if (unit == nullptr || std::strcmp(unit, want) != 0)
                bad(std::string("level unit must be ") + want + "");
        }
        if (s.field_count > YR_CORE_ROLE && s.fields[YR_CORE_ROLE] != nullptr) {
            const yr_field_def* r = s.fields[YR_CORE_ROLE];
            for (uint32_t k = 0; k < r->enum_count && r->enum_items != nullptr; ++k)
                if (r->enum_items[k].value < YR_ROLE_SERVING ||
                    r->enum_items[k].value > YR_ROLE_OTHER)
                    bad("role items must be YR_ROLE_* values");
        }
        if ((s.groups & YR_GROUP_NEIGHBOURS) != 0)
            bad("NEIGHBOURS fields belong in a NEIGHBOUR schema");
    } else if (s.kind == YR_SCHEMA_NEIGHBOUR) {
        check_layout(neighbour_layout.data(), neighbour_layout.size());
        if ((s.groups & ~YR_GROUP_NEIGHBOURS) != 0)
            bad("a NEIGHBOUR schema has only NEIGHBOURS fields after its core");
    }
    return out;
}

/** Problems of a plugin descriptor (empty when valid), checked against the
 *  API this code is compiled for (RAD-11, RAD-12, RAD-13, RAD-48). */
inline std::vector<std::string> validate_descriptor(const yr_descriptor* d)
{
    std::vector<std::string> out;
    const std::string refusal = refusal_reason(yr_api_version_compiled(), d);
    if (!refusal.empty()) {
        out.push_back(refusal);
        return out;
    }
    auto bad = [&](const std::string& what) { out.push_back("descriptor: " + what); };
    // SPECTRUM came with 1.2; an older plugin knows only the 1.1 bits.
    const uint32_t known_caps = d->api_version.minor >= 2 ? YR_CAP_ALL_MASK : YR_CAP_ALL_MASK_1_1;
    if ((d->capabilities & ~known_caps) != 0)
        bad("unknown capability bits");
    if (!has_text(d->id) || !valid_plugin_id(d->id))
        bad("id must match [a-z0-9][a-z0-9._-]*");
    if (!has_text(d->name))
        bad("name is empty");
    if (!has_text(d->version))
        bad("version is empty");
    if (!has_text(d->author))
        bad("author is empty");
    if (!has_text(d->licence) || !licence_is_gpl3_compatible(d->licence))
        bad("licence must be a GPL-3.0-compatible SPDX expression");
    if (!has_text(d->description))
        bad("description is empty");
    if (!has_text(d->source_url))
        bad("source_url is empty");
    if (!has_text(d->technology_id) || !valid_name(d->technology_id))
        bad("technology_id must match [a-z][a-z0-9_]*");
    if (d->init == nullptr || d->shutdown == nullptr)
        bad("init and shutdown are required");
    if (d->stream == nullptr || !YR_HAS_FIELD(d->stream, yr_stream_control, last_error))
        bad("stream control table missing or too small");
    else {
        const yr_stream_control* c = d->stream;
        if (c->enumerate == nullptr || c->open == nullptr || c->close == nullptr ||
            c->device_info == nullptr || c->state == nullptr || c->plan_ranges == nullptr ||
            c->set_plan == nullptr || c->start_stream == nullptr || c->stop_stream == nullptr ||
            c->stream_stats == nullptr || c->last_error == nullptr)
            bad("every stream control function is required");
    }
    if (d->device == nullptr || !YR_HAS_FIELD(d->device, yr_device_control, self_test))
        bad("device control table missing or too small");
    else if (d->device->list_settings == nullptr || d->device->get_setting == nullptr ||
             d->device->set_setting == nullptr)
        bad("list_settings, get_setting and set_setting are required");
    else if ((d->device->list_presets == nullptr) != (d->device->apply_preset == nullptr) ||
             (d->device->list_sensors == nullptr) != (d->device->read_sensor == nullptr))
        bad("list_presets/apply_preset and list_sensors/read_sensor come in pairs");
    if (d->on_context_event != nullptr && (d->capabilities & YR_CAP_SCENE) == 0)
        bad("on_context_event without the SCENE capability");
    if (d->schema_count == 0 || d->schemas == nullptr) {
        bad("no record schemas");
    } else {
        bool measurement = false, scan = false;
        std::vector<std::string_view> ids;
        for (uint32_t i = 0; i < d->schema_count; ++i) {
            const yr_schema* s = d->schemas[i];
            if (s == nullptr) {
                bad("schema " + std::to_string(i) + " is NULL");
                continue;
            }
            for (std::string& p : validate_schema(*s, d->capabilities))
                out.push_back(std::move(p));
            if (YR_HAS_FIELD(s, yr_schema, reserved)) {
                measurement = measurement || s->kind == YR_SCHEMA_MEASUREMENT;
                scan = scan || s->kind == YR_SCHEMA_SCAN;
                if (has_text(s->id)) {
                    for (std::string_view id : ids)
                        if (id == s->id)
                            bad("duplicate schema id " + std::string(s->id));
                    ids.emplace_back(s->id);
                }
            }
        }
        if (!measurement)
            bad("at least one MEASUREMENT schema is required");
        if (scan != ((d->capabilities & YR_CAP_SCAN) != 0))
            bad("a SCAN schema exists exactly when the SCAN capability is set");
    }
    if (d->translation_count > 0 && d->translations == nullptr)
        bad("translation_count without translations");
    for (uint32_t i = 0; i < d->translation_count && d->translations != nullptr; ++i) {
        const yr_translation& t = d->translations[i];
        const bool lang_ok = t.lang != nullptr && (std::strcmp(t.lang, "nl") == 0 ||
                                                   std::strcmp(t.lang, "fr") == 0 ||
                                                   std::strcmp(t.lang, "de") == 0);
        if (!has_text(t.key) || !lang_ok || !has_text(t.text))
            bad("translation " + std::to_string(i) + " needs a key, nl/fr/de and a text");
    }
    return out;
}

/** Problems of one value against its field type (empty when valid). */
inline std::string check_value_type(const yr_value& v, uint32_t type, const yr_enum_item* items,
                                    uint32_t item_count)
{
    if (v.type != type)
        return "type " + std::to_string(v.type) + " where " + std::to_string(type) +
               " is declared";
    switch (type) {
    case YR_TYPE_BOOL:
        if (v.u.i64 != 0 && v.u.i64 != 1)
            return "BOOL must be 0 or 1";
        break;
    case YR_TYPE_DOUBLE:
        if (!std::isfinite(v.u.f64))
            return "DOUBLE must be finite";
        break;
    case YR_TYPE_STRING:
        if (v.u.str == nullptr)
            return "STRING is NULL";
        if (std::strlen(v.u.str) > YR_MAX_STRING_VALUE_BYTES)
            return "STRING longer than YR_MAX_STRING_VALUE_BYTES";
        break;
    case YR_TYPE_ENUM: {
        bool found = false;
        for (uint32_t k = 0; k < item_count && items != nullptr; ++k)
            found = found || items[k].value == v.u.i64;
        if (!found)
            return "ENUM value " + std::to_string(v.u.i64) + " is not an item";
        break;
    }
    default:
        break;
    }
    return {};
}

/**
 * Problems of one record against the descriptor's schema (empty when
 * valid): schema index, value types, the core fields (RAD-21), presence
 * flags (RAD-22), time (RAD-27), SIMULATED and sim_position (RAD-19), the
 * flags the plugin's API version knows and LEVEL_DBM on every record of a
 * SELF_CALIBRATED plugin of API 1.1 or later (RAD-21).
 */
inline std::vector<std::string> validate_record(const yr_descriptor& d, const yr_record& r)
{
    std::vector<std::string> out;
    auto bad = [&](const std::string& what) { out.push_back("record: " + what); };
    if (!YR_HAS_FIELD(&r, yr_record, values)) {
        bad("struct_size too small");
        return out;
    }
    if (r.schema_index >= d.schema_count || d.schemas == nullptr ||
        d.schemas[r.schema_index] == nullptr) {
        bad("schema_index " + std::to_string(r.schema_index) + " is not declared");
        return out;
    }
    const yr_schema& s = *d.schemas[r.schema_index];
    if (r.value_count > s.field_count)
        bad("more values than fields");
    if (r.value_count > 0 && r.values == nullptr) {
        bad("values is NULL");
        return out;
    }
    if (r.reserved != 0)
        bad("reserved is not zero");
    if (r.time.monotonic_ns <= 0)
        bad("time.monotonic_ns must be set");
    if (r.time.utc_source > YR_TIME_SOURCE_DEVICE)
        bad("unknown utc_source");
    else if (r.time.utc_source != YR_TIME_SOURCE_NONE && r.time.utc_ns <= 0)
        bad("utc_ns must be set when utc_source is not NONE");
    // A 1.0 plugin knows only the 1.0 flags; LEVEL_DBM came with 1.1.
    const uint32_t known = d.api_version.minor >= 1 ? YR_RECORD_ALL_MASK : YR_RECORD_ALL_MASK_1_0;
    if ((r.flags & ~known) != 0)
        bad("unknown flag bits");
    if (d.api_version.minor >= 1 && (d.capabilities & YR_CAP_SELF_CALIBRATED) != 0 &&
        (r.flags & YR_RECORD_LEVEL_DBM) == 0)
        bad("LEVEL_DBM missing: every record of a SELF_CALIBRATED plugin is in dBm (API 1.1)");
    if ((r.present & ~s.groups) != 0)
        bad("present has groups the schema does not declare");
    const bool simulated = (d.capabilities & YR_CAP_SIMULATED) != 0;
    if (simulated != ((r.flags & YR_RECORD_SIMULATED) != 0))
        bad("SIMULATED flag must match the plugin's SIMULATED capability");
    if ((r.present & YR_GROUP_SIM_POSITION) != 0 && (r.flags & YR_RECORD_SIMULATED) == 0)
        bad("sim_position only in SIMULATED records");

    const uint32_t n = r.value_count < s.field_count ? r.value_count : s.field_count;
    auto value_at = [&](uint32_t i) -> const yr_value* { return i < n ? &r.values[i] : nullptr; };
    for (uint32_t i = 0; i < n; ++i) {
        const yr_field_def* f = s.fields[i];
        const yr_value& v = r.values[i];
        if (f == nullptr)
            continue;
        if (v.type == YR_TYPE_NONE)
            continue;
        const std::string fname = has_text(f->name) ? std::string(f->name) : std::to_string(i);
        if (f->group != YR_GROUP_CORE && (r.present & f->group) == 0)
            bad("field " + fname + " has a value but its group is not present");
        const std::string e = check_value_type(v, f->type, f->enum_items, f->enum_count);
        if (!e.empty())
            bad("field " + fname + ": " + e);
    }
    auto require = [&](uint32_t index, const char* name) {
        const yr_value* v = value_at(index);
        if (v == nullptr || v->type == YR_TYPE_NONE)
            bad(std::string("core field ") + name + " is missing");
    };
    if (s.kind == YR_SCHEMA_MEASUREMENT || s.kind == YR_SCHEMA_SCAN) {
        require(YR_CORE_FREQUENCY_HZ, YR_FIELD_FREQUENCY_HZ);
        require(YR_CORE_ROLE, YR_FIELD_ROLE);
        require(YR_CORE_LEVEL, YR_FIELD_LEVEL);
        if ((r.flags & YR_RECORD_NO_SYNC) == 0) {
            require(YR_CORE_CHANNEL, YR_FIELD_CHANNEL);
            require(YR_CORE_MCC, YR_FIELD_MCC);
            require(YR_CORE_MNC, YR_FIELD_MNC);
            require(YR_CORE_LA, YR_FIELD_LA);
            require(YR_CORE_COLOUR_CODE, YR_FIELD_COLOUR_CODE);
        }
        const yr_value* role = value_at(YR_CORE_ROLE);
        if (s.kind == YR_SCHEMA_SCAN && role != nullptr && role->type == YR_TYPE_ENUM &&
            role->u.i64 != YR_ROLE_OTHER)
            bad("scan records have role OTHER");
    } else if (s.kind == YR_SCHEMA_NEIGHBOUR) {
        require(YR_NEIGHBOUR_FREQUENCY_HZ, YR_FIELD_FREQUENCY_HZ);
        require(YR_NEIGHBOUR_ENTRY_INDEX, YR_FIELD_ENTRY_INDEX);
        require(YR_NEIGHBOUR_ENTRY_COUNT, YR_FIELD_ENTRY_COUNT);
        const yr_value* idx = value_at(YR_NEIGHBOUR_ENTRY_INDEX);
        const yr_value* cnt = value_at(YR_NEIGHBOUR_ENTRY_COUNT);
        if (idx != nullptr && cnt != nullptr && idx->type == YR_TYPE_UINT &&
            cnt->type == YR_TYPE_UINT && idx->u.u64 >= cnt->u.u64)
            bad("entry_index must be below entry_count");
    }
    return out;
}

/**
 * Problems of one spectrum (since 1.2, RAD-140; empty when valid): the
 * plugin has SPECTRUM, struct size, bins (1..YR_SPECTRUM_MAX_BINS, finite
 * power, width > 0, stop = start + bins * width), time (RAD-27), the flags
 * (within YR_SPECTRUM_FLAGS_MASK; SIMULATED as the capability, LEVEL_DBM
 * on every spectrum of a SELF_CALIBRATED plugin, RAD-21).
 */
inline std::vector<std::string> validate_spectrum(const yr_descriptor& d, const yr_spectrum& sp)
{
    std::vector<std::string> out;
    auto bad = [&](const std::string& what) { out.push_back("spectrum: " + what); };
    if (!YR_HAS_FIELD(&sp, yr_spectrum, power_db)) {
        bad("struct_size too small");
        return out;
    }
    if ((d.capabilities & YR_CAP_SPECTRUM) == 0)
        bad("the plugin has no SPECTRUM capability");
    if (sp.reserved != 0)
        bad("reserved is not zero");
    if ((sp.flags & ~YR_SPECTRUM_FLAGS_MASK) != 0)
        bad("unknown flag bits (OVERLOAD, UNCALIBRATED, SIMULATED, LEVEL_DBM only)");
    const bool simulated = (d.capabilities & YR_CAP_SIMULATED) != 0;
    if (simulated != ((sp.flags & YR_RECORD_SIMULATED) != 0))
        bad("SIMULATED flag must match the plugin's SIMULATED capability");
    if ((d.capabilities & YR_CAP_SELF_CALIBRATED) != 0 && (sp.flags & YR_RECORD_LEVEL_DBM) == 0)
        bad("LEVEL_DBM missing: a SELF_CALIBRATED plugin delivers dBm");
    if (sp.time.monotonic_ns <= 0)
        bad("time.monotonic_ns must be set");
    if (sp.time.utc_source > YR_TIME_SOURCE_DEVICE)
        bad("unknown utc_source");
    else if (sp.time.utc_source != YR_TIME_SOURCE_NONE && sp.time.utc_ns <= 0)
        bad("utc_ns must be set when utc_source is not NONE");
    if (sp.bin_count == 0 || sp.bin_count > YR_SPECTRUM_MAX_BINS) {
        bad("bin_count " + std::to_string(sp.bin_count) + " is not 1.." +
            std::to_string(YR_SPECTRUM_MAX_BINS));
        return out;
    }
    if (!(std::isfinite(sp.bin_width_hz) && sp.bin_width_hz > 0.0))
        bad("bin_width_hz must be above 0");
    if (!(std::isfinite(sp.start_hz) && std::isfinite(sp.stop_hz) && sp.start_hz > 0.0 &&
          sp.start_hz < sp.stop_hz))
        bad("start_hz must be above 0 and below stop_hz");
    else if (std::fabs(sp.start_hz + sp.bin_count * sp.bin_width_hz - sp.stop_hz) >
             1.0 + 1e-9 * sp.stop_hz)
        bad("stop_hz must be start_hz + bin_count * bin_width_hz");
    if (sp.power_db == nullptr) {
        bad("power_db is NULL");
        return out;
    }
    for (uint32_t i = 0; i < sp.bin_count; ++i)
        if (!std::isfinite(sp.power_db[i])) {
            bad("power_db[" + std::to_string(i) + "] is not finite");
            break;
        }
    return out;
}

/** The band a spectrum plan asks for (RAD-140): yr_plan's
 *  spectrum_first_hz..spectrum_last_hz, or the default 380-400 MHz when
 *  both are 0 or the plan is older than 1.2. */
inline void spectrum_band(const yr_plan& plan, double& first_hz, double& last_hz)
{
    first_hz = YR_SPECTRUM_DEFAULT_FIRST_HZ;
    last_hz = YR_SPECTRUM_DEFAULT_LAST_HZ;
    if (YR_HAS_FIELD(&plan, yr_plan, spectrum_last_hz) &&
        (plan.spectrum_first_hz != 0 || plan.spectrum_last_hz != 0)) {
        first_hz = static_cast<double>(plan.spectrum_first_hz);
        last_hz = static_cast<double>(plan.spectrum_last_hz);
    }
}

/** Problems of a setting definition (empty when valid, RAD-40, RAD-42). */
inline std::vector<std::string> validate_setting_def(const yr_setting_def& s, bool sensor = false)
{
    std::vector<std::string> out;
    const std::string key = has_text(s.key) ? std::string(s.key) : std::string("?");
    auto bad = [&](const std::string& what) {
        out.push_back((sensor ? "sensor " : "setting ") + key + ": " + what);
    };
    if (!YR_HAS_FIELD(&s, yr_setting_def, reserved)) {
        bad("struct_size too small");
        return out;
    }
    if (!has_text(s.key) || !valid_key(s.key))
        bad("key must match [a-z0-9_.]+");
    const uint32_t max_type = sensor ? YR_TYPE_ENUM : YR_TYPE_ACTION;
    if (s.type < YR_TYPE_BOOL || s.type > max_type)
        bad("invalid type " + std::to_string(s.type));
    if ((s.flags & ~YR_SETTING_ALL_MASK) != 0)
        bad("unknown flag bits");
    if (sensor && (s.flags & YR_SETTING_READ_ONLY) == 0)
        bad("sensors are READ_ONLY");
    if (!has_text(s.label))
        bad("label is empty");
    if (!has_text(s.help))
        bad("help text is empty");
    if (s.unit == nullptr || s.group == nullptr)
        bad("unit and group must not be NULL (use \"\")");
    if ((s.flags & YR_SETTING_UNAVAILABLE) != 0 && !has_text(s.unavailable_reason))
        bad("UNAVAILABLE needs unavailable_reason");
    if ((s.flags & YR_SETTING_WARNING) != 0 && !has_text(s.warning))
        bad("WARNING needs the warning text");
    if (s.type == YR_TYPE_ENUM) {
        if (s.item_count == 0 || s.items == nullptr)
            bad("ENUM without items");
        else
            for (uint32_t k = 0; k < s.item_count; ++k)
                if (!has_text(s.items[k].name) || !valid_key(s.items[k].name) ||
                    !has_text(s.items[k].label))
                    bad("enum item " + std::to_string(k) + " needs a name and a label");
    }
    const bool numeric = s.type == YR_TYPE_INT || s.type == YR_TYPE_UINT ||
                         s.type == YR_TYPE_DOUBLE;
    if (numeric) {
        if (std::isfinite(s.minimum) && std::isfinite(s.maximum) && s.minimum > s.maximum)
            bad("minimum above maximum");
        if (!(s.step >= 0.0))
            bad("step must be 0 or positive");
    }
    if (s.type == YR_TYPE_ACTION || sensor) {
        if (s.default_value.type != YR_TYPE_NONE)
            bad("actions and sensors have no default value");
    } else {
        const std::string e = check_value_type(s.default_value, s.type, s.items, s.item_count);
        if (!e.empty())
            bad("default value: " + e);
        else if (numeric) {
            const double v = s.type == YR_TYPE_DOUBLE ? s.default_value.u.f64
                             : s.type == YR_TYPE_INT
                                 ? static_cast<double>(s.default_value.u.i64)
                                 : static_cast<double>(s.default_value.u.u64);
            if ((std::isfinite(s.minimum) && v < s.minimum) ||
                (std::isfinite(s.maximum) && v > s.maximum))
                bad("default value out of range");
        }
    }
    return out;
}

/**
 * Checks a requested setting value (RAD-41): #YR_OK, or
 * #YR_ERR_INVALID_ARGUMENT with a one-line reason for last_error in `why`.
 */
inline yr_status check_setting_value(const yr_setting_def& s, const yr_value& v,
                                     std::string* why = nullptr)
{
    auto fail = [&](std::string text) {
        if (why != nullptr)
            *why = std::move(text);
        return YR_ERR_INVALID_ARGUMENT;
    };
    const std::string label = has_text(s.label) ? std::string(s.label) : std::string(s.key);
    if (s.type == YR_TYPE_ACTION)
        return fail(label + " is an action; run it instead of setting it");
    if ((s.flags & YR_SETTING_READ_ONLY) != 0)
        return fail(label + " is read-only");
    if ((s.flags & YR_SETTING_UNAVAILABLE) != 0)
        return fail(label + " is not available: " +
                    (has_text(s.unavailable_reason) ? s.unavailable_reason : ""));
    const std::string e = check_value_type(v, s.type, s.items, s.item_count);
    if (!e.empty())
        return fail(label + ": " + e);
    if (s.type == YR_TYPE_INT || s.type == YR_TYPE_UINT || s.type == YR_TYPE_DOUBLE) {
        const double x = s.type == YR_TYPE_DOUBLE ? v.u.f64
                         : s.type == YR_TYPE_INT  ? static_cast<double>(v.u.i64)
                                                  : static_cast<double>(v.u.u64);
        if ((std::isfinite(s.minimum) && x < s.minimum) ||
            (std::isfinite(s.maximum) && x > s.maximum)) {
            std::string range = label + " must be";
            if (std::isfinite(s.minimum))
                range += " at least " + number_text(s.minimum);
            if (std::isfinite(s.minimum) && std::isfinite(s.maximum))
                range += " and";
            if (std::isfinite(s.maximum))
                range += " at most " + number_text(s.maximum);
            return fail(range + (has_text(s.unit) ? std::string(" ") + s.unit : std::string()));
        }
    }
    if (why != nullptr)
        why->clear();
    return YR_OK;
}

} // namespace yantra::radio

#endif // YANTRA_RADIO_HPP
