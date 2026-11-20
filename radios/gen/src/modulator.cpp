// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pi/4-DQPSK modulator with root-raised-cosine shaping (EN 300 392-2
// clause 5) for the TETRA test-signal generator (RAD-70). Copied from
// yantra 68ea20a (plugins/tetra/gen/src/modulator.cpp), timing offset
// added.

#include "yantra/gen/modulator.hpp"

#include "yantra/gen/burst.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace yantra::gen {

double rrc_pulse(double x, double alpha)
{
    using std::numbers::pi;
    if (std::fabs(x) < 1e-9)
        return 1.0 - alpha + 4.0 * alpha / pi;
    if (alpha > 0.0 && std::fabs(std::fabs(4.0 * alpha * x) - 1.0) < 1e-9) {
        return alpha / std::numbers::sqrt2 *
               ((1.0 + 2.0 / pi) * std::sin(pi / (4.0 * alpha)) + (1.0 - 2.0 / pi) * std::cos(pi / (4.0 * alpha)));
    }
    const double num = std::sin(pi * x * (1.0 - alpha)) + 4.0 * alpha * x * std::cos(pi * x * (1.0 + alpha));
    const double den = pi * x * (1.0 - (4.0 * alpha * x) * (4.0 * alpha * x));
    return num / den;
}

std::vector<std::complex<float>> map_symbols(const Bits& bits, int& phase)
{
    if (bits.size() % 2 != 0)
        throw std::invalid_argument("pi/4-DQPSK needs an even number of bits, got " + std::to_string(bits.size()));
    // The 8 points of the two QPSK constellations, exact to float precision.
    static const std::complex<float> kPoint[8] = {
        {1.0f, 0.0f},
        {static_cast<float>(std::numbers::sqrt2 / 2), static_cast<float>(std::numbers::sqrt2 / 2)},
        {0.0f, 1.0f},
        {-static_cast<float>(std::numbers::sqrt2 / 2), static_cast<float>(std::numbers::sqrt2 / 2)},
        {-1.0f, 0.0f},
        {-static_cast<float>(std::numbers::sqrt2 / 2), -static_cast<float>(std::numbers::sqrt2 / 2)},
        {0.0f, -1.0f},
        {static_cast<float>(std::numbers::sqrt2 / 2), -static_cast<float>(std::numbers::sqrt2 / 2)},
    };
    std::vector<std::complex<float>> out;
    out.reserve(bits.size() / 2);
    for (size_t i = 0; i < bits.size(); i += 2) {
        phase = (((phase + dibit_phase_step(bits[i], bits[i + 1])) % 8) + 8) % 8;
        out.push_back(kPoint[phase]);
    }
    return out;
}

Pi4DqpskModulator::Pi4DqpskModulator(double sample_rate_hz, double delay_seconds)
    : sample_rate_(sample_rate_hz),
      symbols_per_sample_(kSymbolRate / sample_rate_hz),
      delay_symbols_(delay_seconds * kSymbolRate)
{
    const double bandwidth = (1.0 + kRollOff) * kSymbolRate;
    if (!(sample_rate_hz > bandwidth) || !std::isfinite(sample_rate_hz))
        throw std::invalid_argument("modulator sample rate " + std::to_string(sample_rate_hz) +
                                    " Hz is below the signal bandwidth (24.3 kHz)");
    if (!(delay_seconds >= 0.0) || !std::isfinite(delay_seconds))
        throw std::invalid_argument("modulator timing offset must be finite and >= 0, got " +
                                    std::to_string(delay_seconds) + " s");
    const int n = 2 * kHalfSpanSymbols * kTablePerSymbol + 1;
    table_.resize(static_cast<size_t>(n));
    double energy = 0.0;
    for (int i = 0; i < n; ++i) {
        const double h = rrc_pulse(static_cast<double>(i - kHalfSpanSymbols * kTablePerSymbol) / kTablePerSymbol);
        table_[static_cast<size_t>(i)] = static_cast<float>(h);
        energy += h * h;
    }
    // Unit energy of the truncated pulse, so the average power is exactly 1.
    const double scale = 1.0 / std::sqrt(energy / kTablePerSymbol);
    for (float& v : table_)
        v = static_cast<float>(v * scale);
}

Pi4DqpskModulator Pi4DqpskModulator::with_samples_per_symbol(int samples_per_symbol)
{
    if (samples_per_symbol < 2)
        throw std::invalid_argument("at least 2 samples per symbol are needed");
    return Pi4DqpskModulator(kSymbolRate * samples_per_symbol);
}

void Pi4DqpskModulator::push_bits(const Bits& bits)
{
    const std::vector<std::complex<float>> s = map_symbols(bits, phase_);
    symbols_.insert(symbols_.end(), s.begin(), s.end());
    pushed_ += s.size();
}

size_t Pi4DqpskModulator::available() const
{
    // Sample n (at t = n * symbols_per_sample - delay symbols) needs every
    // symbol k < t, so t <= pushed. The margin keeps rounding from making
    // a sample available that a later push would still change, so any
    // read size gives the same samples.
    constexpr double kMargin = 1e-6;  // symbols
    const double last =
        std::floor((static_cast<double>(pushed_) + delay_symbols_ - kMargin) / symbols_per_sample_);
    const double n = last + 1.0 - static_cast<double>(next_);
    return n > 0.0 ? static_cast<size_t>(n) : 0;
}

double Pi4DqpskModulator::pulse(double x) const
{
    // Linear interpolation in the table; x in (-D, D).
    const double pos = (x + kHalfSpanSymbols) * kTablePerSymbol;
    const double fl = std::floor(pos);
    const size_t i = static_cast<size_t>(fl);
    if (i + 1 >= table_.size())
        return table_.back();
    const double f = pos - fl;
    return table_[i] + f * (table_[i + 1] - table_[i]);
}

void Pi4DqpskModulator::read(std::complex<float>* out, size_t n)
{
    if (n > available())
        throw std::logic_error("Pi4DqpskModulator::read: not enough symbols pushed");
    constexpr double kSpan = 2.0 * kHalfSpanSymbols;
    for (size_t j = 0; j < n; ++j, ++next_) {
        const double t = static_cast<double>(next_) * symbols_per_sample_ - delay_symbols_;  // in symbols
        // Symbols k with |t - k - D| < D: t - 2D < k < t.
        const double lo = std::floor(t - kSpan) + 1.0;
        uint64_t k0 = lo > 0.0 ? static_cast<uint64_t>(lo) : 0;
        k0 = std::max(k0, first_symbol_);
        const double hi = std::ceil(t) - 1.0;
        std::complex<double> acc{0.0, 0.0};
        if (hi >= 0.0 && pushed_ > 0) {
            const uint64_t k1 = std::min(static_cast<uint64_t>(hi), pushed_ - 1);
            for (uint64_t k = k0; k <= k1; ++k) {
                const double x = t - static_cast<double>(k) - kHalfSpanSymbols;
                const std::complex<float> s = symbols_[static_cast<size_t>(k - first_symbol_)];
                const double h = pulse(x);
                acc += std::complex<double>(s.real() * h, s.imag() * h);
            }
        }
        out[j] = std::complex<float>(static_cast<float>(acc.real()), static_cast<float>(acc.imag()));
    }
    // Drop symbols no later sample needs.
    const double t_next = static_cast<double>(next_) * symbols_per_sample_ - delay_symbols_;
    const double keep_from = std::floor(t_next - kSpan);
    while (!symbols_.empty() && static_cast<double>(first_symbol_) < keep_from) {
        symbols_.pop_front();
        ++first_symbol_;
    }
}

}  // namespace yantra::gen
