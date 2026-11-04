// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reader of yantra-radios record files (RAD-65, format
// "yantra-radio-records" version 1, documented in the submodule's
// docs/RECORD_FILES.md): what a radio plugin delivered, recorded by
// yantra-radio-host or written by yantra-gen. Yantra's tests, the plugin
// test kit (PLG-13) and the golden recorded measurement files of a
// technology plugin's simulated radio (PLG-11) feed these files into the
// measurement input exactly as the radio host does (TST-10). It is not a
// Yantra recording format.
//
// The reader follows the format's versioning rules: it refuses another
// format and a newer version, ignores unknown members, line types and
// flag names, and matches values to fields by name.
#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <yantra/radio.h>

#include "yantra/core/measure/pipeline.h"
#include "yantra/core/plugin/abi.h"
#include "yantra/core/plugin/schema.h"

namespace yantra::measure {

// A field of a radio schema as the file's header declares it.
struct RecordFileField {
    std::string name;
    std::string label;
    std::string unit;
    std::string help;
    std::uint32_t type = YR_TYPE_UINT;   // YR_TYPE_*
    std::uint32_t group = YR_GROUP_CORE; // YR_GROUP_CORE or one YR_GROUP_* bit
    std::uint32_t flags = 0;             // YR_FIELD_*
    std::vector<plugin::EnumItem> items;
};

// A radio record schema of the header (RAD-21).
struct RecordFileSchema {
    std::string id;
    std::string label;
    std::uint32_t kind = YR_SCHEMA_MEASUREMENT;  // YR_SCHEMA_*
    std::uint32_t version = 1;
    std::uint32_t groups = 0;  // YR_GROUP_* bits it may carry
    std::vector<RecordFileField> fields;
};

// The radio API struct of a header schema (for check_radio_schema), with
// its own storage; it does not change after construction.
class RadioSchemaView {
public:
    explicit RadioSchemaView(const RecordFileSchema& schema);
    RadioSchemaView(const RadioSchemaView&) = delete;
    RadioSchemaView& operator=(const RadioSchemaView&) = delete;
    const yr_schema& get() const noexcept { return schema_; }

private:
    RecordFileSchema copy_;
    std::vector<std::vector<yr_enum_item>> items_;
    std::vector<yr_field_def> fields_;
    std::vector<const yr_field_def*> pointers_;
    yr_schema schema_{};
};

// One line after the header, in file order.
using RecordFileLine = std::variant<RadioRecord, RadioEvent>;

struct RecordFile {
    std::string producer;
    std::int64_t created_utc_ns = 0;
    // The radio plugin (RAD-12).
    std::string plugin_id;
    std::string plugin_name;
    std::string plugin_version;
    std::string technology_id;
    std::string licence;
    std::string api_version;        // "MAJOR.MINOR"
    std::uint32_t capabilities = 0; // YR_CAP_*
    std::string device_key;         // "" when the header has no device
    std::vector<RecordFileSchema> schemas;
    std::vector<RecordFileLine> lines;
    std::size_t record_count = 0;
    std::size_t event_count = 0;
    bool complete = false;      // the end line was read
    std::uint64_t dropped = 0;  // records the producer dropped (end line)
};

// Reads a record file; on error the messages name the file and the line.
plugin::Parsed<RecordFile> read_record_file(const std::filesystem::path& path);
plugin::Parsed<RecordFile> parse_record_file(std::istream& in, const std::string& source);

// Feeds every record and event of the file, in file order, into `input`
// as the radio host would (stream id 1). Returns the number of records.
std::size_t replay(const RecordFile& file, MeasurementInput& input);

}  // namespace yantra::measure
