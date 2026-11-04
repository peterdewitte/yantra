// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Records of the common data model (PLG-05) on the host side: checked,
// owning copies of what plugins emit, their unique key, and the provenance
// stored with them (PLG-12).
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plugin/record.h,
// adapted for plugin API 1.0.
#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "yantra/core/plugin/schema.h"

namespace yantra::plugin {

struct EnumValue {
    std::int64_t value = 0;
    bool operator==(const EnumValue&) const = default;
};

// A value; std::monostate = absent.
using Value = std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double,
                           std::string, EnumValue>;

// Display text of a value for a field ("" when absent; enum label; number
// without unit).
std::string value_text(const Value& value, const Field& field);

struct Record {
    std::string schema_id;
    std::uint32_t schema_version = 0;
    yantra_time time{};
    std::uint32_t flags = 0;    // YANTRA_RECORD_* bits
    std::vector<Value> values;  // one per schema field (absent ones = monostate)
};

// Checks and copies a record the plugin emitted for `schema`: value types
// match the fields, non-OPTIONAL fields are present (KEY fields may be
// absent in a YANTRA_RECORD_NO_SYNC record), flags are known, doubles are
// finite, enum values exist, strings are valid UTF-8 and at most
// YANTRA_MAX_STRING_VALUE_BYTES (PLG-08). `scope_violation` is set when a
// rejection is a scope matter (the host answers YANTRA_ERR_SCOPE).
struct RecordCheck {
    Parsed<Record> parsed;
    bool scope_violation = false;
};
RecordCheck parse_record(const yantra_record* record, const Schema& schema);

// Canonical text of the record's KEY fields ("name=value;..."), the identity
// used by the cell list and the cell database. Empty for a schema without
// KEY fields.
std::string record_key(const Record& record, const Schema& schema);

// Where stored data came from (PLG-12).
struct Provenance {
    std::string technology_id;
    std::string plugin_id;
    std::string plugin_version;
    std::string schema_id;
    std::uint32_t schema_version = 0;
    bool operator==(const Provenance&) const = default;
};

Parsed<Provenance> parse_provenance(const yantra_provenance* provenance);
// Fills a C provenance pointing into `p` (valid while `p` lives).
yantra_provenance to_c(const Provenance& p) noexcept;

// How stored data opens with the plugins at hand (PLG-12, PLG-22).
enum class OpenMode {
    Full,             // the plugin and a schema of this or a newer version are loaded
    ReadOnlyGeneric,  // plugin absent, disabled or older: generic views, read-only
};

// `loaded` is the schema with the same id of the loaded plugin, or nullptr
// when the plugin is absent or disabled.
OpenMode open_mode(const Provenance& data, const Schema* loaded) noexcept;

}  // namespace yantra::plugin
