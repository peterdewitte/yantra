// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/calibration/wizard.cpp, adapted
// for RF-02 (see wizard.h); the IQ tone measurement is left out (RAD-26).

#include "yantra/core/calibration/wizard.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace yantra::calibration {

// ---- plan ------------------------------------------------------------------------

std::string Plan::validate() const {
    if (frequencies_hz.empty()) return "Choose at least one frequency.";
    if (levels_dbm.empty()) return "Choose at least one generator level.";
    for (double f : frequencies_hz)
        if (!(f > 0.0) || !std::isfinite(f)) return "Frequencies must be positive.";
    for (double l : levels_dbm)
        if (!std::isfinite(l) || l < -150.0 || l > 30.0) return "Generator levels must be between -150 and 30 dBm.";
    if (valid_days < 1) return "The validity must be at least one day.";
    if (method != kMethodGenerator && method != kMethodReference && method != kMethodPractice)
        return "Unknown calibration method.";
    if (simulated != (method == kMethodPractice))
        return "A practice calibration is made with a simulated radio, and only there.";
    return {};
}

// ---- averager --------------------------------------------------------------------

void LevelAverager::add(double level_dbfs, bool no_sync, bool overload, double snr_db) {
    if (overload) overload_ = true;
    if (no_sync || overload || !std::isfinite(level_dbfs)) return;
    if (skipped_ < skip_) {
        ++skipped_;
        return;
    }
    if (n_ == 0) {
        lo_ = hi_ = level_dbfs;
    } else {
        lo_ = std::min(lo_, level_dbfs);
        hi_ = std::max(hi_, level_dbfs);
    }
    sum_ += level_dbfs;
    ++n_;
    if (std::isfinite(snr_db)) {
        sum_snr_ += snr_db;
        ++n_snr_;
    }
}

double LevelAverager::mean_dbfs() const noexcept { return n_ == 0 ? kNaN : sum_ / static_cast<double>(n_); }
double LevelAverager::mean_snr_db() const noexcept {
    return n_snr_ == 0 ? kNaN : sum_snr_ / static_cast<double>(n_snr_);
}
double LevelAverager::spread_db() const noexcept { return n_ == 0 ? kNaN : hi_ - lo_; }

void LevelAverager::reset() {
    const std::size_t skip = skip_;
    *this = LevelAverager(skip);
}

// ---- wizard ----------------------------------------------------------------------

Wizard::Wizard(Key key, Plan plan, std::string radio_plugin_id)
    : key_(std::move(key)), plan_(std::move(plan)), radio_plugin_id_(std::move(radio_plugin_id)) {
    const std::string problem = plan_.validate();
    if (!problem.empty()) throw std::invalid_argument(problem);
}

Target Wizard::target() const {
    Target t;
    t.count = count();
    t.index = std::min(next_, t.count);
    if (next_ < t.count) {
        const std::size_t levels = plan_.levels_dbm.size();
        t.frequency_hz = plan_.frequencies_hz[next_ / levels];
        t.level_dbm = plan_.levels_dbm[next_ % levels];
    }
    return t;
}

const Reading& Wizard::record(double measured_dbfs, double snr_db, bool overload) {
    const Target t = target();
    Reading r;
    r.frequency_hz = t.frequency_hz;
    r.level_dbm = t.level_dbm;
    r.measured_dbfs = measured_dbfs;
    r.snr_db = snr_db;
    if (done()) {
        r.problem = "All points are measured.";
    } else if (overload) {
        r.problem = "The radio reports overload: lower the generator level or the gain.";
    } else if (!std::isfinite(measured_dbfs)) {
        r.problem = "The radio measured no level: check the generator frequency, level and cable.";
    } else if (measured_dbfs > plan_.max_dbfs) {
        r.problem = "The level is too close to full scale: lower the generator level or the gain.";
    } else if (std::isfinite(snr_db) && snr_db < plan_.min_snr_db) {
        r.problem = "The signal is too weak above the noise: raise the generator level.";
    } else {
        r.accepted = true;
        ++next_;
    }
    readings_.push_back(r);
    return readings_.back();
}

const Reading& Wizard::record(const LevelAverager& readings) {
    return record(readings.mean_dbfs(), readings.mean_snr_db(), readings.any_overload());
}

void Wizard::skip() {
    if (!done()) ++next_;
}

void Wizard::restart() {
    next_ = 0;
    readings_.clear();
}

std::size_t Wizard::accepted() const {
    return static_cast<std::size_t>(
        std::count_if(readings_.begin(), readings_.end(), [](const Reading& r) { return r.accepted; }));
}

Entry Wizard::result(std::int64_t created_unix) const {
    Entry e;
    e.key = key_;
    e.created_unix = created_unix;
    e.valid_days = plan_.valid_days;
    e.method = plan_.method;
    e.simulated = plan_.simulated;
    e.radio_plugin_id = radio_plugin_id_;
    struct Acc {
        double sum = 0.0, lo = INFINITY, hi = -INFINITY, ref = 0.0, meas = 0.0;
        std::size_t n = 0;
    };
    std::map<double, Acc> per_freq;
    for (const Reading& r : readings_) {
        if (!r.accepted) continue;
        const double offset = r.level_dbm - r.measured_dbfs;
        Acc& a = per_freq[r.frequency_hz];
        a.sum += offset;
        a.lo = std::min(a.lo, offset);
        a.hi = std::max(a.hi, offset);
        a.ref = r.level_dbm;
        a.meas = r.measured_dbfs;
        ++a.n;
    }
    for (const auto& [f, a] : per_freq) {
        Point p;
        p.frequency_hz = f;
        p.offset_db = a.sum / static_cast<double>(a.n);
        p.spread_db = a.hi - a.lo;
        if (a.n == 1) {
            p.reference_dbm = a.ref;
            p.measured_dbfs = a.meas;
        }
        e.points.push_back(p);
    }
    e.normalize();
    return e;
}

// ---- verify and ppm ----------------------------------------------------------------

double verify_error_db(const Lookup& lookup, double measured_dbfs, double reference_dbm) {
    if (!lookup.usable() || !std::isfinite(measured_dbfs) || !std::isfinite(reference_dbm)) return kNaN;
    return measured_dbfs + lookup.offset_db - reference_dbm;
}

double ppm_from_carrier(double true_hz, double measured_hz) {
    if (!(true_hz > 0.0) || !std::isfinite(true_hz) || !std::isfinite(measured_hz)) return kNaN;
    // A fast oscillator (+e ppm) tunes too high, so the carrier reads low.
    return (true_hz - measured_hz) / true_hz * 1e6;
}

}  // namespace yantra::calibration
