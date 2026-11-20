// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator's synthetic spectrum (radio API 1.2, RAD-140, RAD-61):
// no FFT of the generated samples, but the spectrum an SDR would show of
// the same scene. Each carrier on the air spreads its level over the TETRA
// downlink's power spectrum (root-raised-cosine filter, roll-off 0.35, 18
// ksymbol/s: flat to 5.85 kHz from the carrier, zero beyond 12.15 kHz),
// integrated over each bin, on top of the receiver noise (the scenario's
// noise in 18 kHz, scaled to the bin width). A small jitter from a seeded
// generator gives the look of an averaged FFT; the same seed and inputs
// give the same spectrum on every platform (own generator, no
// std::*_distribution).
#include "sim_device.hpp"

#include <algorithm>
#include <cmath>

namespace yantra::simulator {

namespace {

constexpr double kSymbolRate = 18000.0;
constexpr double kRollOff = 0.35;
constexpr double kFlatHz = (1.0 - kRollOff) * kSymbolRate / 2.0;  // 5850 Hz
constexpr double kEdgeHz = (1.0 + kRollOff) * kSymbolRate / 2.0;  // 12150 Hz
constexpr double kNoiseBandwidthHz = 18000.0;
/// Noise in 18 kHz without receiver noise: the thermal floor (-174 dBm/Hz)
/// with a 5 dB noise figure, so every bin stays finite.
constexpr double kQuietNoiseDbm = -174.0 + 42.55 + 5.0;
constexpr double kJitterDb = 0.6; ///< spread of the averaged-FFT look

double db_to_mw(double db) { return std::pow(10.0, db / 10.0); }

/// Raised-cosine power shape at an offset from the carrier, 1 in the flat
/// part; its integral over all offsets is kSymbolRate.
double shape(double offset_hz)
{
    const double f = std::fabs(offset_hz);
    if (f <= kFlatHz)
        return 1.0;
    if (f >= kEdgeHz)
        return 0.0;
    const double pi = 3.14159265358979323846;
    return 0.5 * (1.0 + std::cos(pi / (kRollOff * kSymbolRate) * (f - kFlatHz)));
}

/// splitmix64: a small, portable, seeded generator.
uint64_t next(uint64_t& state)
{
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// About normal, mean 0, standard deviation 1 (four uniforms).
double jitter(uint64_t& state)
{
    double sum = 0.0;
    for (int i = 0; i < 4; ++i)
        sum += static_cast<double>(next(state) >> 11) * (1.0 / 9007199254740992.0) - 0.5;
    return sum * std::sqrt(3.0);
}

} // namespace

std::vector<double> synthetic_spectrum(double start_hz, double bin_width_hz, uint32_t bins,
                                       const std::vector<SpectrumCarrier>& carriers,
                                       double noise_dbm, uint64_t seed)
{
    const double noise = std::isfinite(noise_dbm) ? noise_dbm : kQuietNoiseDbm;
    const double noise_bin_mw = db_to_mw(noise) * bin_width_hz / kNoiseBandwidthHz;
    std::vector<double> mw(bins, noise_bin_mw);
    // Each carrier over the bins it reaches, with sub-steps of at most
    // 250 Hz inside a bin.
    const int steps = std::max(4, static_cast<int>(std::ceil(bin_width_hz / 250.0)));
    const double dx = bin_width_hz / steps;
    for (const SpectrumCarrier& c : carriers) {
        if (!std::isfinite(c.level_dbm))
            continue;
        const double total_mw = db_to_mw(c.level_dbm);
        const double lo = c.frequency_hz - kEdgeHz, hi = c.frequency_hz + kEdgeHz;
        const double first = std::floor((lo - start_hz) / bin_width_hz);
        const double last = std::floor((hi - start_hz) / bin_width_hz);
        for (double b = std::max(0.0, first); b <= std::min(last, bins - 1.0); b += 1.0) {
            const double edge = start_hz + b * bin_width_hz;
            double share = 0.0;
            for (int k = 0; k < steps; ++k)
                share += shape(edge + (k + 0.5) * dx - c.frequency_hz) * dx;
            mw[static_cast<std::size_t>(b)] += total_mw * share / kSymbolRate;
        }
    }
    uint64_t state = seed ^ 0x5350454354525532ull; // "SPECTRU2"
    std::vector<double> out(bins);
    for (uint32_t i = 0; i < bins; ++i)
        out[i] = 10.0 * std::log10(mw[i]) + kJitterDb * jitter(state);
    return out;
}

} // namespace yantra::simulator
