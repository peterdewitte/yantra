// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The data side of the level calibration wizard (RF-02, HW-01b, PLG-16
// calibration window; the window itself comes with the UI phases): a plan
// of frequencies and generator levels for one calibration key (one gain
// combination; another combination is another run), the next point to
// set on the signal generator (or to read on the reference radio), the
// radio's reading of it, and the resulting table entry. Also the verify
// mode (a reading through the table against the generator) and the
// frequency (ppm) calibration from a known carrier.
//
// No IQ here (RAD-26): the reading is the level the radio plugin itself
// measured and delivered in its records (dBFS), averaged over several
// records with LevelAverager. With the simulator's simulated signal
// generator (RAD-61 practice calibration) the same steps recover the
// simulator's true offset (TST-30).
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/calibration/
// wizard.h, core/src/calibration/wizard.cpp, adapted for RF-02 of spec
// v0.2 (readings from radio records instead of a tone found in IQ
// samples; LevelAverager; the reference-radio method).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "yantra/core/calibration/table.h"

namespace yantra::calibration {

struct Plan {
    std::vector<double> frequencies_hz;  // at least one
    std::vector<double> levels_dbm;      // generator (or reference radio) levels, at least one
    int valid_days = 365;
    std::string method = kMethodGenerator;
    bool simulated = false;              // the radio is simulated (practice calibration)
    // Readings closer than this to full scale or too close to the noise
    // are refused.
    double max_dbfs = -6.0;
    double min_snr_db = 20.0;

    // "" when usable, else what is wrong (English).
    std::string validate() const;
};

struct Target {
    std::size_t index = 0;  // 0 .. count-1
    std::size_t count = 0;
    double frequency_hz = 0.0;
    double level_dbm = 0.0;
};

struct Reading {
    double frequency_hz = 0.0;
    double level_dbm = 0.0;
    double measured_dbfs = kNaN;
    double snr_db = kNaN;
    bool accepted = false;
    std::string problem;  // why it was refused (English), "" if accepted
};

// Mean of several level readings of one point (records of the radio at
// the generator's frequency). Records flagged NO_SYNC or OVERLOAD and
// non-finite levels are not used; the first `skip` usable readings are
// left out (the receiver settles after a change).
class LevelAverager {
public:
    explicit LevelAverager(std::size_t skip = 2) : skip_(skip) {}
    // `no_sync`/`overload`: the record's flags.
    void add(double level_dbfs, bool no_sync, bool overload, double snr_db = kNaN);
    std::size_t count() const noexcept { return n_; }
    double mean_dbfs() const noexcept;  // NaN without readings
    double mean_snr_db() const noexcept;  // NaN without SNR values
    double spread_db() const noexcept;  // max - min, NaN without readings
    bool any_overload() const noexcept { return overload_; }
    void reset();

private:
    std::size_t skip_ = 2;
    std::size_t skipped_ = 0;
    std::size_t n_ = 0;
    double sum_ = 0.0, lo_ = 0.0, hi_ = 0.0;
    std::size_t n_snr_ = 0;
    double sum_snr_ = 0.0;
    bool overload_ = false;
};

class Wizard {
public:
    // Throws std::invalid_argument when plan.validate() is not "".
    Wizard(Key key, Plan plan, std::string radio_plugin_id = {});

    const Key& key() const { return key_; }
    const Plan& plan() const { return plan_; }
    bool done() const { return next_ >= count(); }
    std::size_t count() const { return plan_.frequencies_hz.size() * plan_.levels_dbm.size(); }
    // The point to set on the generator now (frequency-major order).
    Target target() const;

    // The radio's reading of the current target (mean dBFS). Accepted
    // readings move on to the next target; a refused one (no reading,
    // overload, too close to full scale, too weak) stays on it and says
    // why.
    const Reading& record(double measured_dbfs, double snr_db = kNaN, bool overload = false);
    const Reading& record(const LevelAverager& readings);
    // Skips the current target (no reading).
    void skip();
    // Starts over with the same plan.
    void restart();

    const std::vector<Reading>& readings() const { return readings_; }
    std::size_t accepted() const;
    // The entry from the accepted readings: per frequency the mean of
    // (level - measured) and the spread over the levels (linearity).
    // Empty points if nothing was accepted.
    Entry result(std::int64_t created_unix) const;

private:
    Key key_;
    Plan plan_;
    std::string radio_plugin_id_;
    std::size_t next_ = 0;
    std::vector<Reading> readings_;
};

// Verify mode: how far a reading through the table is off the generator
// level (dB, positive: the table reads high). NaN if the lookup is not
// usable.
double verify_error_db(const Lookup& lookup, double measured_dbfs, double reference_dbm);

// Frequency calibration: the oscillator error in ppm from a carrier known
// to be at `true_hz` that the radio sees at `measured_hz` (both RF).
// Positive: the radio's oscillator runs fast (it reads low). NaN for
// invalid input.
double ppm_from_carrier(double true_hz, double measured_hz);

}  // namespace yantra::calibration
