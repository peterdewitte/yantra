// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/measure/sample.h"

#include <cmath>
#include <sstream>

namespace yantra::measure {

std::string sample_flags_text(std::uint32_t flags) {
    static const std::pair<std::uint32_t, const char*> names[] = {
        {sample_flag::kNoSync, "no_sync"},
        {sample_flag::kOverload, "overload"},
        {sample_flag::kUncalibrated, "uncalibrated"},
        {sample_flag::kSimulated, "simulated"},
        {sample_flag::kHeld, "held"},
        {sample_flag::kStale, "stale"},
        {sample_flag::kLate, "late"},
        {sample_flag::kDeviceLost, "device_lost"},
        {sample_flag::kDataLoss, "data_loss"},
        {sample_flag::kC1Estimated, "c1_estimated"},
    };
    std::string out;
    for (const auto& [bit, name] : names) {
        if ((flags & bit) == 0) continue;
        if (!out.empty()) out += ',';
        out += name;
    }
    return out;
}

const char* c1_source_text(C1Source source) noexcept {
    switch (source) {
    case C1Source::None: return "";
    case C1Source::Radio: return "radio";
    case C1Source::Estimated: return "estimated";
    }
    return "";
}

const char* role_text(CarrierRole role) noexcept {
    switch (role) {
    case CarrierRole::Serving: return "serving";
    case CarrierRole::Neighbour: return "neighbour";
    case CarrierRole::Other: return "other";
    case CarrierRole::ScanChannel: return "scan";
    case CarrierRole::NeighbourEntry: return "neighbour_entry";
    }
    return "other";
}

const CarrierSample* MeasurementCycle::serving(const std::string& device_key) const noexcept {
    for (const CarrierSample& s : samples) {
        if (s.latest.role != CarrierRole::Serving) continue;
        if (device_key.empty() || s.latest.device_key == device_key) return &s;
    }
    return nullptr;
}

std::vector<const CarrierSample*> MeasurementCycle::with_role(CarrierRole role) const {
    std::vector<const CarrierSample*> out;
    for (const CarrierSample& s : samples)
        if (s.latest.role == role) out.push_back(&s);
    return out;
}

namespace {

bool same_double(double a, double b) {
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

bool same_value(const plugin::Value& a, const plugin::Value& b) {
    if (a.index() != b.index()) return false;
    if (const double* d = std::get_if<double>(&a)) return same_double(*d, std::get<double>(b));
    return a == b;
}

std::string num(double v) {
    std::ostringstream o;
    o.precision(17);
    o << v;
    return o.str();
}

std::string record_difference(const MappedRecord& a, const MappedRecord& b) {
    if (!(a.provenance == b.provenance)) return "provenance";
    if (a.generic != b.generic) return "generic";
    if (a.radio_plugin_id != b.radio_plugin_id || a.radio_plugin_version != b.radio_plugin_version)
        return "radio plugin";
    if (a.device_key != b.device_key) return "device " + a.device_key + " != " + b.device_key;
    if (a.stream_id != b.stream_id) return "stream";
    if ((a.schema == nullptr) != (b.schema == nullptr) || (a.schema && !(*a.schema == *b.schema)))
        return "schema";
    if (a.schema_index != b.schema_index) return "schema index";
    const plugin::Record& ra = a.record;
    const plugin::Record& rb = b.record;
    if (ra.schema_id != rb.schema_id || ra.schema_version != rb.schema_version) return "record schema";
    if (ra.time.monotonic_ns != rb.time.monotonic_ns || ra.time.utc_ns != rb.time.utc_ns ||
        ra.time.utc_source != rb.time.utc_source)
        return "record time";
    if (ra.flags != rb.flags) return "record flags";
    if (ra.values.size() != rb.values.size()) return "value count";
    for (std::size_t i = 0; i < ra.values.size(); ++i)
        if (!same_value(ra.values[i], rb.values[i])) return "value " + std::to_string(i);
    if (a.role != b.role) return "role";
    if (a.frequency_hz != b.frequency_hz) return "frequency";
    if (a.entry_index != b.entry_index) return "entry index";
    if (!same_double(a.level, b.level)) return "level " + num(a.level) + " != " + num(b.level);
    if (!same_double(a.radio_level, b.radio_level)) return "radio level";
    if (a.radio_dbfs != b.radio_dbfs) return "radio dBFS";
    if (!(a.correction == b.correction)) return "correction";
    if (a.quality.size() != b.quality.size()) return "quality count";
    for (std::size_t i = 0; i < a.quality.size(); ++i)
        if (a.quality[i].field != b.quality[i].field || a.quality[i].unit != b.quality[i].unit ||
            !same_double(a.quality[i].value, b.quality[i].value))
            return "quality " + a.quality[i].field;
    if (a.c1_source != b.c1_source) return "C1 source";
    if (a.flags != b.flags) return "flags " + sample_flags_text(a.flags) + " != " + sample_flags_text(b.flags);
    if (a.arrival_ns != b.arrival_ns) return "arrival " + std::to_string(a.arrival_ns) + " != " + std::to_string(b.arrival_ns);
    return {};
}

}  // namespace

std::string cycle_difference(const MeasurementCycle& a, const MeasurementCycle& b) {
    const std::string at = "cycle " + std::to_string(a.sequence) + ": ";
    if (a.sequence != b.sequence)
        return at + "sequence " + std::to_string(a.sequence) + " != " + std::to_string(b.sequence);
    if (a.time.monotonic_ns != b.time.monotonic_ns) return at + "time";
    if (a.time.utc_ns != b.time.utc_ns || a.time.utc_source != b.time.utc_source) return at + "UTC";
    if (a.period_ns != b.period_ns) return at + "period";
    if (a.built_ns != b.built_ns) return at + "tick time";
    if (a.inputs != b.inputs)
        return at + "inputs " + std::to_string(a.inputs) + " != " + std::to_string(b.inputs);
    if (a.flags != b.flags) return at + "flags " + sample_flags_text(a.flags) + " != " + sample_flags_text(b.flags);
    if (a.samples.size() != b.samples.size())
        return at + "samples " + std::to_string(a.samples.size()) + " != " + std::to_string(b.samples.size());
    for (std::size_t i = 0; i < a.samples.size(); ++i) {
        const CarrierSample& x = a.samples[i];
        const CarrierSample& y = b.samples[i];
        const std::string s = at + "sample " + std::to_string(i) + " (" +
                              std::to_string(x.latest.frequency_hz) + " Hz): ";
        if (x.age_ns != y.age_ns) return s + "age";
        if (x.records_in_cycle != y.records_in_cycle) return s + "records in cycle";
        if (x.flags != y.flags) return s + "flags " + sample_flags_text(x.flags) + " != " + sample_flags_text(y.flags);
        if (const std::string d = record_difference(x.latest, y.latest); !d.empty()) return s + d;
    }
    return {};
}

}  // namespace yantra::measure
