// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Self-interference warning logic (self_interference.h, NFR-07). The
// monitor's on/off rule and the advice text are copied from yantra-v2
// 68ea20a: core/src/measure/self_interference.cpp, adapted for cycles
// (no spectrum in Yantra, PLG-27).

#include "yantra/core/measure/self_interference.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace yantra::measure {

namespace {

constexpr const char* kSnrField = "snr_db";  // radio API YR_FIELD_SNR_DB

bool fresh(const CarrierSample& s) {
    return s.records_in_cycle > 0 && (s.flags & (sample_flag::kStale | sample_flag::kHeld)) == 0;
}

std::optional<double> snr_of(const MappedRecord& m) {
    for (const QualityValue& q : m.quality)
        if (q.field == kSnrField && std::isfinite(q.value)) return q.value;
    if (m.schema == nullptr) return std::nullopt;
    const auto i = m.schema->field_index(kSnrField);
    if (!i || *i >= m.record.values.size()) return std::nullopt;
    if (const auto* d = std::get_if<double>(&m.record.values[*i]); d && std::isfinite(*d)) return *d;
    return std::nullopt;
}

}  // namespace

double thermal_noise_dbm(double bandwidth_hz, double noise_figure_db) {
    return -174.0 + 10.0 * std::log10(std::max(bandwidth_hz, 1.0)) + noise_figure_db;
}

namespace {

// The least level - SNR (self_interference.h) and how many samples gave
// one, in all and below the SNR limit.
std::optional<double> least_noise(const MeasurementCycle& cycle, const SelfInterferenceOptions& o, std::size_t& samples,
                                  std::size_t& below_limit) {
    std::optional<double> least;
    samples = below_limit = 0;
    for (const CarrierSample& s : cycle.samples) {
        const std::uint32_t flags = s.flags | s.latest.flags;
        if (!fresh(s) || (flags & (sample_flag::kNoSync | sample_flag::kUncalibrated)) != 0) continue;
        if (!std::isfinite(s.latest.level)) continue;
        const auto snr = snr_of(s.latest);
        if (!snr) continue;
        ++samples;
        if (*snr < o.snr_limit_db) ++below_limit;
        const double n = s.latest.level - *snr;
        if (!least || n < *least) least = n;
    }
    if (samples == 0 || samples < o.min_noise_samples) return std::nullopt;
    return least;
}

}  // namespace

std::optional<double> cycle_noise_dbm(const MeasurementCycle& cycle, const SelfInterferenceOptions& o,
                                      std::size_t* samples) {
    std::size_t n = 0, below = 0;
    const std::optional<double> noise = least_noise(cycle, o, n, below);
    if (samples != nullptr) *samples = n;
    return noise;
}

InterferenceCheck check_self_interference(const MeasurementCycle& cycle, std::optional<double> reference_dbm,
                                          const SelfInterferenceOptions& o) {
    InterferenceCheck c;
    c.noise_dbm = least_noise(cycle, o, c.noise_samples, c.noise_limited_samples);
    c.thermal_reference = !reference_dbm.has_value();
    c.reference_dbm = reference_dbm ? *reference_dbm : thermal_noise_dbm(o.bandwidth_hz, o.noise_figure_db);
    if (c.noise_dbm) {
        c.rise_db = *c.noise_dbm - c.reference_dbm;
        // Strong carriers at the receiver's SNR limit only bound the noise.
        c.noise_raised = *c.rise_db >= o.rise_db && c.noise_limited_samples > 0;
    }
    for (const CarrierSample& s : cycle.samples) {
        if (!fresh(s) || ((s.flags | s.latest.flags) & sample_flag::kNoSync) == 0) continue;
        const double level = s.latest.level;
        if (!std::isfinite(level) || level < c.reference_dbm + o.unsynced_above_db) continue;  // the noise
        bool skirt = false;
        for (const CarrierSample& t : cycle.samples) {
            if (!fresh(t) || ((t.flags | t.latest.flags) & sample_flag::kNoSync) != 0 || !std::isfinite(t.latest.level))
                continue;
            const double df = std::fabs(static_cast<double>(t.latest.frequency_hz) - static_cast<double>(s.latest.frequency_hz));
            if (df <= o.skirt_hz && t.latest.level - level >= o.skirt_db) skirt = true;
        }
        if (!skirt) ++c.unsynced;
    }
    c.many_unsynced = o.min_unsynced > 0 && c.unsynced >= o.min_unsynced;
    return c;
}

std::string self_interference_text(const InterferenceCheck& c) {
    std::string what;
    char b[160];
    if (c.noise_raised && c.rise_db) {
        std::snprintf(b, sizeof b, "the noise floor is %.1f dB above %s", *c.rise_db,
                      c.thermal_reference ? "the receiver's thermal floor" : "the reference taken away from the PC");
        what = b;
    }
    if (c.many_unsynced) {
        std::snprintf(b, sizeof b, "%zu carriers have a level but no sync", c.unsynced);
        what += (what.empty() ? "" : "; ") + std::string(b);
    }
    if (what.empty()) return {};
    return "Possible self-interference: " + what +
           ". Keep the antenna away from the laptop, its USB ports and cables (a USB extension cable helps) "
           "and check again.";
}

SelfInterferenceMonitor::SelfInterferenceMonitor(SelfInterferenceOptions options) : options_(options) {}

const SelfInterferenceWarning& SelfInterferenceMonitor::on_cycle(const MeasurementCycle& cycle) {
    warning_.last = check_self_interference(cycle, reference_, options_);
    if (warning_.last.finding()) {
        ++with_finding_;
        without_ = 0;
        if (with_finding_ >= options_.on_cycles) warning_.active = true;
    } else {
        ++without_;
        with_finding_ = 0;
        if (without_ >= options_.off_cycles) warning_.active = false;
    }
    if (warning_.active && warning_.last.finding()) warning_.text = self_interference_text(warning_.last);
    if (!warning_.active) warning_.text.clear();
    return warning_;
}

void SelfInterferenceMonitor::reset() {
    with_finding_ = 0;
    without_ = 0;
    warning_ = SelfInterferenceWarning{};
}

}  // namespace yantra::measure
