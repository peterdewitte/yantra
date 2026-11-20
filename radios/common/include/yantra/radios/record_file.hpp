// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * \file yantra/radios/record_file.hpp
 * \brief Record files (RAD-65): a documented, versioned JSON-lines format
 *        for radio records and stream events.
 *
 * A record file holds what a radio plugin delivered through the radio API
 * (yantra/radio.h): one header line with the plugin and its record schemas,
 * then one line per record or stream event, optionally an end line. It is
 * written by the radio test host (RAD-91) and by yantra-gen with
 * ground-truth records (RAD-71); Yantra's tests feed these files into its
 * record sink (Yantra TST-10). It is not a Yantra recording format.
 * The format is described in docs/RECORD_FILES.md.
 *
 * The data model here is plain C++ (strings copied), independent of the
 * lifetime rules of the C ABI. Conversions from and to the C structs are
 * included.
 */
#ifndef YANTRA_RADIOS_RECORD_FILE_HPP
#define YANTRA_RADIOS_RECORD_FILE_HPP

#include <yantra/radio.h>

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::radios {

/** Format name in the header line ("format"). */
inline constexpr std::string_view record_file_format = "yantra-radio-records";
/** Version of the format written by this code; a reader refuses newer ones. */
inline constexpr int record_file_version = 1;
/** Suggested file name extension. */
inline constexpr std::string_view record_file_extension = ".records.jsonl";

/** One item of an ENUM field. */
struct EnumItem {
    int64_t value = 0;
    std::string name;
    std::string label;
    bool operator==(const EnumItem&) const = default;
};

/** A schema field (copy of ::yr_field_def). */
struct FieldDef {
    std::string name;
    std::string label;
    std::string unit;
    std::string help;
    uint32_t type = YR_TYPE_NONE; ///< YR_TYPE_*.
    uint32_t group = YR_GROUP_CORE; ///< YR_GROUP_CORE or one optional group bit.
    uint32_t flags = 0;           ///< YR_FIELD_* bits.
    std::vector<EnumItem> items;  ///< ENUM items.
    bool operator==(const FieldDef&) const = default;
};

/** A record schema (copy of ::yr_schema). */
struct SchemaDef {
    std::string id;
    std::string label;
    uint32_t kind = YR_SCHEMA_MEASUREMENT; ///< YR_SCHEMA_*.
    uint32_t version = 1;
    uint32_t groups = 0; ///< Optional groups (YR_GROUP_* bits).
    std::vector<FieldDef> fields;
    /** Index of the field `name`, -1 when absent. */
    int field_index(std::string_view name) const;
    bool operator==(const SchemaDef&) const = default;
};

/** The plugin that produced the records. */
struct PluginInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string technology_id;
    std::string licence;
    yr_api_version api_version{YR_API_VERSION_MAJOR, YR_API_VERSION_MINOR};
    uint32_t capabilities = 0; ///< YR_CAP_* bits.
    bool operator==(const PluginInfo& o) const
    {
        return id == o.id && name == o.name && version == o.version &&
               technology_id == o.technology_id && licence == o.licence &&
               api_version.major == o.api_version.major &&
               api_version.minor == o.api_version.minor && capabilities == o.capabilities;
    }
};

/** The device the records came from (optional in the header). */
struct DeviceInfo {
    std::string key;
    std::string label;
    std::string driver;
    std::string serial;
    std::string module_version;
    uint32_t flags = 0; ///< YR_DEVICE_* bits.
    bool operator==(const DeviceInfo&) const = default;
};

/** The measurement plan (optional in the header). */
struct PlanInfo {
    uint32_t type = YR_PLAN_SERVING; ///< YR_PLAN_*.
    uint64_t serving_hz = 0;
    std::vector<uint64_t> carriers_hz;
    uint32_t flags = 0; ///< YR_PLAN_* flags.
    uint64_t scan_first_hz = 0;
    uint64_t scan_last_hz = 0;
    uint64_t scan_step_hz = 0;
    bool operator==(const PlanInfo&) const = default;
};

/** The header line. */
struct RecordFileHeader {
    int version = record_file_version;
    std::string producer;      ///< Program and version, e.g. "yantra-radio-host 0.1.0".
    int64_t created_utc_ns = 0; ///< When the file was started, 0 if unknown.
    PluginInfo plugin;
    std::optional<DeviceInfo> device;
    std::optional<PlanInfo> plan;
    std::vector<SchemaDef> schemas;
    /** Index of the schema `id`, -1 when absent. */
    int schema_index(std::string_view id) const;
    bool operator==(const RecordFileHeader&) const = default;
};

/** A typed value with its own string storage. */
struct Value {
    uint32_t type = YR_TYPE_NONE; ///< YR_TYPE_*, NONE when absent.
    int64_t i64 = 0;              ///< BOOL, INT, ENUM.
    uint64_t u64 = 0;             ///< UINT.
    double f64 = 0.0;             ///< DOUBLE.
    std::string str;              ///< STRING.
    bool operator==(const Value&) const = default;
};

/** One record. `values` has one entry per schema field (NONE = absent). */
struct Record {
    uint32_t schema_index = 0; ///< Index into RecordFileHeader::schemas.
    yr_time time{};
    uint32_t flags = 0;   ///< YR_RECORD_* bits.
    uint32_t present = 0; ///< Optional groups carried (YR_GROUP_* bits).
    std::vector<Value> values;
    bool operator==(const Record& o) const
    {
        return schema_index == o.schema_index && time.monotonic_ns == o.time.monotonic_ns &&
               time.utc_ns == o.time.utc_ns && time.utc_source == o.time.utc_source &&
               flags == o.flags && present == o.present && values == o.values;
    }
};

/** One stream event (RAD-25). */
struct Event {
    uint32_t event = 0;       ///< YR_EVENT_*.
    int64_t value = 0;
    int64_t monotonic_ns = 0; ///< Host monotonic time when it was reported.
    bool operator==(const Event&) const = default;
};

/** The optional last line: what the writer saw. */
struct EndSummary {
    uint64_t records = 0; ///< Record lines written.
    uint64_t events = 0;  ///< Event lines written.
    uint64_t dropped = 0; ///< Records the producer dropped (not in the file).
    bool operator==(const EndSummary&) const = default;
};

/** One line after the header. */
struct RecordFileLine {
    enum class Kind { record, event, end };
    Kind kind = Kind::record;
    Record record; ///< Kind::record.
    Event event;   ///< Kind::event.
    EndSummary end; ///< Kind::end.
};

/** \name Names used in the file
 * Machine names of the API constants ("measurement", "uint", "quality",
 * "sync_found", ...); "" or nullopt for unknown values.
 * @{ */
std::string_view schema_kind_name(uint32_t kind);
std::string_view type_name(uint32_t type);
std::string_view group_name(uint32_t group_bit);
std::string_view event_name(uint32_t event);
std::string_view plan_type_name(uint32_t type);
std::string_view time_source_name(uint32_t source);
std::optional<uint32_t> event_from_name(std::string_view name);
/** @} */

/** \name Conversions from the C ABI (strings copied)
 * @{ */
SchemaDef schema_from_c(const yr_schema& schema);
PluginInfo plugin_from_c(const yr_descriptor& descriptor);
/** Header with plugin and schemas of `descriptor`; producer and time empty. */
RecordFileHeader header_from_descriptor(const yr_descriptor& descriptor);
DeviceInfo device_from_c(const yr_device_info& device);
PlanInfo plan_from_c(const yr_plan& plan);
/** Copies a record; values of groups not in `present` and missing trailing
 *  values become NONE. `schema` is the record's schema. */
Record record_from_c(const yr_record& record, const SchemaDef& schema);
/** @} */

/** A stored record as a ::yr_record, e.g. to feed it into a record sink
 *  (Yantra TST-10). Points into `record`, which must outlive the view. */
struct RecordView {
    std::vector<yr_value> values;
    yr_record record{};
    explicit RecordView(const Record& record);
};

/** A stored schema as a ::yr_schema, e.g. for a plugin descriptor
 *  (RAD-12) built from a SchemaDef. Points into `schema`, which must
 *  outlive the view; the view itself must not be copied or moved after
 *  `schema_c` was taken (it points into its own vectors). */
struct SchemaView {
    std::vector<std::vector<yr_enum_item>> items;
    std::vector<yr_field_def> fields;
    std::vector<const yr_field_def*> field_ptrs;
    yr_schema schema_c{};
    explicit SchemaView(const SchemaDef& schema);
    SchemaView(const SchemaView&) = delete;
    SchemaView& operator=(const SchemaView&) = delete;
};

/** Writes a record file line by line. Throws std::invalid_argument when a
 *  record does not fit its schema (wrong type, unknown schema, non-finite
 *  double) and std::logic_error when the header is missing or repeated. */
class RecordFileWriter {
public:
    explicit RecordFileWriter(std::ostream& out);
    /** First line; exactly once. */
    void write_header(const RecordFileHeader& header);
    void write_record(const Record& record);
    void write_event(const Event& event);
    /** Optional last line. */
    void write_end(const EndSummary& end);
    /** The JSON line of `record` without the newline (header must be written). */
    std::string record_line(const Record& record) const;
    /** The JSON line of `event` without the newline. */
    static std::string event_line(const Event& event);
    uint64_t records() const { return records_; }
    uint64_t events() const { return events_; }
    const RecordFileHeader& header() const { return header_; }

private:
    std::ostream& out_;
    RecordFileHeader header_;
    bool have_header_ = false;
    uint64_t records_ = 0;
    uint64_t events_ = 0;
};

/** Reads a record file line by line, checking every line against the
 *  header's schemas. Unknown members and unknown line types are ignored
 *  (compatible additions within version 1); empty lines are skipped. */
class RecordFileReader {
public:
    enum class Result { line, end_of_file, error };
    explicit RecordFileReader(std::istream& in);
    /** Reads and checks the header line. False with `error` on failure (not
     *  a record file, other format, newer version, bad schema). */
    bool read_header(std::string& error);
    const RecordFileHeader& header() const { return header_; }
    /** Next record, event or end line. */
    Result next(RecordFileLine& line, std::string& error);
    /** Number of the line read last (1 = header). */
    uint64_t line_number() const { return line_number_; }

private:
    std::istream& in_;
    RecordFileHeader header_;
    bool have_header_ = false;
    uint64_t line_number_ = 0;
};

/** Reads a whole file. False with `error` ("<path>:<line>: what") on failure. */
bool read_record_file(const std::filesystem::path& path, RecordFileHeader& header,
                      std::vector<RecordFileLine>& lines, std::string& error);

} // namespace yantra::radios

#endif // YANTRA_RADIOS_RECORD_FILE_HPP
