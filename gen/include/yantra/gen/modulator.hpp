// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pi/4-DQPSK modulator of the TETRA test-signal generator (RAD-70), after
// EN 300 392-2 clause 5: 18 ksymbol/s, differential phase steps per dibit,
// root-raised-cosine pulse shaping with roll-off 0.35. Streaming: bits go
// in burst by burst, complex float baseband samples come out at any
// sample rate (not only whole samples per symbol). The symbol phase and
// the filter memory carry over from one burst to the next, so a stream of
// bursts is one continuous signal. A timing offset delays every symbol by
// a fixed time (any fraction of a sample). Plain scalar code: the default
// and the -DYANTRA_SIMD=OFF trees give the same samples.
// Copied from yantra 68ea20a (plugins/tetra/gen modulator.h), timing
// offset added.

#pragma once

#include "bits.hpp"
#include "timing.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace yantra::gen {

/// Root-raised-cosine roll-off factor of TETRA.
inline constexpr double kRollOff = 0.35;

/// Root-raised-cosine pulse at `x` symbol periods from its centre, scaled
/// to unit energy (the integral of h^2 over x is 1), roll-off `alpha`.
double rrc_pulse(double x, double alpha = kRollOff);

/// Maps dibits to pi/4-DQPSK symbols: each symbol is the previous one
/// turned by the dibit's phase step. `phase` (units of pi/4, 0..7) is the
/// phase before the first dibit and is updated to the last symbol's phase.
/// std::invalid_argument for an odd number of bits.
std::vector<std::complex<float>> map_symbols(const Bits& bits, int& phase);

class Pi4DqpskModulator {
public:
    /// One-sided pulse length in symbols (the filter spans twice this).
    static constexpr int kHalfSpanSymbols = 8;
    /// Table resolution of the pulse (points per symbol period).
    static constexpr int kTablePerSymbol = 512;

    /// Output at `sample_rate_hz` samples per second (> 24.3 kHz, the
    /// signal's occupied bandwidth (1 + roll-off) * 18 kHz), every symbol
    /// delayed by `delay_seconds` (>= 0, finite). std::invalid_argument
    /// otherwise.
    explicit Pi4DqpskModulator(double sample_rate_hz, double delay_seconds = 0.0);
    /// Output with a whole number of samples per symbol.
    static Pi4DqpskModulator with_samples_per_symbol(int samples_per_symbol);

    double sample_rate() const { return sample_rate_; }
    double samples_per_symbol() const { return sample_rate_ / kSymbolRate; }
    /// The timing offset in seconds.
    double delay() const { return delay_symbols_ / kSymbolRate; }

    /// Appends bits (an even number) as symbols.
    void push_bits(const Bits& bits);
    /// Symbols pushed so far.
    uint64_t symbols() const { return pushed_; }
    /// Phase of the last pushed symbol (units of pi/4, 0..7); 0 before any.
    int phase() const { return phase_; }

    /// Samples that can be read now: every symbol they depend on is known.
    size_t available() const;
    /// Writes `n` samples (n <= available(); std::logic_error otherwise).
    /// The average power is 1. Symbol k (0 = the first pushed) has its
    /// pulse centred at symbol_time(k) + delay() seconds after sample 0.
    void read(std::complex<float>* out, size_t n);
    /// Samples written so far.
    uint64_t samples() const { return next_; }

    /// Time of the pulse centre of symbol `k` in seconds after sample 0
    /// without a timing offset: (k + kHalfSpanSymbols) / 18000 s.
    static double symbol_time(uint64_t k) { return (static_cast<double>(k) + kHalfSpanSymbols) / kSymbolRate; }

private:
    double pulse(double x) const;

    double sample_rate_;
    double symbols_per_sample_;
    double delay_symbols_;
    std::vector<float> table_;  // pulse at x = -D .. D in steps of 1/kTablePerSymbol
    std::deque<std::complex<float>> symbols_;
    uint64_t first_symbol_ = 0;  // index of symbols_.front()
    uint64_t pushed_ = 0;
    uint64_t next_ = 0;  // next sample index
    int phase_ = 0;
};

}  // namespace yantra::gen
