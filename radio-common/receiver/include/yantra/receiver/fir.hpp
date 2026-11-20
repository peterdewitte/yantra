// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FIR design and filtering (RAD-100): Kaiser-window low-pass design,
// root-raised-cosine (RRC) matched filter taps for any roll-off, span and
// samples per symbol, and a streaming decimating FIR filter for complex
// samples with real taps (SIMD dot product, kernels.h).
#pragma once

#include "yantra/receiver/kernels.hpp"

#include <cstddef>
#include <vector>

namespace yantra::receiver {

/// Kaiser window beta for a stopband attenuation in dB.
double kaiser_beta(double atten_db);
/// Odd number of taps for a Kaiser low-pass with this transition width
/// and attenuation at `rate_hz`.
std::size_t kaiser_length(double transition_hz, double atten_db, double rate_hz);
/// Linear-phase low-pass: windowed sinc cut off at `cutoff_hz` (the middle
/// of the transition band), Kaiser window, DC gain 1, at least `atten_db`
/// down from cutoff + transition/2 on (designed with a 1.5 dB margin: odd
/// length kaiser_length(transition_hz, atten_db + 1.5, ...) or `min_taps`
/// if larger).
/// std::invalid_argument for cutoffs outside (0, rate/2) or bad widths.
std::vector<float> kaiser_lowpass(double cutoff_hz, double transition_hz, double atten_db, double rate_hz,
                                  std::size_t min_taps = 1);

/// Root-raised-cosine pulse at `t` symbol periods from its centre, roll-off
/// `rolloff` in (0, 1], unit energy (integral of p^2 dt = 1, t in symbols).
double rrc_value(double t, double rolloff);
/// RRC taps for `samples_per_symbol` (>= 1) over `span_symbols` (>= 2,
/// even) symbols: span * sps + 1 taps, symmetric, scaled to unit energy
/// (sum h^2 = 1), so a matched pair peaks at 1 and is nearly zero at
/// every other multiple of sps (raised cosine, no ISI).
std::vector<float> rrc_taps(double rolloff, int samples_per_symbol, int span_symbols);

/// Streaming FIR filter, complex input, real taps, decimation by an
/// integer D: output k is sum_j h[j] x[k*D - j] (input 0 is the first
/// sample pushed after construction or reset(); earlier inputs are 0).
class FirFilter {
public:
    /// std::invalid_argument for no taps or decimation 0.
    explicit FirFilter(std::vector<float> taps, std::size_t decimation = 1, Isa isa = Isa::Simd);

    std::size_t taps() const noexcept { return taps_; }
    std::size_t decimation() const noexcept { return decimation_; }
    /// Delay of a linear-phase filter in input samples: (taps - 1) / 2.
    double delay() const noexcept { return 0.5 * static_cast<double>(taps_ - 1); }
    /// Appends the outputs due for `n` more inputs to `out`.
    void process(const cf* in, std::size_t n, std::vector<cf>& out);
    void reset();

private:
    std::size_t taps_;
    std::size_t decimation_;
    const Kernels* k_;
    std::vector<float> h2_;  // reversed, each tap twice (kernels.h)
    std::vector<cf> hist_;   // last taps-1 inputs, then the new ones
    std::size_t skip_ = 0;   // inputs to take before the next output
};

}  // namespace yantra::receiver
