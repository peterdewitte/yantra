// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Channel model of the TETRA test-signal generator (RAD-72), applied to the
// transmitter IQ streams of cell_tx.hpp by the scenario engine
// (scenario_engine.hpp) and usable on its own:
//
//   large scale   free-space and log-distance path loss, spatially
//                 correlated log-normal shadowing (Gudmundson);
//   small scale   flat Rayleigh/Rician fading (sum of sinusoids, Clarke
//                 spectrum) and the TETRA propagation conditions of
//                 EN 300 392-2 (static, TU50, HT200) as tapped delay lines;
//   frequency     Doppler shift, CFO from oscillator errors (ppm); the
//                 sample-clock error is made by rendering the transmitters
//                 at fs * (1 + ppm) and labelling the result fs;
//   receiver      AWGN from a noise figure, gain steps (AGC), IQ imbalance,
//                 swapped I/Q, DC spike, clipping, n-bit (8-bit)
//                 quantisation, and sample drops.
//
// Everything is seeded (random.hpp) and plain scalar code, so a seed gives
// the same samples in every build tree. Formulas: docs/CHANNEL_MODEL.md.
// Copied from yantra 68ea20a (core/include/yantra/core/sim/fading.h,
// impairments.h, propagation.h; plugins/tetra/sens/include/yantra/tetra/
// sens/channel.h), adapted for RAD-72: no building model (the indoor
// multi-wall model is multiwall.hpp), no tap-delay limit, receiver
// front-end impairments and sample drops added.
#pragma once

#include "random.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yantra::gen {

inline constexpr double kSpeedOfLight = 299792458.0;
/// Boltzmann constant times 290 K in dBm/Hz: 10 log10(k T0 / 1 mW).
inline constexpr double kThermalNoiseDbmPerHz = -173.97518719422808;

// ------------------------------------------------------------ path loss --

/// Free-space path loss 20 log10(4 pi d f / c), dB. Distances below 1 cm
/// are taken as 1 cm.
double free_space_path_loss_db(double distance_m, double frequency_hz);

/// Log-distance path loss: PL(d) = PL(d0) + 10 n log10(d / d0) for
/// d >= d0, PL(d0) for d < d0. PL(d0) is free space at d0 unless given.
struct LogDistanceModel {
    double exponent = 2.0;
    double reference_distance_m = 1.0;
    std::optional<double> reference_loss_db;

    double loss_db(double distance_m, double frequency_hz) const;
};

/// Spatially correlated log-normal shadowing in dB over the plane: zero
/// mean, standard deviation `sigma_db`, correlation exp(-d / decorrelation)
/// between points d metres apart (Gudmundson). A sum of random plane waves
/// whose wave numbers follow the 2D spectrum of that correlation: defined
/// everywhere, cheap, the same for the same seed. Sigma 0: 0 everywhere.
class ShadowingField {
public:
    ShadowingField(double sigma_db, double decorrelation_m, uint64_t seed, int components = 256);

    double value(double x_m, double y_m) const;
    double sigma_db() const { return sigma_; }
    double decorrelation_m() const { return decorrelation_; }

private:
    struct Wave {
        double kx, ky, phase;
    };
    double sigma_;
    double decorrelation_;
    double amplitude_ = 0.0;
    std::vector<Wave> waves_;
};

// --------------------------------------------------------------- fading --

enum class FadingType { none, rayleigh, rician };

struct FadingSpec {
    FadingType type = FadingType::none;
    /// Rician K factor (LOS power / scattered power), dB.
    double k_db = 6.0;
    /// Sinusoids per quadrature branch.
    int sinusoids = 32;
};

/// "none", "rayleigh", "rician"; std::invalid_argument otherwise.
FadingType fading_type_from_string(const std::string& s);
const char* to_string(FadingType t);

/// Maximum Doppler frequency |v| f / c.
double doppler_hz(double speed_mps, double frequency_hz);

/// Flat fading: complex gain over time from a sum of sinusoids
/// (Zheng-Xiao type with a random angle offset), Clarke's Doppler spectrum
/// with maximum Doppler `doppler_hz`; E|g|^2 = 1.
class FadingGenerator {
public:
    /// `doppler_hz` >= 0; with 0 the gain is a random constant (Rayleigh,
    /// Rician) or 1 (none).
    FadingGenerator(const FadingSpec& spec, double doppler_hz, uint64_t seed);

    /// Complex gain at time t (seconds). None: always 1.
    std::complex<double> gain(double t) const;
    const FadingSpec& spec() const { return spec_; }
    double doppler_hz() const { return doppler_; }

private:
    struct Branch {
        double w_i, phi_i;  // in-phase: cos(w_i t + phi_i)
        double w_q, phi_q;  // quadrature
    };
    FadingSpec spec_;
    double doppler_;
    double scatter_amp_ = 1.0;
    double los_amp_ = 0.0;
    double los_w_ = 0.0;
    double los_phase_ = 0.0;
    std::vector<Branch> branches_;
};

// ----------------------------------------------------------- multipath --

struct Tap {
    double delay_us = 0.0;
    double power_db = 0.0;  ///< relative to the first tap
};

/// A propagation condition: taps and the mobile speed for their Doppler.
struct Propagation {
    std::string name;
    std::vector<Tap> taps;
    double speed_kmh = 0.0;
    bool fading = false;  ///< Rayleigh taps (false: static)
};

/// The propagation conditions of EN 300 392-2 (clause 6, propagation
/// models): "static" (one tap, no fading), "TU50" (typical urban: 0 us
/// 0 dB, 5 us -22.3 dB, 50 km/h) and "HT200" (hilly terrain: 0 us 0 dB,
/// 15 us -8.6 dB, 200 km/h); names are case-insensitive.
/// std::invalid_argument otherwise.
Propagation propagation(const std::string& name);
std::vector<std::string> propagation_names();

/// Tapped delay line with independently Rayleigh-fading taps (classical
/// Doppler spectrum), total mean power 1. Taps at fractional sample delays
/// use a windowed sinc (Blackman, +/-16 samples), so a multipath channel
/// delays the signal by kHalfLength samples besides the tap delays. Tap
/// gains are evaluated every kGainStep samples and held in between.
class MultipathChannel {
public:
    static constexpr int kHalfLength = 16;
    static constexpr int kGainStep = 4;

    /// `carrier_hz` sets the Doppler (speed / c * carrier); `seed` seeds
    /// every tap (derived per tap). std::invalid_argument for a rate <= 0,
    /// no taps, or a tap delay beyond 4096 samples.
    MultipathChannel(const Propagation& p, double rate_hz, double carrier_hz, uint64_t seed);

    /// Filters `n` samples in place; continuous across calls.
    void apply(std::complex<float>* x, size_t n);
    /// Delay of the first tap in samples (0 for the static channel).
    int bulk_delay() const { return identity_ ? 0 : kHalfLength; }
    bool identity() const { return identity_; }
    double doppler_hz() const { return doppler_; }

private:
    struct Path {
        std::vector<float> h;  // fractional-delay filter scaled by the tap amplitude
        FadingGenerator fade;
        std::complex<double> gain{1.0, 0.0};
    };
    bool identity_ = false;
    double rate_;
    double doppler_ = 0.0;
    size_t length_ = 0;
    std::vector<Path> paths_;
    std::vector<std::complex<float>> hist_;
    uint64_t n_ = 0;
};

// ------------------------------------------------- frequency and noise --

/// Frequency error of an oscillator `ppm` parts per million off at
/// `frequency_hz`.
inline double ppm_to_hz(double ppm, double frequency_hz) { return ppm * 1e-6 * frequency_hz; }

/// Thermal noise power in `bandwidth_hz` at the input of a receiver with
/// noise figure `nf_db`: kT0 B F, dBm.
double thermal_noise_dbm(double bandwidth_hz, double nf_db);

/// Sample power (mean |x|^2) of a level `dbm` when power 1 stands for
/// `reference_dbm`, and the inverse.
double dbm_to_power(double dbm, double reference_dbm);
double power_to_dbm(double power, double reference_dbm);

/// Multiplies samples by exp(j 2 pi f n / fs), continuous across calls
/// (phase kept in cycles as a double).
class FrequencyShifter {
public:
    FrequencyShifter(double frequency_hz, double sample_rate_hz, double phase_cycles = 0.0);
    void apply(std::complex<float>* x, size_t n);
    double phase_cycles() const { return cycles_; }

private:
    double step_;
    double cycles_;
};

/// Complex white Gaussian noise of mean power `power` (|n|^2), added to
/// samples. Deterministic for a seed.
class Awgn {
public:
    Awgn(double power, uint64_t seed);
    void add(std::complex<float>* x, size_t n);
    std::complex<double> sample();
    double power() const { return power_; }

private:
    double power_;
    double sigma_;  // per real component
    Rng rng_;
};

// -------------------------------------------------- receiver front end --

/// Impairments of the receiver after the antenna, in this order: gain
/// (AGC step), IQ imbalance, I/Q swap, DC spike, clipping, quantisation.
/// Amplitudes are in sample units (power 1 = the reference level).
struct FrontEndSpec {
    double gain_db = 0.0;
    /// IQ imbalance: Q branch gain relative to I (dB) and phase error
    /// (degrees): I' = I, Q' = g (Q cos(phi) - I sin(phi)).
    double iq_gain_db = 0.0;
    double iq_phase_deg = 0.0;
    bool swap_iq = false;
    /// DC offset added to every sample (the "DC spike" at 0 Hz).
    std::complex<double> dc{0.0, 0.0};
    /// Full-scale amplitude of one component (I or Q): with `clip`, values
    /// beyond +/- full scale are clipped; with `quantise_bits` > 0 both
    /// components are rounded to full_scale / 2^(bits-1) steps and limited
    /// to [-2^(bits-1), 2^(bits-1) - 1] steps (8 bits: like cs8 files).
    double full_scale = 1.0;
    bool clip = false;
    int quantise_bits = 0;
};

class FrontEnd {
public:
    /// std::invalid_argument for non-finite values, full_scale <= 0 or
    /// quantise_bits outside 0 and 2..16.
    explicit FrontEnd(const FrontEndSpec& spec);
    void apply(std::complex<float>* x, size_t n);
    /// Changes the gain from the next sample on (an AGC step).
    void set_gain_db(double gain_db);
    const FrontEndSpec& spec() const { return spec_; }
    /// Samples with at least one component at or beyond full scale
    /// (clipping or quantiser limit) so far.
    uint64_t clipped() const { return clipped_; }
    /// The image rejection ratio of the IQ imbalance, dB (inf without).
    double image_rejection_db() const;

private:
    FrontEndSpec spec_;
    double gain_;
    double q_cos_, q_sin_;  // g cos(phi), g sin(phi)
    double step_ = 0.0;
    double lo_ = 0.0, hi_ = 0.0;
    uint64_t clipped_ = 0;
};

/// Sample drops (USB overflows): starting at sample `first`, `length`
/// samples are lost every `interval` samples. Works on a stream of input
/// sample indexes; deterministic.
struct DropSpec {
    uint64_t first = 0;
    uint64_t interval = 0;  ///< 0: no drops
    uint64_t length = 0;
};

class SampleDropper {
public:
    /// std::invalid_argument when length >= interval (> 0).
    explicit SampleDropper(const DropSpec& spec);
    /// Copies the samples of in[0..n) that are not dropped to `out`
    /// (which may be `in`); returns how many.
    size_t process(const std::complex<float>* in, size_t n, std::complex<float>* out);
    /// Whether input sample `index` is dropped.
    bool dropped(uint64_t index) const;
    /// Input samples dropped before input sample `index`.
    uint64_t dropped_before(uint64_t index) const;
    uint64_t input_samples() const { return in_; }
    uint64_t dropped_total() const { return dropped_before(in_); }

private:
    DropSpec spec_;
    uint64_t in_ = 0;
};

}  // namespace yantra::gen
