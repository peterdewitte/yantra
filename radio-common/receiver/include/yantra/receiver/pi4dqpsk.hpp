// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pi/4-DQPSK demodulator (RAD-100) for a channel at a whole number of
// samples per symbol (the channelizer's output). Generic: symbol rate,
// roll-off and loop settings are parameters; technology parameter sets
// live in tetra_params.hpp.
//
// Chain per channel (docs/RECEIVER_DSP.md):
//  1. coarse frequency offset over the first coarse_symbols: candidate
//     offsets every symbol_rate / 12 within +/- max_cfo_hz, each mixed
//     and matched-filtered (so an adjacent carrier does not pull it); the
//     most coherent 4th power of the differential product wins and its
//     phase gives the residual; at low SNR, where that sum is incoherent,
//     the power-spectrum centroid of the band instead; repeated
//     over each further window while unlocked, restarting the FLL when it
//     is off by more than symbol_rate / 12 (wander during a gap, a new
//     carrier), before it can drift into its symbol_rate / 4 ambiguity;
//  2. NCO by the frequency estimate, RRC matched filter, block AGC;
//  3. symbol timing: Gardner detector at the symbol and half-symbol
//     points (cubic Lagrange interpolation), PI loop with a wide
//     acquisition bandwidth (at the start and after a lost lock) and a
//     narrow tracking one;
//     follows sample-clock errors up to +/-1 %;
//  4. fine frequency: FLL on the 4th power of the differential product
//     (pi/4-DQPSK steps of +/-pi/4, +/-3pi/4 all become -1), range
//     +/- symbol_rate / 8 even without the coarse step, updates the NCO;
//  5. differential detection -> soft bits (no phase ambiguity, robust in
//     fading), two per symbol: soft[0] = Im(d), soft[1] = Re(d) of the
//     normalised product d = z_k conj(z_{k-1}); > 0 means bit 0, about
//     +/-0.71 without noise (dibit 00 = +pi/4, 01 = +3pi/4, 11 = -3pi/4,
//     10 = -pi/4);
//  6. quality: decision-directed phase tracking on the 8-point
//     constellation, MER over an exponential window and a lock flag with
//     hysteresis. The phase-tracked symbol (`z`) and its ideal point are
//     given per symbol, so a later data-aided MER (e.g. on known bits)
//     can be computed from them.
#pragma once

#include "yantra/receiver/fft.hpp"
#include "yantra/receiver/fir.hpp"
#include "yantra/receiver/kernels.hpp"
#include "yantra/receiver/nco.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace yantra::receiver {

struct Pi4DqpskConfig {
    double symbol_rate_hz = 0.0;  ///< required
    int samples_per_symbol = 4;   ///< input rate = sps * symbol rate, >= 2
    double rolloff = 0.0;         ///< RRC roll-off, required, (0, 1]
    int rrc_span_symbols = 16;    ///< even
    bool coarse_cfo = true;
    std::size_t coarse_symbols = 512;
    /// Coarse search half-width; 0 = symbol_rate / 6.
    double max_cfo_hz = 0.0;
    /// Timing loop noise bandwidth (fraction of the symbol rate) while
    /// acquiring (the first acquisition_symbols, and after a lost lock
    /// until locked again), else while tracking.
    double timing_bw_acquire = 0.02;
    double timing_bw_track = 0.004;
    /// FLL gain per 8-symbol block (fraction of the measured error
    /// applied) for acquisition_symbols after a coarse estimate, then.
    double fll_gain_acquire = 0.25;
    double fll_gain_track = 0.05;
    std::size_t acquisition_symbols = 256;
    /// Decision-directed phase tracking gain (quality only).
    double phase_gain = 0.1;
    /// MER averaging (exponential, in symbols).
    std::size_t mer_symbols = 255;
    double lock_mer_db = 10.0;
    double unlock_mer_db = 7.0;
    Isa isa = Isa::Simd;
};

struct DemodSymbol {
    uint64_t index = 0;        ///< symbol number since reset
    double sample_pos = 0.0;   ///< symbol centre in input samples since reset
    cf z;                      ///< phase-tracked, normalised symbol (|ideal| ~ 1)
    cf ideal;                  ///< nearest of the 8 constellation points
    float soft[2] = {0, 0};    ///< > 0: bit 0
    uint8_t dibit() const noexcept { return static_cast<uint8_t>(((soft[0] < 0) << 1) | (soft[1] < 0)); }
};

struct DemodStatus {
    double coarse_offset_hz = 0.0;  ///< coarse estimate (0 when disabled)
    double frequency_offset_hz = 0.0;  ///< total carrier offset estimate
    double timing_rate_ppm = 0.0;   ///< sample clock vs symbol clock
    double mer_db = 0.0;
    bool locked = false;
    uint64_t symbols = 0;
};

class Pi4DqpskDemodulator {
public:
    /// std::invalid_argument for a bad configuration.
    explicit Pi4DqpskDemodulator(const Pi4DqpskConfig& config);

    const Pi4DqpskConfig& config() const noexcept { return config_; }
    /// Demodulates `n` input samples; appends the finished symbols.
    void process(const cf* in, std::size_t n, std::vector<DemodSymbol>& out);
    const DemodStatus& status() const noexcept { return status_; }
    /// Starts over (new coarse estimate), e.g. after a retune or a gap.
    void reset();

private:
    double estimate_coarse(const cf* x) const;
    double spectral_centroid(const cf* x) const;
    void run(const cf* in, std::size_t n, std::vector<DemodSymbol>& out);
    void symbols(std::vector<DemodSymbol>& out);
    cf interpolate(double t) const;

    Pi4DqpskConfig config_;
    int sps_;
    double max_cfo_;
    const Kernels* k_;
    FirFilter rrc_;
    Nco nco_;
    double rrc_delay_;
    // Gain of the Gardner detector (output per sample of timing error) at
    // unit power; measured, see docs/RECEIVER_DSP.md.
    double kd_;

    bool coarse_pending_ = true;
    std::vector<cf> pending_;
    uint64_t input_pos_ = 0;  // input samples run through the chain
    std::vector<cf> mixed_;
    std::vector<cf> y_;       // filtered, AGC-scaled samples
    uint64_t y_base_ = 0;     // input-chain index of y_[0]
    double t_ = 0.0;          // next symbol strobe, relative to y_[0]
    double agc_power_ = 0.0;
    double integ_ = 0.0;      // timing loop integrator (samples per symbol)
    double rate_ppm_ = 0.0;   // smoothed sample-clock error
    bool reacquire_ = false;  // lock lost: wide loops until locked again
    std::size_t fll_acquire_left_ = 0;  // symbols of wide FLL gain left
    std::vector<cf> roll_;    // raw input for a new coarse estimate while unlocked
    double freq_ = 0.0;       // carrier offset estimate (Hz)
    cf fll_acc_;
    int fll_count_ = 0;
    cf z_prev_;
    bool have_prev_ = false;
    double theta_ = 0.0;      // phase tracker
    double amp_ = 1.0;        // symbol magnitude estimate
    // The 9 decision angles k*pi/4 (k = -4..4) as unit phasors, made once
    // with std::polar: the same floats as computing them per symbol, without
    // a sin/cos pair per symbol (their cost is the C runtime's).
    std::array<cf, 9> ideal_phasor_;
    double sig_acc_ = 0.0;
    double err_acc_ = 0.0;
    DemodStatus status_;
};

}  // namespace yantra::receiver
