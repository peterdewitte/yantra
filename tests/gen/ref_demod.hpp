// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test-only pi/4-DQPSK demodulator for the TETRA generator tests (RAD-70, RAD-93):
// matched root-raised-cosine filter computed from its frequency-domain
// definition (not from the generator's time-domain formula), sampled at
// the symbol centres, then differential detection. Coherent timing comes
// from the generator's documented symbol times; it is a checker, not a
// receiver.

#pragma once

#include "yantra/gen/bits.hpp"

#include <complex>
#include <cstdint>
#include <vector>

namespace yantra::gen::ref {

/// Root-raised-cosine pulse (roll-off 0.35, unit energy) at `x` symbol
/// periods, as the inverse Fourier transform of the square root of the
/// raised-cosine spectrum, integrated numerically.
double rrc_reference(double x);

/// Matched-filter outputs at the centres of symbols first..first+count-1,
/// symbol k being centred at (k + delay_symbols) / 18000 s after sample 0
/// of `iq` (rate `fs`), after shifting the signal by -offset_hz. With unit
/// symbols and no noise the outputs are the symbols (raised-cosine pulses
/// have no inter-symbol interference).
std::vector<std::complex<double>> matched_symbols(const std::vector<std::complex<float>>& iq, double fs,
                                                  double delay_symbols, uint64_t first, size_t count,
                                                  double offset_hz = 0.0);

/// Differential detection: the dibit of the phase step nearest to the
/// angle between consecutive outputs; `previous` precedes z[0].
Bits differential_decode(const std::vector<std::complex<double>>& z, std::complex<double> previous);

}  // namespace yantra::gen::ref
