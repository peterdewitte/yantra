// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The JSON lines of docs/formats/RECORDING.md (yantra-recording/1).

#include "codec.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>

#include <yantra/radio.h>

#include "../measure/record_json.h"
#include "yantra/core/platform/time.h"

namespace yantra::recording::codec {

namespace {

namespace ym = yantra::measure;

std::string str(const json& j, const char* k) {
    const auto it = j.find(k);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

bool has_int(const json& j, const char* k) {
    const auto it = j.find(k);
    return it != j.end() && it->is_number_integer();
}

std::int64_t i64(const json& j, const char* k, std::int64_t fallback = 0) {
    const auto it = j.find(k);
    if (it == j.end() || !it->is_number_integer()) return fallback;
    if (it->is_number_unsigned() &&
        it->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return fallback;
    return it->get<std::int64_t>();
}

std::uint64_t u64(const json& j, const char* k, std::uint64_t fallback = 0) {
    const auto it = j.find(k);
    return it != j.end() && it->is_number_unsigned() ? it->get<std::uint64_t>() : fallback;
}

std::uint32_t u32(const json& j, const char* k, std::uint32_t fallback = 0) {
    const std::uint64_t v = u64(j, k, fallback);
    return v > std::numeric_limits<std::uint32_t>::max() ? fallback : static_cast<std::uint32_t>(v);
}

double dbl(const json& j, const char* k, double fallback = 0.0) {
    const auto it = j.find(k);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

bool boolean(const json& j, const char* k) {
    const auto it = j.find(k);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

json number_or_null(double v) { return std::isfinite(v) ? json(v) : json(nullptr); }

const char* role_name(ym::CarrierRole r) {
    switch (r) {
    case ym::CarrierRole::Serving: return "serving";
    case ym::CarrierRole::Neighbour: return "neighbour";
    case ym::CarrierRole::Other: return "other";
    case ym::CarrierRole::ScanChannel: return "scan";
    case ym::CarrierRole::NeighbourEntry: return "neighbour_entry";
    }
    return "other";
}

ym::CarrierRole role_of(const std::string& s) {
    if (s == "serving") return ym::CarrierRole::Serving;
    if (s == "neighbour") return ym::CarrierRole::Neighbour;
    if (s == "scan") return ym::CarrierRole::ScanChannel;
    if (s == "neighbour_entry") return ym::CarrierRole::NeighbourEntry;
    return ym::CarrierRole::Other;
}

constexpr std::pair<ym::RadioEvent::Kind, const char*> kEventKinds[] = {
    {ym::RadioEvent::Kind::Stream, "stream"},     {ym::RadioEvent::Kind::Started, "started"},
    {ym::RadioEvent::Kind::Stopped, "stopped"},   {ym::RadioEvent::Kind::Lost, "lost"},
    {ym::RadioEvent::Kind::Reopened, "reopened"}, {ym::RadioEvent::Kind::Failed, "failed"}};

json time_json(const yantra_time& t) { return json::array({t.monotonic_ns, t.utc_ns, t.utc_source}); }

bool read_time(const json& j, yantra_time& t) {
    if (!j.is_array() || j.size() != 3 || !j[0].is_number_integer() || !j[1].is_number_integer() ||
        !j[2].is_number_unsigned())
        return false;
    t.monotonic_ns = j[0].get<std::int64_t>();
    t.utc_ns = j[1].get<std::int64_t>();
    t.utc_source = j[2].get<std::uint32_t>();
    return true;
}

json values_json(const plugin::Record& r) {
    json v = json::array();
    for (const plugin::Value& x : r.values) v.push_back(ym::value_json(x));
    return v;
}

// The record part shared by `rec` lines and state carriers.
void put_record(json& j, std::uint32_t src, const ym::RadioRecord& r) {
    j["src"] = src;
    j["dev"] = r.device_key;
    j["stream"] = r.stream_id;
    j["groups"] = r.present;
    j["time"] = time_json(r.record.time);
    j["flags"] = r.record.flags;
    j["v"] = values_json(r.record);
}

bool read_record(const json& j, const File& file, ReadContext& c, ym::RadioRecord& r, std::string& error) {
    const std::uint32_t src = u32(j, "src");
    const auto it = c.sources.find(src);
    if (it == c.sources.end()) {
        error = "unknown source " + std::to_string(src);
        return false;
    }
    const Source& s = file.sources[it->second];
    const ym::RecordFileSchema& schema = s.schema->schema();
    r.plugin_id = s.plugin_id;
    r.plugin_version = s.plugin_version;
    r.technology_id = s.technology_id;
    r.capabilities = s.capabilities;
    r.schema_index = s.schema_index;
    r.schema_kind = schema.kind;
    r.radio_schema = &s.schema->get();
    r.field_names = c.names[src];
    r.device_key = str(j, "dev");
    r.stream_id = u64(j, "stream");
    r.present = u32(j, "groups");
    r.record.schema_id = schema.id;
    r.record.schema_version = schema.version;
    const auto t = j.find("time");
    if (t == j.end() || !read_time(*t, r.record.time)) {
        error = "no valid time";
        return false;
    }
    r.record.flags = u32(j, "flags");
    const auto v = j.find("v");
    if (v == j.end() || !v->is_array() || v->size() != schema.fields.size()) {
        error = "values do not match schema '" + schema.id + "'";
        return false;
    }
    r.record.values.resize(schema.fields.size());
    for (std::size_t i = 0; i < schema.fields.size(); ++i)
        if (!ym::parse_value_json((*v)[i], schema.fields[i], r.record.values[i], error)) return false;
    return true;
}

bool read_correction_ref(const json& j, ReadContext& c, bool& corrected, ym::LevelCorrection& out,
                         std::string& error) {
    corrected = j.contains("cal");
    if (!corrected) return true;
    const auto it = c.corrections.find(u32(j, "cal"));
    if (it == c.corrections.end()) {
        error = "unknown correction";
        return false;
    }
    out = it->second;
    return true;
}

json stats_json(const ym::PipelineStats& s) {
    return {{"records", s.records},
            {"accepted", s.accepted},
            {"generic", s.generic},
            {"unmapped", s.unmapped},
            {"schema_mismatch", s.schema_mismatch},
            {"implausible", s.implausible},
            {"out_of_order", s.out_of_order},
            {"duplicates", s.duplicates},
            {"late", s.late},
            {"events", s.events},
            {"cycles", s.cycles},
            {"carrier_samples", s.carrier_samples},
            {"held", s.held},
            {"stale", s.stale},
            {"expired", s.expired},
            {"skipped_cycles", s.skipped_cycles},
            {"c1_radio", s.c1_radio},
            {"c1_estimated", s.c1_estimated},
            {"c1_failed", s.c1_failed}};
}

ym::PipelineStats read_stats(const json& j) {
    ym::PipelineStats s;
    s.records = u64(j, "records");
    s.accepted = u64(j, "accepted");
    s.generic = u64(j, "generic");
    s.unmapped = u64(j, "unmapped");
    s.schema_mismatch = u64(j, "schema_mismatch");
    s.implausible = u64(j, "implausible");
    s.out_of_order = u64(j, "out_of_order");
    s.duplicates = u64(j, "duplicates");
    s.late = u64(j, "late");
    s.events = u64(j, "events");
    s.cycles = u64(j, "cycles");
    s.carrier_samples = u64(j, "carrier_samples");
    s.held = u64(j, "held");
    s.stale = u64(j, "stale");
    s.expired = u64(j, "expired");
    s.skipped_cycles = u64(j, "skipped_cycles");
    s.c1_radio = u64(j, "c1_radio");
    s.c1_estimated = u64(j, "c1_estimated");
    s.c1_failed = u64(j, "c1_failed");
    return s;
}

json pipeline_json(const ym::PipelineConfig& p) {
    return {{"period_ns", p.period_ns},
            {"stale_after_ns", p.stale_after_ns},
            {"drop_after_ns", p.drop_after_ns},
            {"scan_stale_after_ns", p.scan_stale_after_ns},
            {"scan_drop_after_ns", p.scan_drop_after_ns},
            {"late_after_ns", p.late_after_ns},
            {"max_catch_up", p.max_catch_up},
            {"min_level", p.min_level},
            {"max_level", p.max_level},
            {"ms_power_dbm", p.ms_power_dbm}};
}

ym::PipelineConfig read_pipeline(const json& j) {
    ym::PipelineConfig p;
    p.period_ns = i64(j, "period_ns", p.period_ns);
    p.stale_after_ns = i64(j, "stale_after_ns", p.stale_after_ns);
    p.drop_after_ns = i64(j, "drop_after_ns", p.drop_after_ns);
    p.scan_stale_after_ns = i64(j, "scan_stale_after_ns", p.scan_stale_after_ns);
    p.scan_drop_after_ns = i64(j, "scan_drop_after_ns", p.scan_drop_after_ns);
    p.late_after_ns = i64(j, "late_after_ns", p.late_after_ns);
    p.max_catch_up = u32(j, "max_catch_up", p.max_catch_up);
    p.min_level = dbl(j, "min_level", p.min_level);
    p.max_level = dbl(j, "max_level", p.max_level);
    p.ms_power_dbm = dbl(j, "ms_power_dbm", p.ms_power_dbm);
    return p;
}

std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

}  // namespace

std::string iso_utc(std::int64_t utc_ns) {
    std::time_t secs = static_cast<std::time_t>(utc_ns / 1'000'000'000);
    if (utc_ns < 0 && utc_ns % 1'000'000'000 != 0) --secs;
    const std::tm tm = platform::utc_tm(secs);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

std::int64_t parse_iso_utc(const std::string& text) {
    std::tm tm{};
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    char z = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c", &y, &mo, &d, &h, &mi, &s, &z) != 7 || z != 'Z')
        return 0;
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    return static_cast<std::int64_t>(platform::utc_time(tm)) * 1'000'000'000;
}

std::string source_key(const ym::RadioRecord& r) {
    return r.plugin_id + '\x1f' + r.plugin_version + '\x1f' + r.technology_id + '\x1f' +
           std::to_string(r.capabilities) + '\x1f' + std::to_string(r.schema_index) + '\x1f' +
           r.record.schema_id + '\x1f' + std::to_string(r.record.schema_version);
}

std::uint32_t source_id(Declared& d, const ym::RadioRecord& r, const ym::RecordFileSchema& schema,
                        std::vector<std::string>& out) {
    const std::string key = source_key(r);
    const auto it = d.sources.find(key);
    if (it != d.sources.end()) return it->second;
    const auto id = static_cast<std::uint32_t>(d.sources.size() + 1);
    d.sources.emplace(key, id);
    json j{{"t", "source"},
           {"id", id},
           {"plugin", r.plugin_id},
           {"plugin_version", r.plugin_version},
           {"technology", r.technology_id},
           {"capabilities", r.capabilities},
           {"schema_index", r.schema_index},
           {"schema", ym::schema_json(schema)}};
    out.push_back(dump(j));
    return id;
}

std::uint32_t correction_id(Declared& d, const ym::LevelCorrection& c, std::vector<std::string>& out) {
    json j{{"calibrated", c.calibrated},
           {"offset_db", c.offset_db},
           {"correction_db", c.correction_db},
           {"source", c.source},
           {"key", c.key},
           {"date_unix", c.calibration_date_unix},
           {"expired", c.expired},
           {"extrapolated", c.extrapolated}};
    const std::string key = dump(j);
    const auto it = d.corrections.find(key);
    if (it != d.corrections.end()) return it->second;
    const auto id = static_cast<std::uint32_t>(d.corrections.size() + 1);
    d.corrections.emplace(key, id);
    j["t"] = "cal";
    j["id"] = id;
    out.push_back(dump(j));
    return id;
}

std::string header_line(const Header& h) {
    json radios = json::array();
    for (const RadioInfo& r : h.info.radios)
        radios.push_back({{"plugin", r.plugin_id},
                          {"plugin_version", r.plugin_version},
                          {"device", r.device_key},
                          {"driver", r.driver},
                          {"serial", r.serial},
                          {"label", r.label}});
    json cal = json::array();
    for (const CalibrationInfo& c : h.info.calibration)
        cal.push_back({{"key", c.key},
                       {"created_unix", c.created_unix},
                       {"valid_days", c.valid_days},
                       {"method", c.method},
                       {"simulated", c.simulated}});
    json antenna = json::array();
    for (const AntennaInfo& a : h.info.antenna)
        antenna.push_back({{"plugin", a.plugin_id},
                           {"device", a.device_key},
                           {"gain_dbi", a.gain_dbi},
                           {"cable_loss_db", a.cable_loss_db},
                           {"attenuator_db", a.attenuator_db}});
    json j{{"t", "header"},
           {"format", std::string(kFormat) + "/" + std::to_string(h.version)},
           {"tool", {{"name", h.tool_name}, {"version", h.tool_version}}},
           {"session",
            {{"id", h.session_id},
             {"name", h.session_name},
             {"file_index", h.file_index},
             {"started_utc", iso_utc(h.session_started_utc_ns)},
             {"started_ns", h.session_started_utc_ns},
             {"continued", h.continued}}},
           {"file_started_utc", iso_utc(h.started_utc_ns)},
           {"file_started_ns", h.started_utc_ns},
           {"file_started_mono", h.started_mono_ns},
           {"next_marker", h.next_marker},
           {"radios", std::move(radios)},
           {"calibration", std::move(cal)},
           {"antenna", std::move(antenna)},
           {"cell_db", h.info.cell_db.empty() ? json(nullptr) : json(h.info.cell_db)},
           {"notes", h.info.notes},
           {"project", h.info.project ? json{{"file", h.info.project->file}, {"floor", h.info.project->floor_id}}
                                      : json(nullptr)},
           {"pipeline", pipeline_json(h.pipeline)},
           {"split", {{"minutes", h.split_minutes}, {"messages", h.split_messages}}},
           {"keyframe_s", h.keyframe_s},
           {"compression", h.compression == Compression::Zstd ? "zstd" : "none"},
           {"cut", h.cut ? json{{"from_ns", h.cut->from_ns},
                                {"to_ns", h.cut->to_ns},
                                {"source", h.cut->source},
                                {"source_session", h.cut->source_session}}
                         : json(nullptr)}};
    return dump(j);
}

bool read_header(const json& j, Header& h, std::string& error) {
    if (!j.is_object() || str(j, "t") != "header") {
        error = "not a Yantra recording (no header line)";
        return false;
    }
    const std::string format = str(j, "format");
    const std::string prefix = std::string(kFormat) + "/";
    if (format.rfind(prefix, 0) != 0) {
        error = "not a Yantra recording (format '" + format + "')";
        return false;
    }
    int version = 0;
    if (std::sscanf(format.c_str() + prefix.size(), "%d", &version) != 1 || version < 1) {
        error = "not a Yantra recording (format '" + format + "')";
        return false;
    }
    if (version > kFormatVersion) {
        error = "made by a newer Yantra (recording format " + std::to_string(version) +
                ", this version reads up to " + std::to_string(kFormatVersion) + ")";
        return false;
    }
    h.version = version;
    if (const auto t = j.find("tool"); t != j.end() && t->is_object()) {
        h.tool_name = str(*t, "name");
        h.tool_version = str(*t, "version");
    }
    if (const auto s = j.find("session"); s != j.end() && s->is_object()) {
        h.session_id = str(*s, "id");
        h.session_name = str(*s, "name");
        h.file_index = u32(*s, "file_index");
        h.session_started_utc_ns = has_int(*s, "started_ns") ? i64(*s, "started_ns") : parse_iso_utc(str(*s, "started_utc"));
        h.continued = boolean(*s, "continued");
    }
    h.started_utc_ns = has_int(j, "file_started_ns") ? i64(j, "file_started_ns") : parse_iso_utc(str(j, "file_started_utc"));
    h.started_mono_ns = i64(j, "file_started_mono");
    h.next_marker = u32(j, "next_marker", 1);
    if (const auto r = j.find("radios"); r != j.end() && r->is_array())
        for (const json& x : *r)
            if (x.is_object())
                h.info.radios.push_back({str(x, "plugin"), str(x, "plugin_version"), str(x, "device"),
                                         str(x, "driver"), str(x, "serial"), str(x, "label")});
    if (const auto r = j.find("calibration"); r != j.end() && r->is_array())
        for (const json& x : *r)
            if (x.is_object())
                h.info.calibration.push_back({str(x, "key"), i64(x, "created_unix"),
                                              static_cast<int>(i64(x, "valid_days")), str(x, "method"),
                                              boolean(x, "simulated")});
    if (const auto r = j.find("antenna"); r != j.end() && r->is_array())
        for (const json& x : *r)
            if (x.is_object())
                h.info.antenna.push_back({str(x, "plugin"), str(x, "device"), dbl(x, "gain_dbi"),
                                          dbl(x, "cable_loss_db"), dbl(x, "attenuator_db")});
    h.info.cell_db = str(j, "cell_db");
    h.info.notes = str(j, "notes");
    if (const auto p = j.find("project"); p != j.end() && p->is_object())
        h.info.project = ProjectLink{str(*p, "file"), str(*p, "floor")};
    if (const auto p = j.find("pipeline"); p != j.end() && p->is_object()) h.pipeline = read_pipeline(*p);
    if (const std::string problem = h.pipeline.validate(); !problem.empty()) {
        error = "header: pipeline configuration: " + problem;
        return false;
    }
    if (const auto s = j.find("split"); s != j.end() && s->is_object()) {
        h.split_minutes = dbl(*s, "minutes");
        h.split_messages = u64(*s, "messages");
    }
    h.keyframe_s = dbl(j, "keyframe_s");
    h.compression = str(j, "compression") == "zstd" ? Compression::Zstd : Compression::None;
    if (const auto c = j.find("cut"); c != j.end() && c->is_object())
        h.cut = CutInfo{i64(*c, "from_ns"), i64(*c, "to_ns"), str(*c, "source"), str(*c, "source_session")};
    return true;
}

std::string input_line(Declared& d, const ym::PipelineInput& in, std::vector<std::string>& out) {
    json j;
    if (in.record != nullptr) {
        const ym::RadioRecord& r = *in.record;
        std::uint32_t src = 0;
        if (const auto it = d.sources.find(source_key(r)); it != d.sources.end()) src = it->second;
        else if (r.radio_schema != nullptr) src = source_id(d, r, ym::record_file_schema(*r.radio_schema), out);
        j["t"] = "rec";
        j["n"] = in.sequence;
        j["mono"] = in.arrival_ns;
        put_record(j, src, r);
        if (in.corrected) j["cal"] = correction_id(d, in.correction, out);
    } else {
        const ym::RadioEvent& e = *in.event;
        const char* kind = "stream";
        for (const auto& [k, name] : kEventKinds)
            if (k == e.kind) kind = name;
        j = json{{"t", "event"},
                 {"n", in.sequence},
                 {"mono", in.arrival_ns},
                 {"kind", kind},
                 {"plugin", e.plugin_id},
                 {"dev", e.device_key},
                 {"stream", e.stream_id},
                 {"value", e.value},
                 {"time", e.monotonic_ns},
                 {"message", e.message}};
        if (e.kind == ym::RadioEvent::Kind::Stream) {
            const char* name = ym::event_name(e.event);
            j["event"] = name != nullptr ? json(name) : json(e.event);
        }
    }
    return dump(j);
}

std::string cycle_line(const ym::MeasurementCycle& c) {
    json samples = json::array();
    for (const ym::CarrierSample& s : c.samples)
        samples.push_back(json::array({s.latest.device_key, s.latest.provenance.schema_id, role_name(s.latest.role),
                                       s.latest.frequency_hz, s.latest.entry_index, number_or_null(s.latest.level),
                                       s.flags, s.records_in_cycle, s.age_ns}));
    return dump(json{{"t", "cycle"},
                     {"seq", c.sequence},
                     {"mono", c.time.monotonic_ns},
                     {"utc", c.time.utc_ns},
                     {"built", c.built_ns},
                     {"inputs", c.inputs},
                     {"period", c.period_ns},
                     {"flags", c.flags},
                     {"samples", std::move(samples)}});
}

std::string state_line(Declared& d, std::int64_t time_ns, const ym::PipelineState& s,
                       const ym::PipelineStats& stats, std::vector<std::string>& out) {
    json carriers = json::array();
    for (const ym::PipelineState::Carrier& c : s.carriers) {
        if (!c.radio_schema) continue;
        json j;
        put_record(j, source_id(d, c.input, c.radio_schema->schema(), out), c.input);
        j["mono"] = c.arrival_ns;
        j["cal"] = correction_id(d, c.correction, out);
        j["fresh"] = c.fresh;
        j["late"] = c.late;
        j["sync_lost"] = c.sync_lost;
        carriers.push_back(std::move(j));
    }
    json devices = json::array();
    for (const ym::PipelineState::Device& dv : s.devices)
        devices.push_back(json::array({dv.plugin_id, dv.device_key, dv.lost, dv.stopped, dv.data_loss}));
    return dump(json{{"t", "state"},
                     {"mono", time_ns},
                     {"next_end", s.next_end_ns},
                     {"seq", s.sequence},
                     {"inputs", s.inputs},
                     {"stats", stats_json(stats)},
                     {"devices", std::move(devices)},
                     {"carriers", std::move(carriers)}});
}

std::string position_line(const PositionLine& p) {
    return dump(json{{"t", "pos"},
                     {"mono", p.time_ns},
                     {"floor", p.floor_id},
                     {"x", p.x},
                     {"y", p.y},
                     {"xm", p.x_m ? json(*p.x_m) : json(nullptr)},
                     {"ym", p.y_m ? json(*p.y_m) : json(nullptr)},
                     {"kind", p.kind},
                     {"label", p.label}});
}

std::string floor_line(const FloorLine& f) {
    return dump(json{{"t", "floor"}, {"mono", f.time_ns}, {"floor", f.floor_id}, {"name", f.name}});
}

std::string walk_line(const WalkLine& w) {
    auto opt = [](const std::optional<double>& v) { return v ? json(*v) : json(nullptr); };
    return dump(json{{"t", "walk"},
                     {"mono", w.time_ns},
                     {"floor", w.floor_id},
                     {"action", w.action},
                     {"x", opt(w.x)},
                     {"y", opt(w.y)},
                     {"xm", opt(w.x_m)},
                     {"ym", opt(w.y_m)},
                     {"mode", w.mode},
                     {"kind", w.kind},
                     {"seconds", w.seconds},
                     {"label", w.label}});
}

std::string marker_line(const MarkerLine& m) {
    return dump(json{{"t", "marker"},
                     {"mono", m.time_ns},
                     {"n", m.number},
                     {"label", m.label},
                     {"action", marker_action_name(m.action)}});
}

std::string voice_line(const VoiceLine& v) {
    return dump(json{{"t", "voice"},
                     {"mono", v.time_ns},
                     {"n", v.number},
                     {"file", v.file},
                     {"seconds", v.seconds},
                     {"rate", v.sample_rate},
                     {"label", v.label}});
}

std::string end_line(const EndLine& e) {
    return dump(json{{"t", "end"},
                     {"mono", e.time_ns},
                     {"reason", e.reason},
                     {"messages", e.messages},
                     {"inputs", e.inputs}});
}

bool read_line(const json& j, File& file, ReadContext& c, std::string& error) {
    if (!j.is_object()) {
        error = "not an object";
        return false;
    }
    const std::string t = str(j, "t");
    if (t == "source") {
        const std::uint32_t id = u32(j, "id");
        const auto sj = j.find("schema");
        if (id == 0 || sj == j.end()) {
            error = "source without id or schema";
            return false;
        }
        auto schema = ym::parse_schema_json(*sj, "source " + std::to_string(id), error);
        if (!schema) return false;
        Source s;
        s.id = id;
        s.plugin_id = str(j, "plugin");
        s.plugin_version = str(j, "plugin_version");
        s.technology_id = str(j, "technology");
        s.capabilities = u32(j, "capabilities");
        s.schema_index = u32(j, "schema_index");
        auto names = std::make_shared<std::vector<std::string>>();
        for (const auto& f : schema->fields) names->push_back(f.name);
        s.schema = std::make_shared<const ym::RadioSchemaView>(*schema);
        c.sources[id] = file.sources.size();
        c.names[id] = std::move(names);
        file.sources.push_back(std::move(s));
        return true;
    }
    if (t == "cal") {
        ym::LevelCorrection x;
        x.calibrated = boolean(j, "calibrated");
        x.offset_db = dbl(j, "offset_db");
        x.correction_db = dbl(j, "correction_db");
        x.source = str(j, "source");
        x.key = str(j, "key");
        x.calibration_date_unix = i64(j, "date_unix");
        x.expired = boolean(j, "expired");
        x.extrapolated = boolean(j, "extrapolated");
        c.corrections[u32(j, "id")] = std::move(x);
        return true;
    }
    if (t == "rec" || t == "event") {
        InputLine in;
        in.sequence = u64(j, "n");
        in.arrival_ns = i64(j, "mono");
        if (in.sequence == 0) {
            error = "input without number";
            return false;
        }
        if (t == "rec") {
            ym::RadioRecord r;
            if (!read_record(j, file, c, r, error)) return false;
            if (!read_correction_ref(j, c, in.corrected, in.correction, error)) return false;
            in.record = std::move(r);
        } else {
            ym::RadioEvent e;
            const std::string kind = str(j, "kind");
            bool known = false;
            for (const auto& [k, name] : kEventKinds)
                if (kind == name) {
                    e.kind = k;
                    known = true;
                }
            if (!known) {
                error = "unknown event kind '" + kind + "'";
                return false;
            }
            e.plugin_id = str(j, "plugin");
            e.device_key = str(j, "dev");
            e.stream_id = u64(j, "stream");
            e.value = i64(j, "value");
            e.monotonic_ns = i64(j, "time");
            e.message = str(j, "message");
            if (e.kind == ym::RadioEvent::Kind::Stream) {
                const auto ev = j.find("event");
                if (ev != j.end() && ev->is_string()) {
                    const auto v = ym::event_value(ev->get<std::string>());
                    if (!v) {
                        error = "unknown event '" + ev->get<std::string>() + "'";
                        return false;
                    }
                    e.event = *v;
                } else {
                    e.event = u32(j, "event");
                }
            }
            in.event = std::move(e);
        }
        file.inputs.push_back(std::move(in));
        return true;
    }
    if (t == "cycle") {
        CycleLine cl;
        cl.sequence = u64(j, "seq");
        cl.time_ns = i64(j, "mono");
        cl.utc_ns = i64(j, "utc");
        cl.built_ns = i64(j, "built");
        cl.inputs = u64(j, "inputs");
        cl.period_ns = i64(j, "period");
        cl.flags = u32(j, "flags");
        const auto s = j.find("samples");
        if (s == j.end() || !s->is_array()) {
            error = "cycle without samples";
            return false;
        }
        for (const json& x : *s) {
            if (!x.is_array() || x.size() != 9 || !x[0].is_string() || !x[1].is_string() || !x[2].is_string() ||
                !x[3].is_number_unsigned() || !x[4].is_number_unsigned() || !(x[5].is_number() || x[5].is_null()) ||
                !x[6].is_number_unsigned() || !x[7].is_number_unsigned() || !x[8].is_number_integer()) {
                error = "a cycle sample is not [device, schema, role, Hz, entry, level, flags, records, age]";
                return false;
            }
            CycleSample cs;
            cs.device_key = x[0].get<std::string>();
            cs.schema_id = x[1].get<std::string>();
            cs.role = role_of(x[2].get<std::string>());
            cs.frequency_hz = x[3].get<std::uint64_t>();
            cs.entry_index = x[4].get<std::uint64_t>();
            cs.level = x[5].is_null() ? std::nan("") : x[5].get<double>();
            cs.flags = x[6].get<std::uint32_t>();
            cs.records_in_cycle = x[7].get<std::uint32_t>();
            cs.age_ns = x[8].get<std::int64_t>();
            cl.samples.push_back(std::move(cs));
        }
        file.cycles.push_back(std::move(cl));
        return true;
    }
    if (t == "state") {
        StateLine st;
        st.time_ns = i64(j, "mono");
        st.state.next_end_ns = i64(j, "next_end");
        st.state.sequence = u64(j, "seq");
        st.state.inputs = u64(j, "inputs");
        if (const auto s = j.find("stats"); s != j.end() && s->is_object()) st.stats = read_stats(*s);
        if (const auto dv = j.find("devices"); dv != j.end() && dv->is_array())
            for (const json& x : *dv) {
                if (!x.is_array() || x.size() != 5 || !x[0].is_string() || !x[1].is_string() ||
                    !x[2].is_boolean() || !x[3].is_boolean() || !x[4].is_boolean()) {
                    error = "a state device is not [plugin, device, lost, stopped, data_loss]";
                    return false;
                }
                st.state.devices.push_back({x[0].get<std::string>(), x[1].get<std::string>(), x[2].get<bool>(),
                                            x[3].get<bool>(), x[4].get<bool>()});
            }
        const auto cs = j.find("carriers");
        if (cs == j.end() || !cs->is_array()) {
            error = "state without carriers";
            return false;
        }
        for (const json& x : *cs) {
            if (!x.is_object()) {
                error = "a state carrier is not an object";
                return false;
            }
            ym::PipelineState::Carrier car;
            if (!read_record(x, file, c, car.input, error)) return false;
            car.radio_schema = file.sources[c.sources.at(u32(x, "src"))].schema;
            car.input.radio_schema = nullptr;
            bool corrected = false;
            if (!read_correction_ref(x, c, corrected, car.correction, error)) return false;
            car.arrival_ns = i64(x, "mono");
            car.fresh = u32(x, "fresh");
            car.late = boolean(x, "late");
            car.sync_lost = boolean(x, "sync_lost");
            st.state.carriers.push_back(std::move(car));
        }
        file.states.push_back(std::move(st));
        return true;
    }
    if (t == "pos") {
        PositionLine p;
        p.time_ns = i64(j, "mono");
        p.floor_id = str(j, "floor");
        const auto x = j.find("x");
        const auto y = j.find("y");
        if (x == j.end() || y == j.end() || !x->is_number() || !y->is_number()) {
            error = "position without x and y";
            return false;
        }
        p.x = x->get<double>();
        p.y = y->get<double>();
        if (const auto xm = j.find("xm"); xm != j.end() && xm->is_number()) p.x_m = xm->get<double>();
        if (const auto ym_ = j.find("ym"); ym_ != j.end() && ym_->is_number()) p.y_m = ym_->get<double>();
        p.kind = str(j, "kind");
        p.label = str(j, "label");
        file.positions.push_back(std::move(p));
        return true;
    }
    if (t == "walk") {
        WalkLine w;
        w.time_ns = i64(j, "mono");
        w.floor_id = str(j, "floor");
        w.action = str(j, "action");
        if (w.action.empty()) {
            error = "walk line without an action";
            return false;
        }
        auto opt = [&](const char* k) -> std::optional<double> {
            const auto it = j.find(k);
            if (it == j.end() || !it->is_number()) return std::nullopt;
            return it->get<double>();
        };
        w.x = opt("x");
        w.y = opt("y");
        w.x_m = opt("xm");
        w.y_m = opt("ym");
        if (w.x.has_value() != w.y.has_value()) {
            error = "walk line with only one of x and y";
            return false;
        }
        w.mode = str(j, "mode");
        w.kind = str(j, "kind");
        w.seconds = opt("seconds").value_or(0.0);
        w.label = str(j, "label");
        file.walks.push_back(std::move(w));
        return true;
    }
    if (t == "floor") {
        file.floors.push_back({i64(j, "mono"), str(j, "floor"), str(j, "name")});
        return true;
    }
    if (t == "marker") {
        MarkerLine m;
        m.time_ns = i64(j, "mono");
        m.number = u32(j, "n");
        m.label = str(j, "label");
        const std::string a = str(j, "action");
        m.action = a == "new_file" ? MarkerAction::NewFile
                   : a == "new_file_pause" ? MarkerAction::NewFilePause
                                           : MarkerAction::Mark;
        file.markers.push_back(std::move(m));
        return true;
    }
    if (t == "voice") {
        VoiceLine v;
        v.time_ns = i64(j, "mono");
        v.number = u32(j, "n");
        v.file = str(j, "file");
        if (!valid_voice_file_name(v.file)) {
            error = "voice line with a file name that is not a plain .wav name";
            return false;
        }
        if (const auto it = j.find("seconds"); it != j.end() && it->is_number()) v.seconds = it->get<double>();
        if (!(v.seconds >= 0.0 && v.seconds <= 3600.0)) {
            error = "voice line with a length outside 0..3600 s";
            return false;
        }
        v.sample_rate = u32(j, "rate");
        v.label = str(j, "label");
        file.voices.push_back(std::move(v));
        return true;
    }
    if (t == "end") {
        file.end = EndLine{i64(j, "mono"), str(j, "reason"), u64(j, "messages"), u64(j, "inputs")};
        return true;
    }
    if (t == "header") {
        error = "a second header";
        return false;
    }
    ++file.unknown_lines;
    return true;
}

}  // namespace yantra::recording::codec
