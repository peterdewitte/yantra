// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Self-interference warning logic (NFR-07; docs/MEASUREMENT_PIPELINE.md
// "Self-interference"): a laptop, its USB bus and cables radiate RF noise
// that an antenna close to them picks up. Yantra has no spectrum (the
// radio plugins keep their IQ, PLG-27), so the logic works on what the
// measurement cycles carry:
//
// - Noise: the least level - snr_db (the radio API's QUALITY field
//   snr_db, RAD-21) over the cycle's fresh, synced, calibrated samples, in
//   dBm. Each sample's level - snr_db is the noise and interference next
//   to its carrier plus the receiver's own distortion: a receiver's SNR
//   estimate stops rising at its own limit (about 36 dB for the TETRA
//   receiver of yantra-radios), so for a strong carrier (an indoor DAS at
//   -35 dBm) it is far above the noise. Each sample is an upper bound of
//   the noise, and the least is the closest; noise from the PC raises it
//   next to every carrier.
// - Rise: the noise minus a reference: the noise taken with the antenna
//   away from the PC (set_reference), else the receiver's thermal floor
//   -174 dBm/Hz + 10 log10(bandwidth) + noise figure. A rise >= rise_db
//   is a finding when at least one sample has an SNR below snr_limit_db
//   (one that measured noise, not the receiver's limit): with strong
//   carriers only, the noise is just bounded and shows no rise.
// - Unsynced carriers: at least min_unsynced fresh NO_SYNC samples (a
//   level but no identity: what wideband noise and spurs look like to a
//   decoder) are a finding. Not counted: carriers less than
//   unsynced_above_db above the reference (the noise itself), and carriers within skirt_hz of a
//   synced carrier at least skirt_db stronger: the skirts of a strong
//   carrier (an indoor DAS at -40 dBm shows as weak carriers 6.25 to 62.5
//   kHz beside it, 50 dB down).
// - Monitor: the warning comes on after on_cycles consecutive cycles with
//   a finding and goes off after off_cycles consecutive cycles without one
//   (no flicker). The UI (phase 9) shows it with its advice text.
//
// Technology-free (PLG-01): only radio API fields and sample flags.
#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "yantra/core/measure/sample.h"

namespace yantra::measure {

struct SelfInterferenceOptions {
    double rise_db = 6.0;
    double bandwidth_hz = 25'000.0;   ///< the receiver's channel bandwidth, for the thermal floor
    double noise_figure_db = 15.0;    ///< a typical USB SDR front end
    std::size_t min_noise_samples = 1;
    /// An SNR at or above this may be the receiver's own limit: such a
    /// sample bounds the noise but cannot show a rise on its own.
    double snr_limit_db = 30.0;
    std::size_t min_unsynced = 3;
    double unsynced_above_db = 6.0;   ///< an unsynced carrier counts this far above the reference
    double skirt_hz = 100'000.0;      ///< ... and not this close to a synced carrier
    double skirt_db = 30.0;           ///< ... that is this much stronger
    std::size_t on_cycles = 3;
    std::size_t off_cycles = 10;
};

/// What one cycle shows.
struct InterferenceCheck {
    std::optional<double> noise_dbm;  ///< nullopt: too few samples with level and SNR
    std::size_t noise_samples = 0;
    std::size_t noise_limited_samples = 0;  ///< samples with an SNR below snr_limit_db
    double reference_dbm = 0.0;       ///< what the noise was compared with
    bool thermal_reference = true;    ///< the reference is the thermal floor
    std::optional<double> rise_db;
    std::size_t unsynced = 0;         ///< fresh NO_SYNC samples that count (see above)
    bool noise_raised = false;
    bool many_unsynced = false;
    bool finding() const noexcept { return noise_raised || many_unsynced; }
};

/// The receiver's thermal noise floor, dBm.
double thermal_noise_dbm(double bandwidth_hz, double noise_figure_db);

/// The noise of a cycle by the rule above (e.g. to take the reference with
/// the antenna away from the PC); nullopt with too few samples.
std::optional<double> cycle_noise_dbm(const MeasurementCycle& cycle, const SelfInterferenceOptions& options = {},
                                      std::size_t* samples = nullptr);

/// Checks one cycle against `reference_dbm` (nullopt: the thermal floor).
InterferenceCheck check_self_interference(const MeasurementCycle& cycle, std::optional<double> reference_dbm,
                                          const SelfInterferenceOptions& options = {});

/// The steady warning for the UI (status bar, level panel).
struct SelfInterferenceWarning {
    bool active = false;
    InterferenceCheck last;  ///< the last cycle's check
    std::string text;        ///< English text for the UI ("" when not active)
};

/// Not thread-safe: one thread (the cycle consumer) feeds it.
class SelfInterferenceMonitor {
public:
    explicit SelfInterferenceMonitor(SelfInterferenceOptions options = {});
    void set_reference(std::optional<double> noise_dbm) { reference_ = noise_dbm; }
    std::optional<double> reference() const { return reference_; }
    /// Feeds a cycle; returns the warning after it.
    const SelfInterferenceWarning& on_cycle(const MeasurementCycle& cycle);
    const SelfInterferenceWarning& warning() const { return warning_; }
    void reset();

private:
    SelfInterferenceOptions options_;
    std::optional<double> reference_;
    std::size_t with_finding_ = 0;
    std::size_t without_ = 0;
    SelfInterferenceWarning warning_;
};

/// The warning text of a check (also the hover text of the warning), with
/// the advice: keep the antenna away from the PC.
std::string self_interference_text(const InterferenceCheck& check);

}  // namespace yantra::measure
