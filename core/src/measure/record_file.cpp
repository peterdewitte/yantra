// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Record file reader (RAD-65 version 1, TST-10). Written for Yantra from
// the format reference (yantra-radios docs/RECORD_FILES.md); the name
// tables follow yantra-radios fb194f5 common/src/record_file.cpp.

#include "yantra/core/measure/record_file.h"

#include <cmath>
#include <fstream>
#include <istream>
#include <limits>
#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

namespace yantra::measure {

namespace {

using nlohmann::json;

struct Name {
    std::string_view name;
    std::uint32_t value;
};

constexpr Name kTypes[] = {{"bool", YR_TYPE_BOOL},     {"int", YR_TYPE_INT},
                           {"uint", YR_TYPE_UINT},     {"double", YR_TYPE_DOUBLE},
                           {"string", YR_TYPE_STRING}, {"enum", YR_TYPE_ENUM}};
constexpr Name kKinds[] = {{"measurement", YR_SCHEMA_MEASUREMENT},
                           {"scan", YR_SCHEMA_SCAN},
                           {"neighbour", YR_SCHEMA_NEIGHBOUR}};
constexpr Name kGroups[] = {{"core", YR_GROUP_CORE},
                            {"quality", YR_GROUP_QUALITY},
                            {"c1", YR_GROUP_C1},
                            {"frame", YR_GROUP_FRAME},
                            {"broadcast", YR_GROUP_BROADCAST},
                            {"neighbours", YR_GROUP_NEIGHBOURS},
                            {"sim_position", YR_GROUP_SIM_POSITION},
                            {"other", YR_GROUP_OTHER}};
constexpr Name kFieldFlags[] = {{"key", YR_FIELD_KEY}, {"default", YR_FIELD_DEFAULT}};
constexpr Name kRecordFlags[] = {{"no_sync", YR_RECORD_NO_SYNC},
                                 {"overload", YR_RECORD_OVERLOAD},
                                 {"uncalibrated", YR_RECORD_UNCALIBRATED},
                                 {"simulated", YR_RECORD_SIMULATED}};
constexpr Name kCapabilities[] = {{"scan", YR_CAP_SCAN},
                                  {"hotplug", YR_CAP_HOTPLUG},
                                  {"retune_live", YR_CAP_RETUNE_LIVE},
                                  {"simulated", YR_CAP_SIMULATED},
                                  {"scene", YR_CAP_SCENE},
                                  {"self_calibrated", YR_CAP_SELF_CALIBRATED}};
constexpr Name kEvents[] = {{"sync_found", YR_EVENT_SYNC_FOUND},
                            {"sync_lost", YR_EVENT_SYNC_LOST},
                            {"serving_changed", YR_EVENT_SERVING_CHANGED},
                            {"overflow", YR_EVENT_OVERFLOW},
                            {"dropped", YR_EVENT_DROPPED},
                            {"plan_applied", YR_EVENT_PLAN_APPLIED},
                            {"scan_done", YR_EVENT_SCAN_DONE},
                            {"end_of_stream", YR_EVENT_END_OF_STREAM},
                            {"device_lost", YR_EVENT_DEVICE_LOST},
                            {"device_back", YR_EVENT_DEVICE_BACK},
                            {"error", YR_EVENT_ERROR}};

template <std::size_t N>
std::optional<std::uint32_t> lookup(const Name (&table)[N], std::string_view name) {
    for (const Name& n : table)
        if (n.name == name) return n.value;
    return std::nullopt;
}

// A bit set written as an array of names; unknown names are ignored.
template <std::size_t N>
std::uint32_t bits(const json& j, const char* member, const Name (&table)[N]) {
    std::uint32_t out = 0;
    const auto it = j.find(member);
    if (it == j.end() || !it->is_array()) return 0;
    for (const json& e : *it)
        if (e.is_string())
            if (const auto v = lookup(table, e.get<std::string>())) out |= *v;
    return out;
}

std::string str(const json& j, const char* member) {
    const auto it = j.find(member);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

std::int64_t i64(const json& j, const char* member) {
    const auto it = j.find(member);
    return it != j.end() && it->is_number_integer() ? it->get<std::int64_t>() : 0;
}

struct Error {
    std::string message;
};

[[noreturn]] void fail(const std::string& message) { throw Error{message}; }

RecordFileField parse_field(const json& f, const std::string& where) {
    if (!f.is_object()) fail(where + ": a field is not an object");
    RecordFileField out;
    out.name = str(f, "name");
    if (out.name.empty()) fail(where + ": a field has no name");
    const std::string at = where + " field '" + out.name + "'";
    const auto type = lookup(kTypes, str(f, "type"));
    if (!type) fail(at + ": unknown type '" + str(f, "type") + "'");
    out.type = *type;
    const auto group = lookup(kGroups, str(f, "group").empty() ? "core" : str(f, "group"));
    if (!group) fail(at + ": unknown group '" + str(f, "group") + "'");
    out.group = *group;
    out.unit = str(f, "unit");
    out.label = str(f, "label");
    out.help = str(f, "help");
    out.flags = bits(f, "flags", kFieldFlags);
    if (const auto it = f.find("items"); it != f.end() && it->is_array()) {
        for (const json& item : *it) {
            if (!item.is_object() || !item.contains("value") ||
                !item["value"].is_number_integer())
                fail(at + ": an enum item has no integer value");
            out.items.push_back({item["value"].get<std::int64_t>(), str(item, "name"),
                                 str(item, "label")});
        }
    }
    if (out.type == YR_TYPE_ENUM && out.items.empty()) fail(at + ": an enum field without items");
    return out;
}

RecordFileSchema parse_schema(const json& s, const std::string& where) {
    if (!s.is_object()) fail(where + ": a schema is not an object");
    RecordFileSchema out;
    out.id = str(s, "id");
    if (out.id.empty()) fail(where + ": a schema has no id");
    const std::string at = where + " schema '" + out.id + "'";
    const auto kind = lookup(kKinds, str(s, "kind"));
    if (!kind) fail(at + ": unknown kind '" + str(s, "kind") + "'");
    out.kind = *kind;
    out.label = str(s, "label");
    const std::int64_t version = i64(s, "version");
    if (version < 1 || version > std::numeric_limits<std::uint32_t>::max())
        fail(at + ": no valid version");
    out.version = static_cast<std::uint32_t>(version);
    out.groups = bits(s, "groups", kGroups);
    const auto fields = s.find("fields");
    if (fields == s.end() || !fields->is_array() || fields->empty())
        fail(at + ": no fields");
    for (const json& f : *fields) {
        out.fields.push_back(parse_field(f, at));
        for (std::size_t i = 0; i + 1 < out.fields.size(); ++i)
            if (out.fields[i].name == out.fields.back().name)
                fail(at + ": field '" + out.fields.back().name + "' twice");
    }
    return out;
}

plugin::Value parse_value(const json& v, const RecordFileField& f, const std::string& at) {
    switch (f.type) {
    case YR_TYPE_BOOL:
        if (v.is_boolean()) return v.get<bool>();
        break;
    case YR_TYPE_INT:
        if (v.is_number_integer() &&
            (!v.is_number_unsigned() ||
             v.get<std::uint64_t>() <=
                 static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
            return v.get<std::int64_t>();
        break;
    case YR_TYPE_UINT:
        if (v.is_number_unsigned()) return v.get<std::uint64_t>();
        break;
    case YR_TYPE_DOUBLE:
        if (v.is_number()) {
            const double d = v.get<double>();
            if (std::isfinite(d)) return d;
        }
        break;
    case YR_TYPE_STRING:
        if (v.is_string()) return v.get<std::string>();
        break;
    case YR_TYPE_ENUM:
        if (v.is_number_integer()) {
            const std::int64_t e = v.get<std::int64_t>();
            for (const plugin::EnumItem& item : f.items)
                if (item.value == e) return plugin::EnumValue{e};
            fail(at + ": " + std::to_string(e) + " is not an item of the enum");
        }
        break;
    default:
        break;
    }
    fail(at + ": the value " + v.dump() + " does not fit the field type");
}

struct Context {
    RecordFile file;
    std::vector<std::shared_ptr<const std::vector<std::string>>> names;
};

void parse_header(const json& h, Context& c, const std::string& where) {
    if (!h.is_object()) fail(where + ": the header is not a JSON object");
    if (str(h, "format") != "yantra-radio-records")
        fail(where + ": not a yantra-radio-records file (format '" + str(h, "format") + "')");
    const std::int64_t version = i64(h, "version");
    if (version < 1) fail(where + ": no format version");
    if (version > 1)
        fail(where + ": format version " + std::to_string(version) +
             " is newer than this reader (1)");
    RecordFile& f = c.file;
    f.producer = str(h, "producer");
    f.created_utc_ns = i64(h, "created_utc_ns");
    if (const auto p = h.find("plugin"); p != h.end() && p->is_object()) {
        f.plugin_id = str(*p, "id");
        f.plugin_name = str(*p, "name");
        f.plugin_version = str(*p, "version");
        f.technology_id = str(*p, "technology_id");
        f.licence = str(*p, "licence");
        f.api_version = str(*p, "api_version");
        f.capabilities = bits(*p, "capabilities", kCapabilities);
    }
    if (f.plugin_id.empty() || f.technology_id.empty())
        fail(where + ": the header names no plugin id or technology id");
    if (const auto d = h.find("device"); d != h.end() && d->is_object())
    {
        f.device_key = str(*d, "key");
        f.device_driver = str(*d, "driver");
        f.device_serial = str(*d, "serial");
    }
    const auto schemas = h.find("schemas");
    if (schemas == h.end() || !schemas->is_array() || schemas->empty())
        fail(where + ": the header has no schemas");
    for (const json& s : *schemas) {
        f.schemas.push_back(parse_schema(s, where));
        for (std::size_t i = 0; i + 1 < f.schemas.size(); ++i)
            if (f.schemas[i].id == f.schemas.back().id)
                fail(where + ": schema '" + f.schemas.back().id + "' twice");
        auto names = std::make_shared<std::vector<std::string>>();
        for (const RecordFileField& field : f.schemas.back().fields) names->push_back(field.name);
        c.names.push_back(std::move(names));
    }
}

void parse_record_line(const json& j, Context& c, const std::string& where) {
    RecordFile& f = c.file;
    const std::string id = str(j, "schema");
    std::size_t index = f.schemas.size();
    for (std::size_t i = 0; i < f.schemas.size(); ++i)
        if (f.schemas[i].id == id) index = i;
    if (index == f.schemas.size()) fail(where + ": unknown schema '" + id + "'");
    const RecordFileSchema& s = f.schemas[index];
    RadioRecord r;
    r.plugin_id = f.plugin_id;
    r.plugin_version = f.plugin_version;
    r.technology_id = f.technology_id;
    r.device_key = f.device_key;
    r.stream_id = 1;
    r.schema_index = static_cast<std::uint32_t>(index);
    r.schema_kind = s.kind;
    r.present = bits(j, "groups", kGroups);
    r.capabilities = f.capabilities;
    r.field_names = c.names[index];
    r.record.schema_id = s.id;
    r.record.schema_version = s.version;
    if (const auto t = j.find("time"); t != j.end() && t->is_object()) {
        r.record.time.monotonic_ns = i64(*t, "monotonic_ns");
        r.record.time.utc_ns = i64(*t, "utc_ns");
        const std::string source = str(*t, "utc_source");
        r.record.time.utc_source = source == "system"   ? YANTRA_TIME_SOURCE_SYSTEM
                                   : source == "device" ? YANTRA_TIME_SOURCE_DEVICE
                                                        : YANTRA_TIME_SOURCE_NONE;
    }
    r.record.flags = bits(j, "flags", kRecordFlags);
    r.record.values.resize(s.fields.size());
    if (const auto v = j.find("values"); v != j.end()) {
        if (!v->is_object()) fail(where + ": values is not an object");
        for (const auto& [name, value] : v->items()) {
            std::size_t fi = s.fields.size();
            for (std::size_t i = 0; i < s.fields.size(); ++i)
                if (s.fields[i].name == name) fi = i;
            if (fi == s.fields.size())
                fail(where + ": schema '" + s.id + "' has no field '" + name + "'");
            r.record.values[fi] =
                parse_value(value, s.fields[fi], where + " field '" + name + "'");
        }
    }
    f.lines.emplace_back(std::move(r));
    ++f.record_count;
}

void parse_event_line(const json& j, Context& c, const std::string& where) {
    const std::string name = str(j, "event");
    const auto event = lookup(kEvents, name);
    if (!event) fail(where + ": unknown event '" + name + "'");
    RadioEvent e;
    e.kind = RadioEvent::Kind::Stream;
    e.plugin_id = c.file.plugin_id;
    e.device_key = c.file.device_key;
    e.stream_id = 1;
    e.event = *event;
    e.value = i64(j, "value");
    e.monotonic_ns = i64(j, "monotonic_ns");
    c.file.lines.emplace_back(std::move(e));
    ++c.file.event_count;
}

}  // namespace

RadioSchemaView::RadioSchemaView(const RecordFileSchema& schema) : copy_(schema) {
    items_.reserve(copy_.fields.size());
    fields_.reserve(copy_.fields.size());
    for (const RecordFileField& f : copy_.fields) {
        std::vector<yr_enum_item> items;
        for (const plugin::EnumItem& item : f.items)
            items.push_back({item.value, item.name.c_str(), item.label.c_str()});
        items_.push_back(std::move(items));
        yr_field_def d{};
        d.struct_size = sizeof(yr_field_def);
        d.type = f.type;
        d.group = f.group;
        d.flags = f.flags;
        d.name = f.name.c_str();
        d.label = f.label.c_str();
        d.unit = f.unit.c_str();
        d.help = f.help.c_str();
        d.enum_items = items_.back().empty() ? nullptr : items_.back().data();
        d.enum_count = static_cast<std::uint32_t>(items_.back().size());
        fields_.push_back(d);
    }
    for (const yr_field_def& d : fields_) pointers_.push_back(&d);
    schema_.struct_size = sizeof(yr_schema);
    schema_.kind = copy_.kind;
    schema_.id = copy_.id.c_str();
    schema_.label = copy_.label.c_str();
    schema_.version = copy_.version;
    schema_.field_count = static_cast<std::uint32_t>(pointers_.size());
    schema_.fields = pointers_.data();
    schema_.groups = copy_.groups;
}

plugin::Parsed<RecordFile> parse_record_file(std::istream& in, const std::string& source) {
    plugin::Parsed<RecordFile> out;
    Context c;
    std::string line;
    std::size_t number = 0;
    bool header = false;
    try {
        while (std::getline(in, line)) {
            ++number;
            if (line.empty()) continue;
            const std::string where = source + ":" + std::to_string(number);
            json j = json::parse(line, nullptr, false);
            if (j.is_discarded()) fail(where + ": not valid JSON");
            if (!header) {
                parse_header(j, c, where);
                header = true;
                continue;
            }
            if (c.file.complete) fail(where + ": a line after the end line");
            if (!j.is_object()) fail(where + ": not a JSON object");
            const std::string type = str(j, "type");
            if (type == "record") {
                parse_record_line(j, c, where);
            } else if (type == "event") {
                parse_event_line(j, c, where);
            } else if (type == "end") {
                c.file.complete = true;
                const std::int64_t dropped = i64(j, "dropped");
                c.file.dropped = dropped > 0 ? static_cast<std::uint64_t>(dropped) : 0;
            }  // unknown line types are ignored (version 1 rule)
        }
        if (!header) fail(source + ": empty file (no header line)");
    } catch (const Error& e) {
        out.errors.push_back(e.message);
        return out;
    }
    out.value = std::move(c.file);
    return out;
}

plugin::Parsed<RecordFile> read_record_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        plugin::Parsed<RecordFile> out;
        out.errors.push_back(path.string() + ": cannot be read");
        return out;
    }
    return parse_record_file(in, path.string());
}

std::shared_ptr<const calibration::Key> device_calibration_key(const RecordFile& file) {
    if (file.device_driver.empty() && file.device_serial.empty()) return nullptr;
    auto key = std::make_shared<calibration::Key>();
    key->driver = file.device_driver;
    key->serial = file.device_serial;
    return key;
}

std::size_t replay(const RecordFile& file, MeasurementInput& input, const ReplayOptions& options) {
    // The radio schemas of the header, as the radio host hands its
    // plugin's descriptor schemas on (valid during on_radio_record).
    std::vector<std::unique_ptr<RadioSchemaView>> views;
    views.reserve(file.schemas.size());
    for (const RecordFileSchema& s : file.schemas) views.push_back(std::make_unique<RadioSchemaView>(s));
    std::shared_ptr<const calibration::Key> key = options.calibration_key;
    if (!key && options.key_from_device) key = device_calibration_key(file);

    std::size_t n = 0;
    for (const RecordFileLine& line : file.lines) {
        if (const auto* r = std::get_if<RadioRecord>(&line)) {
            RadioRecord copy = *r;
            copy.radio_schema = r->schema_index < views.size() ? &views[r->schema_index]->get() : nullptr;
            copy.calibration_key = key;
            input.on_radio_record(copy);
            ++n;
        } else {
            input.on_radio_event(std::get<RadioEvent>(line));
        }
    }
    return n;
}

}  // namespace yantra::measure
