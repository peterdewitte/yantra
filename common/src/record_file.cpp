// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Record files (RAD-65): writer, reader and C ABI conversions. Format
// reference: docs/RECORD_FILES.md.
#include <yantra/radios/record_file.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace yantra::radios {

namespace {

using json = nlohmann::ordered_json;

struct Name {
    uint32_t value;
    std::string_view name;
};

constexpr std::array<Name, 3> kKinds{{{YR_SCHEMA_MEASUREMENT, "measurement"},
                                       {YR_SCHEMA_SCAN, "scan"},
                                       {YR_SCHEMA_NEIGHBOUR, "neighbour"}}};
constexpr std::array<Name, 6> kTypes{{{YR_TYPE_BOOL, "bool"},
                                      {YR_TYPE_INT, "int"},
                                      {YR_TYPE_UINT, "uint"},
                                      {YR_TYPE_DOUBLE, "double"},
                                      {YR_TYPE_STRING, "string"},
                                      {YR_TYPE_ENUM, "enum"}}};
constexpr std::array<Name, 7> kGroups{{{YR_GROUP_QUALITY, "quality"},
                                       {YR_GROUP_C1, "c1"},
                                       {YR_GROUP_FRAME, "frame"},
                                       {YR_GROUP_BROADCAST, "broadcast"},
                                       {YR_GROUP_NEIGHBOURS, "neighbours"},
                                       {YR_GROUP_SIM_POSITION, "sim_position"},
                                       {YR_GROUP_OTHER, "other"}}};
constexpr std::array<Name, 11> kEvents{{{YR_EVENT_SYNC_FOUND, "sync_found"},
                                        {YR_EVENT_SYNC_LOST, "sync_lost"},
                                        {YR_EVENT_SERVING_CHANGED, "serving_changed"},
                                        {YR_EVENT_OVERFLOW, "overflow"},
                                        {YR_EVENT_DROPPED, "dropped"},
                                        {YR_EVENT_PLAN_APPLIED, "plan_applied"},
                                        {YR_EVENT_SCAN_DONE, "scan_done"},
                                        {YR_EVENT_END_OF_STREAM, "end_of_stream"},
                                        {YR_EVENT_DEVICE_LOST, "device_lost"},
                                        {YR_EVENT_DEVICE_BACK, "device_back"},
                                        {YR_EVENT_ERROR, "error"}}};
constexpr std::array<Name, 3> kPlanTypes{
    {{YR_PLAN_SERVING, "serving"}, {YR_PLAN_CARRIERS, "carriers"}, {YR_PLAN_SCAN, "scan"}}};
constexpr std::array<Name, 3> kTimeSources{{{YR_TIME_SOURCE_NONE, "none"},
                                            {YR_TIME_SOURCE_SYSTEM, "system"},
                                            {YR_TIME_SOURCE_DEVICE, "device"}}};
constexpr std::array<Name, 5> kRecordFlags{{{YR_RECORD_NO_SYNC, "no_sync"},
                                            {YR_RECORD_OVERLOAD, "overload"},
                                            {YR_RECORD_UNCALIBRATED, "uncalibrated"},
                                            {YR_RECORD_SIMULATED, "simulated"},
                                            {YR_RECORD_LEVEL_DBM, "level_dbm"}}};
constexpr std::array<Name, 2> kFieldFlags{{{YR_FIELD_KEY, "key"}, {YR_FIELD_DEFAULT, "default"}}};
constexpr std::array<Name, 7> kCaps{{{YR_CAP_SCAN, "scan"},
                                     {YR_CAP_HOTPLUG, "hotplug"},
                                     {YR_CAP_RETUNE_LIVE, "retune_live"},
                                     {YR_CAP_SIMULATED, "simulated"},
                                     {YR_CAP_SCENE, "scene"},
                                     {YR_CAP_SELF_CALIBRATED, "self_calibrated"},
                                     {YR_CAP_SPECTRUM, "spectrum"}}};
constexpr std::array<Name, 4> kDeviceFlags{{{YR_DEVICE_IN_USE, "in_use"},
                                            {YR_DEVICE_NO_PERMISSION, "no_permission"},
                                            {YR_DEVICE_SIMULATED, "simulated"},
                                            {YR_DEVICE_LEVEL_DBM, "level_dbm"}}};
constexpr std::array<Name, 2> kPlanFlags{{{YR_PLAN_MEASURE_NEIGHBOURS, "measure_neighbours"},
                                          {YR_PLAN_SPECTRUM, "spectrum"}}};

template <std::size_t N>
std::string_view name_of(const std::array<Name, N>& table, uint32_t value)
{
    for (const Name& n : table)
        if (n.value == value)
            return n.name;
    return {};
}

template <std::size_t N>
std::optional<uint32_t> value_of(const std::array<Name, N>& table, std::string_view name)
{
    for (const Name& n : table)
        if (n.name == name)
            return n.value;
    return std::nullopt;
}

// Bit set as an array of names; unknown bits as "bit<n>" (ignored when read).
template <std::size_t N>
json bits_to_json(const std::array<Name, N>& table, uint32_t bits)
{
    json out = json::array();
    for (uint32_t b = 0; b < 32; ++b) {
        const uint32_t bit = 1u << b;
        if ((bits & bit) == 0)
            continue;
        const std::string_view n = name_of(table, bit);
        out.push_back(n.empty() ? "bit" + std::to_string(b) : std::string(n));
    }
    return out;
}

// Reads a name array; unknown names are ignored (newer minor API versions).
template <std::size_t N>
uint32_t bits_from_json(const std::array<Name, N>& table, const json& j, const char* what)
{
    if (j.is_null())
        return 0;
    if (!j.is_array())
        throw std::invalid_argument(std::string(what) + " must be an array of names");
    uint32_t bits = 0;
    for (const json& e : j) {
        if (!e.is_string())
            throw std::invalid_argument(std::string(what) + " must be an array of names");
        if (auto v = value_of(table, e.get<std::string>()))
            bits |= *v;
    }
    return bits;
}

std::string text(const char* s)
{
    return s != nullptr ? std::string(s) : std::string();
}

const json& member(const json& j, const char* key, const char* where)
{
    auto it = j.find(key);
    if (it == j.end())
        throw std::invalid_argument(std::string(where) + ": member \"" + key + "\" missing");
    return *it;
}

std::string get_string(const json& j, const char* key, const char* where, bool required = true)
{
    auto it = j.find(key);
    if (it == j.end()) {
        if (required)
            throw std::invalid_argument(std::string(where) + ": member \"" + key + "\" missing");
        return {};
    }
    if (!it->is_string())
        throw std::invalid_argument(std::string(where) + ": \"" + key + "\" must be a string");
    return it->get<std::string>();
}

uint64_t get_uint(const json& j, const char* key, const char* where, bool required = true)
{
    auto it = j.find(key);
    if (it == j.end()) {
        if (required)
            throw std::invalid_argument(std::string(where) + ": member \"" + key + "\" missing");
        return 0;
    }
    if (!it->is_number_unsigned() && !(it->is_number_integer() && it->get<int64_t>() >= 0))
        throw std::invalid_argument(std::string(where) + ": \"" + key +
                                    "\" must be an unsigned integer");
    return it->get<uint64_t>();
}

int64_t get_int(const json& j, const char* key, const char* where, bool required = true)
{
    auto it = j.find(key);
    if (it == j.end()) {
        if (required)
            throw std::invalid_argument(std::string(where) + ": member \"" + key + "\" missing");
        return 0;
    }
    if (!it->is_number_integer())
        throw std::invalid_argument(std::string(where) + ": \"" + key + "\" must be an integer");
    if (it->is_number_unsigned() && it->get<uint64_t>() > static_cast<uint64_t>(INT64_MAX))
        throw std::invalid_argument(std::string(where) + ": \"" + key + "\" is out of range");
    return it->get<int64_t>();
}

uint32_t get_u32(const json& j, const char* key, const char* where, bool required = true)
{
    const uint64_t v = get_uint(j, key, where, required);
    if (v > UINT32_MAX)
        throw std::invalid_argument(std::string(where) + ": \"" + key + "\" is out of range");
    return static_cast<uint32_t>(v);
}

json schema_to_json(const SchemaDef& s)
{
    json fields = json::array();
    for (const FieldDef& f : s.fields) {
        json jf = {{"name", f.name},
                   {"type", std::string(type_name(f.type))},
                   {"group", f.group == YR_GROUP_CORE ? std::string("core")
                                                      : std::string(group_name(f.group))},
                   {"unit", f.unit},
                   {"label", f.label},
                   {"help", f.help},
                   {"flags", bits_to_json(kFieldFlags, f.flags)}};
        if (!f.items.empty()) {
            json items = json::array();
            for (const EnumItem& it : f.items)
                items.push_back({{"value", it.value}, {"name", it.name}, {"label", it.label}});
            jf["items"] = std::move(items);
        }
        fields.push_back(std::move(jf));
    }
    return {{"id", s.id},
            {"kind", std::string(schema_kind_name(s.kind))},
            {"label", s.label},
            {"version", s.version},
            {"groups", bits_to_json(kGroups, s.groups)},
            {"fields", std::move(fields)}};
}

SchemaDef schema_from_json(const json& j, std::size_t index)
{
    const std::string where = "schemas[" + std::to_string(index) + "]";
    if (!j.is_object())
        throw std::invalid_argument(where + " must be an object");
    SchemaDef s;
    s.id = get_string(j, "id", where.c_str());
    s.label = get_string(j, "label", where.c_str(), false);
    const std::string kind = get_string(j, "kind", where.c_str());
    auto k = value_of(kKinds, kind);
    if (!k)
        throw std::invalid_argument(where + ": unknown kind \"" + kind + "\"");
    s.kind = *k;
    s.version = get_u32(j, "version", where.c_str());
    s.groups = bits_from_json(kGroups, j.value("groups", json()), (where + ".groups").c_str());
    const json& fields = member(j, "fields", where.c_str());
    if (!fields.is_array())
        throw std::invalid_argument(where + ".fields must be an array");
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const json& jf = fields[i];
        const std::string fw = where + ".fields[" + std::to_string(i) + "]";
        if (!jf.is_object())
            throw std::invalid_argument(fw + " must be an object");
        FieldDef f;
        f.name = get_string(jf, "name", fw.c_str());
        const std::string type = get_string(jf, "type", fw.c_str());
        auto t = value_of(kTypes, type);
        if (!t)
            throw std::invalid_argument(fw + ": unknown type \"" + type + "\"");
        f.type = *t;
        const std::string group = get_string(jf, "group", fw.c_str());
        if (group == "core") {
            f.group = YR_GROUP_CORE;
        } else if (auto g = value_of(kGroups, group)) {
            f.group = *g;
        } else {
            throw std::invalid_argument(fw + ": unknown group \"" + group + "\"");
        }
        f.unit = get_string(jf, "unit", fw.c_str(), false);
        f.label = get_string(jf, "label", fw.c_str(), false);
        f.help = get_string(jf, "help", fw.c_str(), false);
        f.flags = bits_from_json(kFieldFlags, jf.value("flags", json()), (fw + ".flags").c_str());
        if (auto it = jf.find("items"); it != jf.end()) {
            if (!it->is_array())
                throw std::invalid_argument(fw + ".items must be an array");
            for (const json& ji : *it) {
                if (!ji.is_object())
                    throw std::invalid_argument(fw + ".items: each item is an object");
                EnumItem e;
                e.value = get_int(ji, "value", fw.c_str());
                e.name = get_string(ji, "name", fw.c_str());
                e.label = get_string(ji, "label", fw.c_str(), false);
                f.items.push_back(std::move(e));
            }
        }
        if (s.field_index(f.name) >= 0)
            throw std::invalid_argument(fw + ": field name \"" + f.name + "\" repeated");
        s.fields.push_back(std::move(f));
    }
    return s;
}

json value_to_json(const Value& v, const FieldDef& f)
{
    if (v.type != f.type)
        throw std::invalid_argument("field " + f.name + ": value type " +
                                    std::string(type_name(v.type)) + " does not match " +
                                    std::string(type_name(f.type)));
    switch (v.type) {
    case YR_TYPE_BOOL:
        return v.i64 != 0;
    case YR_TYPE_INT:
    case YR_TYPE_ENUM:
        return v.i64;
    case YR_TYPE_UINT:
        return v.u64;
    case YR_TYPE_DOUBLE:
        if (!std::isfinite(v.f64))
            throw std::invalid_argument("field " + f.name + ": value is not finite");
        return v.f64;
    case YR_TYPE_STRING:
        return v.str;
    default:
        throw std::invalid_argument("field " + f.name + ": invalid value type");
    }
}

Value value_from_json(const json& j, const FieldDef& f)
{
    const std::string where = "value of " + f.name;
    Value v;
    v.type = f.type;
    switch (f.type) {
    case YR_TYPE_BOOL:
        if (!j.is_boolean())
            throw std::invalid_argument(where + " must be true or false");
        v.i64 = j.get<bool>() ? 1 : 0;
        break;
    case YR_TYPE_INT:
    case YR_TYPE_ENUM:
        if (!j.is_number_integer() ||
            (j.is_number_unsigned() && j.get<uint64_t>() > static_cast<uint64_t>(INT64_MAX)))
            throw std::invalid_argument(where + " must be a signed 64-bit integer");
        v.i64 = j.get<int64_t>();
        if (f.type == YR_TYPE_ENUM) {
            bool known = false;
            for (const EnumItem& it : f.items)
                known = known || it.value == v.i64;
            if (!known)
                throw std::invalid_argument(where + ": " + std::to_string(v.i64) +
                                            " is not an item of the enum");
        }
        break;
    case YR_TYPE_UINT:
        if (!j.is_number_unsigned() && !(j.is_number_integer() && j.get<int64_t>() >= 0))
            throw std::invalid_argument(where + " must be an unsigned integer");
        v.u64 = j.get<uint64_t>();
        break;
    case YR_TYPE_DOUBLE:
        if (!j.is_number())
            throw std::invalid_argument(where + " must be a number");
        v.f64 = j.get<double>();
        if (!std::isfinite(v.f64))
            throw std::invalid_argument(where + " must be finite");
        break;
    case YR_TYPE_STRING:
        if (!j.is_string())
            throw std::invalid_argument(where + " must be a string");
        v.str = j.get<std::string>();
        if (v.str.size() > YR_MAX_STRING_VALUE_BYTES)
            throw std::invalid_argument(where + " is longer than " +
                                        std::to_string(YR_MAX_STRING_VALUE_BYTES) + " bytes");
        break;
    default:
        throw std::invalid_argument(where + ": invalid field type");
    }
    return v;
}

json header_to_json(const RecordFileHeader& h)
{
    const PluginInfo& p = h.plugin;
    json j = {{"format", std::string(record_file_format)},
              {"version", record_file_version},
              {"producer", h.producer},
              {"created_utc_ns", h.created_utc_ns},
              {"plugin",
               {{"id", p.id},
                {"name", p.name},
                {"version", p.version},
                {"technology_id", p.technology_id},
                {"licence", p.licence},
                {"api_version", std::to_string(p.api_version.major) + "." +
                                    std::to_string(p.api_version.minor)},
                {"capabilities", bits_to_json(kCaps, p.capabilities)}}}};
    if (h.device) {
        const DeviceInfo& d = *h.device;
        j["device"] = {{"key", d.key},
                       {"label", d.label},
                       {"driver", d.driver},
                       {"serial", d.serial},
                       {"module_version", d.module_version},
                       {"flags", bits_to_json(kDeviceFlags, d.flags)}};
    }
    if (h.plan) {
        const PlanInfo& pl = *h.plan;
        const std::string_view type = plan_type_name(pl.type);
        if (type.empty())
            throw std::invalid_argument("plan: unknown type " + std::to_string(pl.type));
        j["plan"] = {{"type", std::string(type)},
                     {"serving_hz", pl.serving_hz},
                     {"carriers_hz", pl.carriers_hz},
                     {"flags", bits_to_json(kPlanFlags, pl.flags)},
                     {"scan_first_hz", pl.scan_first_hz},
                     {"scan_last_hz", pl.scan_last_hz},
                     {"scan_step_hz", pl.scan_step_hz}};
    }
    json schemas = json::array();
    for (const SchemaDef& s : h.schemas) {
        if (schema_kind_name(s.kind).empty())
            throw std::invalid_argument("schema " + s.id + ": unknown kind");
        for (const FieldDef& f : s.fields)
            if (type_name(f.type).empty() ||
                (f.group != YR_GROUP_CORE && group_name(f.group).empty()))
                throw std::invalid_argument("schema " + s.id + ", field " + f.name +
                                            ": unknown type or group");
        schemas.push_back(schema_to_json(s));
    }
    j["schemas"] = std::move(schemas);
    return j;
}

RecordFileHeader header_from_json(const json& j)
{
    if (!j.is_object())
        throw std::invalid_argument("the header line is not a JSON object");
    auto f = j.find("format");
    if (f == j.end() || !f->is_string() || f->get<std::string>() != record_file_format)
        throw std::invalid_argument("not a record file (format is not \"" +
                                    std::string(record_file_format) + "\")");
    RecordFileHeader h;
    const int64_t version = get_int(j, "version", "header");
    if (version < 1)
        throw std::invalid_argument("header: invalid version " + std::to_string(version));
    if (version > record_file_version)
        throw std::invalid_argument("record file version " + std::to_string(version) +
                                    " is newer than this program reads (" +
                                    std::to_string(record_file_version) + ")");
    h.version = static_cast<int>(version);
    h.producer = get_string(j, "producer", "header", false);
    h.created_utc_ns = get_int(j, "created_utc_ns", "header", false);
    const json& p = member(j, "plugin", "header");
    if (!p.is_object())
        throw std::invalid_argument("header: plugin must be an object");
    h.plugin.id = get_string(p, "id", "plugin");
    h.plugin.name = get_string(p, "name", "plugin", false);
    h.plugin.version = get_string(p, "version", "plugin", false);
    h.plugin.technology_id = get_string(p, "technology_id", "plugin", false);
    h.plugin.licence = get_string(p, "licence", "plugin", false);
    const std::string api = get_string(p, "api_version", "plugin");
    unsigned major = 0;
    unsigned minor = 0;
    char extra = 0;
    if (std::sscanf(api.c_str(), "%u.%u%c", &major, &minor, &extra) != 2)
        throw std::invalid_argument("plugin: api_version \"" + api + "\" is not MAJOR.MINOR");
    h.plugin.api_version = {major, minor};
    h.plugin.capabilities = bits_from_json(kCaps, p.value("capabilities", json()),
                                           "plugin.capabilities");
    if (auto d = j.find("device"); d != j.end() && !d->is_null()) {
        if (!d->is_object())
            throw std::invalid_argument("header: device must be an object");
        DeviceInfo di;
        di.key = get_string(*d, "key", "device");
        di.label = get_string(*d, "label", "device", false);
        di.driver = get_string(*d, "driver", "device", false);
        di.serial = get_string(*d, "serial", "device", false);
        di.module_version = get_string(*d, "module_version", "device", false);
        di.flags = bits_from_json(kDeviceFlags, d->value("flags", json()), "device.flags");
        h.device = std::move(di);
    }
    if (auto pl = j.find("plan"); pl != j.end() && !pl->is_null()) {
        if (!pl->is_object())
            throw std::invalid_argument("header: plan must be an object");
        PlanInfo pi;
        const std::string type = get_string(*pl, "type", "plan");
        auto t = value_of(kPlanTypes, type);
        if (!t)
            throw std::invalid_argument("plan: unknown type \"" + type + "\"");
        pi.type = *t;
        pi.serving_hz = get_uint(*pl, "serving_hz", "plan", false);
        if (auto c = pl->find("carriers_hz"); c != pl->end()) {
            if (!c->is_array())
                throw std::invalid_argument("plan: carriers_hz must be an array");
            for (const json& e : *c) {
                if (!e.is_number_unsigned())
                    throw std::invalid_argument("plan: carriers_hz holds unsigned integers");
                pi.carriers_hz.push_back(e.get<uint64_t>());
            }
        }
        pi.flags = bits_from_json(kPlanFlags, pl->value("flags", json()), "plan.flags");
        pi.scan_first_hz = get_uint(*pl, "scan_first_hz", "plan", false);
        pi.scan_last_hz = get_uint(*pl, "scan_last_hz", "plan", false);
        pi.scan_step_hz = get_uint(*pl, "scan_step_hz", "plan", false);
        h.plan = std::move(pi);
    }
    const json& schemas = member(j, "schemas", "header");
    if (!schemas.is_array())
        throw std::invalid_argument("header: schemas must be an array");
    for (std::size_t i = 0; i < schemas.size(); ++i) {
        SchemaDef s = schema_from_json(schemas[i], i);
        if (h.schema_index(s.id) >= 0)
            throw std::invalid_argument("header: schema id \"" + s.id + "\" repeated");
        h.schemas.push_back(std::move(s));
    }
    return h;
}

Record record_from_json(const json& j, const RecordFileHeader& h)
{
    Record r;
    const std::string id = get_string(j, "schema", "record");
    const int index = h.schema_index(id);
    if (index < 0)
        throw std::invalid_argument("record: schema \"" + id + "\" is not in the header");
    r.schema_index = static_cast<uint32_t>(index);
    const SchemaDef& s = h.schemas[r.schema_index];
    const json& t = member(j, "time", "record");
    if (!t.is_object())
        throw std::invalid_argument("record: time must be an object");
    r.time.monotonic_ns = get_int(t, "monotonic_ns", "record.time");
    r.time.utc_ns = get_int(t, "utc_ns", "record.time", false);
    const std::string src = get_string(t, "utc_source", "record.time", false);
    if (src.empty()) {
        r.time.utc_source = YR_TIME_SOURCE_NONE;
    } else if (auto v = value_of(kTimeSources, src)) {
        r.time.utc_source = *v;
    } else {
        throw std::invalid_argument("record.time: unknown utc_source \"" + src + "\"");
    }
    r.flags = bits_from_json(kRecordFlags, j.value("flags", json()), "record.flags");
    r.present = bits_from_json(kGroups, j.value("groups", json()), "record.groups");
    if ((r.present & ~s.groups) != 0)
        throw std::invalid_argument("record: a group is not declared by schema \"" + s.id + "\"");
    r.values.assign(s.fields.size(), Value{});
    const json& values = member(j, "values", "record");
    if (!values.is_object())
        throw std::invalid_argument("record: values must be an object");
    for (auto it = values.begin(); it != values.end(); ++it) {
        const int fi = s.field_index(it.key());
        if (fi < 0)
            throw std::invalid_argument("record: field \"" + it.key() +
                                        "\" is not in schema \"" + s.id + "\"");
        const FieldDef& f = s.fields[static_cast<std::size_t>(fi)];
        if (f.group != YR_GROUP_CORE && (r.present & f.group) == 0)
            throw std::invalid_argument("record: field \"" + f.name +
                                        "\" belongs to a group the record does not carry");
        if (!it.value().is_null())
            r.values[static_cast<std::size_t>(fi)] = value_from_json(it.value(), f);
    }
    return r;
}

} // namespace

int SchemaDef::field_index(std::string_view name) const
{
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (fields[i].name == name)
            return static_cast<int>(i);
    return -1;
}

int RecordFileHeader::schema_index(std::string_view id) const
{
    for (std::size_t i = 0; i < schemas.size(); ++i)
        if (schemas[i].id == id)
            return static_cast<int>(i);
    return -1;
}

std::string_view schema_kind_name(uint32_t kind) { return name_of(kKinds, kind); }
std::string_view type_name(uint32_t type) { return name_of(kTypes, type); }
std::string_view group_name(uint32_t group_bit) { return name_of(kGroups, group_bit); }
std::string_view event_name(uint32_t event) { return name_of(kEvents, event); }
std::string_view plan_type_name(uint32_t type) { return name_of(kPlanTypes, type); }
std::string_view time_source_name(uint32_t source) { return name_of(kTimeSources, source); }
std::optional<uint32_t> event_from_name(std::string_view name) { return value_of(kEvents, name); }

SchemaDef schema_from_c(const yr_schema& schema)
{
    SchemaDef s;
    s.id = text(schema.id);
    s.label = text(schema.label);
    s.kind = schema.kind;
    s.version = schema.version;
    s.groups = schema.groups;
    for (uint32_t i = 0; i < schema.field_count && schema.fields != nullptr; ++i) {
        const yr_field_def* fd = schema.fields[i];
        FieldDef f;
        if (fd != nullptr) {
            f.name = text(fd->name);
            f.label = text(fd->label);
            f.unit = text(fd->unit);
            f.help = text(fd->help);
            f.type = fd->type;
            f.group = fd->group;
            f.flags = fd->flags;
            for (uint32_t k = 0; k < fd->enum_count && fd->enum_items != nullptr; ++k)
                f.items.push_back(
                    {fd->enum_items[k].value, text(fd->enum_items[k].name),
                     text(fd->enum_items[k].label)});
        }
        s.fields.push_back(std::move(f));
    }
    return s;
}

PluginInfo plugin_from_c(const yr_descriptor& d)
{
    PluginInfo p;
    p.id = text(d.id);
    p.name = text(d.name);
    p.version = text(d.version);
    p.technology_id = text(d.technology_id);
    p.licence = text(d.licence);
    p.api_version = d.api_version;
    p.capabilities = d.capabilities;
    return p;
}

RecordFileHeader header_from_descriptor(const yr_descriptor& d)
{
    RecordFileHeader h;
    h.plugin = plugin_from_c(d);
    for (uint32_t i = 0; i < d.schema_count && d.schemas != nullptr; ++i)
        if (d.schemas[i] != nullptr)
            h.schemas.push_back(schema_from_c(*d.schemas[i]));
    return h;
}

DeviceInfo device_from_c(const yr_device_info& d)
{
    DeviceInfo out;
    out.key = text(d.key);
    out.label = text(d.label);
    out.driver = text(d.driver);
    out.serial = text(d.serial);
    out.module_version = text(d.module_version);
    out.flags = d.flags;
    return out;
}

PlanInfo plan_from_c(const yr_plan& p)
{
    PlanInfo out;
    out.type = p.type;
    out.serving_hz = p.serving_hz;
    if (p.carriers_hz != nullptr)
        out.carriers_hz.assign(p.carriers_hz, p.carriers_hz + p.carrier_count);
    out.flags = p.flags;
    out.scan_first_hz = p.scan_first_hz;
    out.scan_last_hz = p.scan_last_hz;
    out.scan_step_hz = p.scan_step_hz;
    return out;
}

Record record_from_c(const yr_record& r, const SchemaDef& schema)
{
    Record out;
    out.schema_index = r.schema_index;
    out.time = r.time;
    out.flags = r.flags;
    out.present = r.present;
    out.values.assign(schema.fields.size(), Value{});
    const uint32_t n = r.values != nullptr ? r.value_count : 0;
    for (uint32_t i = 0; i < n && i < schema.fields.size(); ++i) {
        const FieldDef& f = schema.fields[i];
        if (f.group != YR_GROUP_CORE && (r.present & f.group) == 0)
            continue;
        const yr_value& v = r.values[i];
        Value& o = out.values[i];
        o.type = v.type;
        switch (v.type) {
        case YR_TYPE_BOOL:
        case YR_TYPE_INT:
        case YR_TYPE_ENUM:
            o.i64 = v.u.i64;
            break;
        case YR_TYPE_UINT:
            o.u64 = v.u.u64;
            break;
        case YR_TYPE_DOUBLE:
            o.f64 = v.u.f64;
            break;
        case YR_TYPE_STRING:
            o.str = text(v.u.str);
            break;
        default:
            o.type = YR_TYPE_NONE;
            break;
        }
    }
    return out;
}

SchemaView::SchemaView(const SchemaDef& s)
{
    items.resize(s.fields.size());
    fields.resize(s.fields.size());
    for (size_t i = 0; i < s.fields.size(); ++i) {
        const FieldDef& f = s.fields[i];
        for (const EnumItem& e : f.items)
            items[i].push_back({e.value, e.name.c_str(), e.label.c_str()});
        yr_field_def& c = fields[i];
        c = {};
        c.struct_size = sizeof(yr_field_def);
        c.type = f.type;
        c.group = f.group;
        c.flags = f.flags;
        c.name = f.name.c_str();
        c.label = f.label.c_str();
        c.unit = f.unit.c_str();
        c.help = f.help.c_str();
        c.enum_items = items[i].empty() ? nullptr : items[i].data();
        c.enum_count = static_cast<uint32_t>(items[i].size());
    }
    for (const yr_field_def& f : fields)
        field_ptrs.push_back(&f);
    schema_c.struct_size = sizeof(yr_schema);
    schema_c.kind = s.kind;
    schema_c.id = s.id.c_str();
    schema_c.label = s.label.c_str();
    schema_c.version = s.version;
    schema_c.field_count = static_cast<uint32_t>(fields.size());
    schema_c.fields = field_ptrs.data();
    schema_c.groups = s.groups;
}

RecordView::RecordView(const Record& r)
{
    values.reserve(r.values.size());
    for (const Value& v : r.values) {
        yr_value c = yr_value_none();
        c.type = v.type;
        switch (v.type) {
        case YR_TYPE_BOOL:
        case YR_TYPE_INT:
        case YR_TYPE_ENUM:
            c.u.i64 = v.i64;
            break;
        case YR_TYPE_UINT:
            c.u.u64 = v.u64;
            break;
        case YR_TYPE_DOUBLE:
            c.u.f64 = v.f64;
            break;
        case YR_TYPE_STRING:
            c.u.str = v.str.c_str();
            break;
        default:
            c.type = YR_TYPE_NONE;
            break;
        }
        values.push_back(c);
    }
    record.struct_size = sizeof(yr_record);
    record.schema_index = r.schema_index;
    record.time = r.time;
    record.flags = r.flags;
    record.present = r.present;
    record.value_count = static_cast<uint32_t>(values.size());
    record.reserved = 0;
    record.values = values.data();
}

RecordFileWriter::RecordFileWriter(std::ostream& out) : out_(out) {}

void RecordFileWriter::write_header(const RecordFileHeader& header)
{
    if (have_header_)
        throw std::logic_error("record file header written twice");
    const json j = header_to_json(header);
    header_ = header;
    header_.version = record_file_version;
    have_header_ = true;
    out_ << j.dump() << '\n';
}

std::string RecordFileWriter::record_line(const Record& r) const
{
    if (!have_header_)
        throw std::logic_error("record file header not written");
    if (r.schema_index >= header_.schemas.size())
        throw std::invalid_argument("record: schema index " + std::to_string(r.schema_index) +
                                    " is not in the header");
    const SchemaDef& s = header_.schemas[r.schema_index];
    if (r.values.size() > s.fields.size())
        throw std::invalid_argument("record: more values than schema \"" + s.id + "\" has fields");
    json values = json::object();
    for (std::size_t i = 0; i < r.values.size(); ++i) {
        const Value& v = r.values[i];
        if (v.type == YR_TYPE_NONE)
            continue;
        const FieldDef& f = s.fields[i];
        if (f.group != YR_GROUP_CORE && (r.present & f.group) == 0)
            continue;
        values[f.name] = value_to_json(v, f);
    }
    const std::string_view src = time_source_name(r.time.utc_source);
    json j = {{"type", "record"},
              {"schema", s.id},
              {"time",
               {{"monotonic_ns", r.time.monotonic_ns},
                {"utc_ns", r.time.utc_ns},
                {"utc_source", src.empty() ? std::string("none") : std::string(src)}}},
              {"flags", bits_to_json(kRecordFlags, r.flags)},
              {"groups", bits_to_json(kGroups, r.present)},
              {"values", std::move(values)}};
    return j.dump();
}

void RecordFileWriter::write_record(const Record& record)
{
    const std::string line = record_line(record);
    out_ << line << '\n';
    ++records_;
}

std::string RecordFileWriter::event_line(const Event& e)
{
    const std::string_view name = event_name(e.event);
    if (name.empty())
        throw std::invalid_argument("event: unknown event " + std::to_string(e.event));
    json j = {{"type", "event"},
              {"event", std::string(name)},
              {"value", e.value},
              {"monotonic_ns", e.monotonic_ns}};
    return j.dump();
}

void RecordFileWriter::write_event(const Event& event)
{
    if (!have_header_)
        throw std::logic_error("record file header not written");
    out_ << event_line(event) << '\n';
    ++events_;
}

void RecordFileWriter::write_end(const EndSummary& end)
{
    if (!have_header_)
        throw std::logic_error("record file header not written");
    json j = {{"type", "end"},
              {"records", end.records},
              {"events", end.events},
              {"dropped", end.dropped}};
    out_ << j.dump() << '\n';
    out_.flush();
}

RecordFileReader::RecordFileReader(std::istream& in) : in_(in) {}

bool RecordFileReader::read_header(std::string& error)
{
    std::string line;
    if (!std::getline(in_, line)) {
        error = "empty file: no header line";
        return false;
    }
    line_number_ = 1;
    try {
        header_ = header_from_json(json::parse(line));
    } catch (const json::exception& e) {
        error = std::string("header is not valid JSON: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    have_header_ = true;
    return true;
}

RecordFileReader::Result RecordFileReader::next(RecordFileLine& out, std::string& error)
{
    if (!have_header_) {
        error = "header not read";
        return Result::error;
    }
    std::string line;
    while (std::getline(in_, line)) {
        ++line_number_;
        if (line.find_first_not_of(" \t\r") == std::string::npos)
            continue;
        try {
            const json j = json::parse(line);
            if (!j.is_object())
                throw std::invalid_argument("a line is a JSON object");
            const std::string type = get_string(j, "type", "line");
            if (type == "record") {
                out.kind = RecordFileLine::Kind::record;
                out.record = record_from_json(j, header_);
                return Result::line;
            }
            if (type == "event") {
                out.kind = RecordFileLine::Kind::event;
                const std::string name = get_string(j, "event", "event");
                auto ev = event_from_name(name);
                if (!ev)
                    throw std::invalid_argument("event: unknown event \"" + name + "\"");
                out.event.event = *ev;
                out.event.value = get_int(j, "value", "event");
                out.event.monotonic_ns = get_int(j, "monotonic_ns", "event", false);
                return Result::line;
            }
            if (type == "end") {
                out.kind = RecordFileLine::Kind::end;
                out.end.records = get_uint(j, "records", "end");
                out.end.events = get_uint(j, "events", "end", false);
                out.end.dropped = get_uint(j, "dropped", "end", false);
                return Result::line;
            }
            // Another line type of a later compatible version: skipped.
        } catch (const json::exception& e) {
            error = "line " + std::to_string(line_number_) + ": not valid JSON: " + e.what();
            return Result::error;
        } catch (const std::exception& e) {
            error = "line " + std::to_string(line_number_) + ": " + e.what();
            return Result::error;
        }
    }
    return Result::end_of_file;
}

bool read_record_file(const std::filesystem::path& path, RecordFileHeader& header,
                      std::vector<RecordFileLine>& lines, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = path.string() + ": cannot open";
        return false;
    }
    RecordFileReader reader(in);
    std::string why;
    if (!reader.read_header(why)) {
        error = path.string() + ":1: " + why;
        return false;
    }
    header = reader.header();
    lines.clear();
    RecordFileLine line;
    for (;;) {
        const auto r = reader.next(line, why);
        if (r == RecordFileReader::Result::end_of_file)
            return true;
        if (r == RecordFileReader::Result::error) {
            error = path.string() + ": " + why;
            return false;
        }
        lines.push_back(line);
    }
}

} // namespace yantra::radios
