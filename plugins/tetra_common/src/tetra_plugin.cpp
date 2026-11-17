// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared glue of the TETRA radio plugins (see tetra_plugin.hpp).
#include "yantra/plugins/tetra_plugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace yantra::plugins {

using radios::Record;
using radios::RecordFiller;
using radios::Value;

// --- schemas ------------------------------------------------------------------------

TetraSchemaTable::TetraSchemaTable(const radios::TetraSchemaOptions& options)
    : options_(options), defs_(radios::tetra_schemas(options))
{
    for (const radios::SchemaDef& d : defs_) {
        views_.push_back(std::make_unique<radios::SchemaView>(d));
        ptrs_.push_back(&views_.back()->schema_c);
    }
}

// --- plans --------------------------------------------------------------------------

yr_status plan_setup(const yr_plan& plan, const std::vector<uint64_t>& carriers, double first_hz,
                     double last_hz, uint32_t max_carriers, bool scan, PlanSetup& out,
                     std::string& why)
{
    out = PlanSetup{};
    out.type = plan.type;
    auto inside = [&](double hz) { return hz >= first_hz && hz <= last_hz; };
    auto span_text = [&] {
        return radio::number_text(first_hz / 1e6) + " to " + radio::number_text(last_hz / 1e6) +
               " MHz";
    };
    switch (plan.type) {
    case YR_PLAN_SERVING:
        if (plan.serving_hz != 0) {
            const double hz = static_cast<double>(plan.serving_hz);
            if (!inside(hz)) {
                why = "the serving carrier " + radio::number_text(hz / 1e6) +
                      " MHz is outside the span measured now (" + span_text() + ")";
                return YR_ERR_INVALID_ARGUMENT;
            }
            out.serving_hz = hz;
            out.carriers.push_back(hz);
        }
        out.search = true;
        return YR_OK;
    case YR_PLAN_CARRIERS:
        if (carriers.empty()) {
            why = "a carrier plan needs at least one carrier";
            return YR_ERR_INVALID_ARGUMENT;
        }
        if (carriers.size() > max_carriers) {
            why = "at most " + std::to_string(max_carriers) + " carriers can be measured at once";
            return YR_ERR_INVALID_ARGUMENT;
        }
        for (uint64_t c : carriers) {
            const double hz = static_cast<double>(c);
            if (!inside(hz)) {
                why = "carrier " + radio::number_text(hz / 1e6) +
                      " MHz is outside the span measured now (" + span_text() + ")";
                return YR_ERR_INVALID_ARGUMENT;
            }
            out.carriers.push_back(hz);
        }
        out.search = false;
        return YR_OK;
    case YR_PLAN_SCAN:
        if (!scan) {
            why = "this device cannot scan a band; use a serving or carrier plan";
            return YR_ERR_UNSUPPORTED;
        }
        out.search = true;
        return YR_OK;
    default:
        why = "unknown plan type " + std::to_string(plan.type);
        return YR_ERR_INVALID_ARGUMENT;
    }
}

void apply_plan(const PlanSetup& plan, receiver::tetra::EngineConfig& config)
{
    config.carriers = plan.carriers;
    config.serving_hz = plan.serving_hz;
    config.search = plan.search;
    config.scan_records = plan.type == YR_PLAN_SCAN;
}

// --- records ------------------------------------------------------------------------

bool push_record(const yr_host_api& host, yr_stream* sink, const Record& record,
                 SinkCounters& counters)
{
    const radios::RecordView view(record);
    // The sink never blocks: when the host is busy it drops and counts the
    // record and still returns OK; the plugin carries on (RAD-24).
    if (host.push_record(sink, &view.record) == YR_OK) {
        ++counters.delivered;
        return true;
    }
    ++counters.refused;
    return false;
}

void push_output(const yr_host_api& host, yr_stream* sink,
                 const receiver::tetra::EngineOutput& out, SinkCounters& counters)
{
    for (const Record& r : out.records)
        push_record(host, sink, r, counters);
    for (const radios::Event& e : out.events)
        host.stream_event(sink, e.event, e.value);
}

// --- spectra ------------------------------------------------------------------------

SpectrumPlan spectrum_plan(const yr_host_api& host, const yr_plan& plan)
{
    SpectrumPlan out;
    out.on = (plan.flags & YR_PLAN_SPECTRUM) != 0 && yr_host_spectrum_sink(&host) != nullptr;
    radio::spectrum_band(plan, out.first_hz, out.last_hz);
    return out;
}

bool clip_band(double first, double last, double lo, double hi, double& out_first,
               double& out_last)
{
    if (!(lo < hi))
        return false;
    out_first = std::max(first, lo);
    out_last = std::min(last, hi);
    if (!(out_first < out_last)) {
        out_first = lo;
        out_last = hi;
    }
    return true;
}

double spectrum_bin_width(double span_hz)
{
    double w = 25e3 / 64.0;
    while (span_hz / w > static_cast<double>(YR_SPECTRUM_MAX_BINS))
        w *= 2.0;
    return w;
}

bool push_spectrum(const yr_host_api& host, yr_stream* sink, const SpectrumOut& s)
{
    const yr_push_spectrum_fn push = yr_host_spectrum_sink(&host);
    if (push == nullptr || sink == nullptr || s.power_db.empty())
        return false;
    yr_spectrum c{};
    c.struct_size = sizeof c;
    c.flags = s.flags & YR_SPECTRUM_FLAGS_MASK;
    c.time = s.time;
    c.start_hz = s.start_hz;
    c.bin_width_hz = s.bin_width_hz;
    c.bin_count = static_cast<uint32_t>(std::min<std::size_t>(s.power_db.size(), YR_SPECTRUM_MAX_BINS));
    c.stop_hz = s.start_hz + c.bin_count * s.bin_width_hz;
    c.power_db = s.power_db.data();
    // Like push_record: the host drops and counts when it is busy (RAD-24).
    return push(sink, &c) == YR_OK;
}

// --- C1 -----------------------------------------------------------------------------

bool set_c1(Record& record, const RecordFiller& cell, double level_to_dbm_db,
            double ms_max_power_dbm)
{
    const radios::SchemaDef& s = cell.schema();
    const int level = s.field_index(YR_FIELD_LEVEL);
    const int rxlev = s.field_index("rxlev_access_min_dbm");
    const int txpwr = s.field_index("ms_txpwr_max_cell_dbm");
    if (level < 0 || rxlev < 0 || txpwr < 0 || (record.present & YR_GROUP_BROADCAST) == 0)
        return false;
    const Value& l = record.values.at(static_cast<std::size_t>(level));
    const Value& a = record.values.at(static_cast<std::size_t>(rxlev));
    const Value& p = record.values.at(static_cast<std::size_t>(txpwr));
    if (l.type != YR_TYPE_DOUBLE || a.type != YR_TYPE_INT || p.type != YR_TYPE_INT)
        return false;
    cell.set(record, YR_FIELD_C1_DB,
             radios::value_double(radios::tetra_c1_db(l.f64 + level_to_dbm_db,
                                                      static_cast<double>(a.i64),
                                                      static_cast<double>(p.i64),
                                                      ms_max_power_dbm)));
    return true;
}

yr_setting_def ms_max_power_setting(const char* group, int32_t order)
{
    yr_setting_def s = make_setting(
        kMsMaxPowerKey, YR_TYPE_DOUBLE, "MS maximum power (C1)", "dBm", group,
        "Maximum transmit power of the terminal assumed for the path loss criterion C1 "
        "(EN 300 392-2 clause 23.3): C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - "
        "this). 30 dBm is power class 4 (1 W, a typical hand-portable); 40 dBm class 2 (a "
        "vehicle radio).",
        order);
    s.minimum = 15.0;
    s.maximum = 45.0;
    s.step = 0.25;
    s.default_value = yr_value_double(radios::kTetraC1MsMaxPowerDbm);
    return s;
}

// --- settings as data ---------------------------------------------------------------

yr_setting_def make_setting(const char* key, uint32_t type, const char* label, const char* unit,
                            const char* group, const char* help, int32_t order)
{
    yr_setting_def s{};
    s.struct_size = sizeof s;
    s.type = type;
    s.key = key;
    s.label = label;
    s.unit = unit;
    s.group = group;
    s.help = help;
    s.minimum = NAN;
    s.maximum = NAN;
    s.step = 0.0;
    s.default_value = yr_value_none();
    s.sort_order = order;
    return s;
}

yr_setting_def make_sensor(const char* key, uint32_t type, const char* label, const char* unit,
                           const char* group, const char* help, int32_t order)
{
    yr_setting_def s = make_setting(key, type, label, unit, group, help, order);
    s.flags = YR_SETTING_READ_ONLY;
    return s;
}

const yr_setting_def* find_setting(const std::vector<yr_setting_def>& defs, const char* key)
{
    if (key == nullptr)
        return nullptr;
    for (const yr_setting_def& s : defs)
        if (std::strcmp(s.key, key) == 0)
            return &s;
    return nullptr;
}

double number_of(const yr_value& v)
{
    switch (v.type) {
    case YR_TYPE_DOUBLE: return v.u.f64;
    case YR_TYPE_INT:
    case YR_TYPE_ENUM:
    case YR_TYPE_BOOL: return static_cast<double>(v.u.i64);
    case YR_TYPE_UINT: return static_cast<double>(v.u.u64);
    default: return NAN;
    }
}

double on_raster(const yr_setting_def& def, double x)
{
    if (def.step > 0.0) {
        const double base = std::isfinite(def.minimum) ? def.minimum : 0.0;
        x = base + std::round((x - base) / def.step) * def.step;
    }
    if (std::isfinite(def.minimum))
        x = std::max(x, def.minimum);
    if (std::isfinite(def.maximum))
        x = std::min(x, def.maximum);
    return x;
}

yr_status string_value(const std::string& text, yr_value* out, char* buffer, uint32_t size)
{
    const yr_status st = radio::copy_text(text, buffer, size);
    if (st != YR_OK)
        return st;
    *out = yr_value_string(buffer);
    return YR_OK;
}

yr_selftest_item selftest_item(uint32_t result, const char* name, const char* label,
                               const char* detail, const char* remedy)
{
    return {sizeof(yr_selftest_item), result, name, label, detail, remedy};
}

// --- GSMTAP -------------------------------------------------------------------------

void add_gsmtap_settings(std::vector<yr_setting_def>& defs, const char* group,
                         int32_t first_order)
{
    using namespace receiver::tetra;
    yr_setting_def on = make_setting(
        kGsmtapEnabledKey, YR_TYPE_BOOL, "GSMTAP output", "", group,
        "Sends a copy of the decoded broadcast blocks as GSMTAP over UDP, for Wireshark "
        "(debugging only; never part of the records).",
        first_order);
    on.flags = YR_SETTING_ADVANCED | YR_SETTING_RESTART;
    on.default_value = yr_value_bool(0);
    defs.push_back(on);
    yr_setting_def host = make_setting(
        kGsmtapHostKey, YR_TYPE_STRING, "GSMTAP address", "", group,
        "Numeric IPv4 or IPv6 address the GSMTAP datagrams go to (127.0.0.1: this computer). "
        "Another text turns the output off with a warning in the log.",
        first_order + 1);
    host.flags = YR_SETTING_ADVANCED | YR_SETTING_RESTART;
    host.default_value = yr_value_string("127.0.0.1");
    defs.push_back(host);
    yr_setting_def port = make_setting(kGsmtapPortKey, YR_TYPE_UINT, "GSMTAP port", "", group,
                                       "UDP port of the GSMTAP datagrams (4729 is the "
                                       "GSMTAP standard port).",
                                       first_order + 2);
    port.flags = YR_SETTING_ADVANCED | YR_SETTING_RESTART;
    port.minimum = 1.0;
    port.maximum = 65535.0;
    port.step = 1.0;
    port.default_value = yr_value_uint(kGsmtapDefaultPort);
    defs.push_back(port);
}

std::optional<yr_status> gsmtap_get(const GsmtapSettings& s, const char* key, yr_value* out,
                                    char* text, uint32_t text_size)
{
    using namespace receiver::tetra;
    if (std::strcmp(key, kGsmtapEnabledKey) == 0) {
        *out = yr_value_bool(s.enabled ? 1 : 0);
        return YR_OK;
    }
    if (std::strcmp(key, kGsmtapHostKey) == 0)
        return string_value(s.host, out, text, text_size);
    if (std::strcmp(key, kGsmtapPortKey) == 0) {
        *out = yr_value_uint(s.port);
        return YR_OK;
    }
    return std::nullopt;
}

bool gsmtap_set(GsmtapSettings& s, const char* key, const yr_value& value)
{
    using namespace receiver::tetra;
    if (std::strcmp(key, kGsmtapEnabledKey) == 0)
        s.enabled = value.u.i64 != 0;
    else if (std::strcmp(key, kGsmtapHostKey) == 0)
        s.host = value.u.str != nullptr ? value.u.str : "";
    else if (std::strcmp(key, kGsmtapPortKey) == 0)
        s.port = value.u.u64;
    else
        return false;
    return true;
}

receiver::tetra::GsmtapOptions gsmtap_options(const GsmtapSettings& s, std::string& warning)
{
    using namespace receiver::tetra;
    warning.clear();
    if (!s.enabled)
        return {};
    const std::string port = std::to_string(s.port);
    return receiver::tetra::gsmtap_options(
        [&](const char* key) -> std::optional<std::string> {
            if (std::strcmp(key, kGsmtapEnabledKey) == 0)
                return std::string("1");
            if (std::strcmp(key, kGsmtapHostKey) == 0)
                return s.host;
            if (std::strcmp(key, kGsmtapPortKey) == 0)
                return port;
            return std::nullopt;
        },
        warning);
}

} // namespace yantra::plugins
