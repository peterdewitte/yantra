// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The common data model on the host side (PLG-05): checked, owning C++
// copies of the schemas a plugin declares. The core stores and shows
// values by these schemas only; it has no technology knowledge (PLG-01).
// Parsing also enforces the declarative part of the scope boundary
// (PLG-08), and check_radio_schema() compares a technology schema with
// the radio API schema of the same id (PLG-05, RAD-21).
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plugin/schema.h,
// adapted for plugin API 1.0.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/core/plugin/abi.h"

struct yr_schema;

namespace yantra::plugin {

enum class FieldType : std::uint32_t {
    Bool = YANTRA_TYPE_BOOL,
    Int = YANTRA_TYPE_INT,
    UInt = YANTRA_TYPE_UINT,
    Double = YANTRA_TYPE_DOUBLE,
    String = YANTRA_TYPE_STRING,
    Enum = YANTRA_TYPE_ENUM,
};

enum class Category : std::uint32_t {
    SystemInfo = YANTRA_CATEGORY_SYSTEM_INFO,
    Level = YANTRA_CATEGORY_LEVEL,
    Quality = YANTRA_CATEGORY_QUALITY,
    Device = YANTRA_CATEGORY_DEVICE,
    PlanPosition = YANTRA_CATEGORY_PLAN_POSITION,
};

enum class SchemaKind : std::uint32_t {
    Measurement = YANTRA_SCHEMA_MEASUREMENT,
    Scan = YANTRA_SCHEMA_SCAN,
    Neighbour = YANTRA_SCHEMA_NEIGHBOUR,
    Recording = YANTRA_SCHEMA_RECORDING,
};

struct EnumItem {
    std::int64_t value = 0;
    std::string name;
    std::string label;
    bool operator==(const EnumItem&) const = default;
};

struct Field {
    std::string name;
    std::string label;
    std::string unit;
    std::string help;
    FieldType type = FieldType::Int;
    Category category = Category::SystemInfo;
    bool key = false;  // part of the unique key
    bool shown_by_default = false;
    bool optional = false;
    bool verify = false;  // compared with the cell database (DB-04)
    std::vector<EnumItem> enum_items;

    const EnumItem* find_enum(std::int64_t value) const noexcept;
    bool operator==(const Field&) const = default;
};

struct Schema {
    std::string id;
    std::string label;
    SchemaKind kind = SchemaKind::Measurement;
    std::uint32_t version = 1;
    std::vector<Field> fields;

    // Index of a field by name, or nullopt.
    std::optional<std::size_t> field_index(std::string_view name) const noexcept;
    // Indices of the KEY fields, in field order.
    std::vector<std::size_t> key_fields() const;
    bool operator==(const Schema&) const = default;
};

// True for a machine name `[a-z][a-z0-9_]*` of at most 64 characters.
bool is_identifier(std::string_view name) noexcept;

// True for an enum item name `[a-z0-9_]+` of at most 64 characters.
bool is_item_name(std::string_view name) noexcept;

// True when a field or schema name names data outside the scope boundary
// (PLG-08): subscriber identities or addresses, message content, speech,
// key material, or raw signal data (IQ, samples, bursts, PDUs). Checked
// per '_'-separated word, so "rssi" or "encrypted_blocks" pass and "issi"
// or "sds_text" do not; "subscriber" passes only as "subscriber_class"
// (a cell's broadcast access classes, DB-01). The word list is the radio API's (RAD-17) plus a
// few more identity words.
bool names_out_of_scope_data(std::string_view name) noexcept;

// Checks and copies a plugin's schema. `declared_categories` is the
// plugin's yantra_plugin_descriptor::data_categories: every field's
// category must be one of them.
Parsed<Schema> parse_schema(const yantra_schema* schema, std::uint32_t declared_categories);

// Checks and copies a schema list (unique ids); errors name each schema.
Parsed<std::vector<Schema>> parse_schemas(const yantra_schema* const* schemas,
                                          std::uint32_t count,
                                          std::uint32_t declared_categories);

// Is `next` a compatible new version of `previous` (same id, version not
// lower, existing fields kept with their name, type, unit, category and
// key flag, new fields only appended and not KEY, a version step for any
// change)? Returns the problems; empty when compatible.
std::vector<std::string> check_schema_upgrade(const Schema& previous, const Schema& next);

// Does the technology schema `tech` describe the radio schema `radio` of
// the same id (plugin.h "Radio records and technology schemas")? Every
// radio field exists in `tech` with the same name and type and unit (a
// LEVEL field: "dBm" in tech, "dBFS" or "dBm" from the radio), every enum
// item of the radio exists in tech, the kinds match, tech fields the
// radio never delivers are OPTIONAL, and no radio field of the BROADCAST
// or NEIGHBOURS group is a LEVEL field in tech (broadcast thresholds are
// SYSTEM_INFO: never calibrated or antenna-corrected, RF-02/HW-09).
// Returns the problems; empty when it matches.
std::vector<std::string> check_radio_schema(const Schema& tech, const yr_schema& radio);

// A C view (yantra_schema) of a schema, for calls into plugins (exporter
// write_table, feature on_record): it owns a copy of the schema, and the
// view points into it, so it is valid while the SchemaView lives.
class SchemaView {
public:
    explicit SchemaView(Schema schema);
    SchemaView(const SchemaView&) = delete;
    SchemaView& operator=(const SchemaView&) = delete;
    const yantra_schema& c() const noexcept { return c_; }
    const Schema& schema() const noexcept { return schema_; }

private:
    Schema schema_;
    std::vector<std::vector<yantra_enum_item>> items_;
    std::vector<yantra_field_def> defs_;
    std::vector<const yantra_field_def*> ptrs_;
    yantra_schema c_{};
};

}  // namespace yantra::plugin
