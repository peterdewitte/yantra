// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/fir.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace yantra::receiver {
namespace {

// Modified Bessel function of the first kind, order 0 (power series).
double bessel_i0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 200; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
        if (term < 1e-17 * sum)
            break;
    }
    return sum;
}

}  // namespace

double kaiser_beta(double a)
{
    if (a > 50.0)
        return 0.1102 * (a - 8.7);
    if (a >= 21.0)
        return 0.5842 * std::pow(a - 21.0, 0.4) + 0.07886 * (a - 21.0);
    return 0.0;
}

std::size_t kaiser_length(double transition_hz, double atten_db, double rate_hz)
{
    if (!(transition_hz > 0.0) || !(rate_hz > 0.0))
        throw std::invalid_argument("kaiser_length: transition and rate must be positive");
    const double dw = 2.0 * std::numbers::pi * transition_hz / rate_hz;
    auto n = static_cast<std::size_t>(std::ceil((atten_db - 8.0) / (2.285 * dw))) + 1;
    if (n % 2 == 0)
        ++n;
    return std::max<std::size_t>(n, 1);
}

std::vector<float> kaiser_lowpass(double cutoff_hz, double transition_hz, double atten_db, double rate_hz,
                                  std::size_t min_taps)
{
    if (!(cutoff_hz > 0.0) || !(cutoff_hz < rate_hz / 2.0))
        throw std::invalid_argument("kaiser_lowpass: cutoff must be inside (0, rate/2)");
    // Kaiser's formulas are estimates: design 1.5 dB deeper so the
    // stopband really reaches atten_db (with float taps).
    const double design_db = atten_db + 1.5;
    std::size_t n = std::max(kaiser_length(transition_hz, design_db, rate_hz), min_taps);
    if (n % 2 == 0)
        ++n;
    const double beta = kaiser_beta(design_db);
    const double fc = cutoff_hz / rate_hz;
    const double mid = 0.5 * static_cast<double>(n - 1);
    std::vector<double> h(n);
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double m = static_cast<double>(i) - mid;
        const double sinc = m == 0.0 ? 2.0 * fc : std::sin(2.0 * std::numbers::pi * fc * m) / (std::numbers::pi * m);
        const double r = mid > 0.0 ? m / mid : 0.0;
        const double w = bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel_i0(beta);
        h[i] = sinc * w;
        sum += h[i];
    }
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = static_cast<float>(h[i] / sum);
    return out;
}

double rrc_value(double t, double a)
{
    if (!(a > 0.0) || a > 1.0)
        throw std::invalid_argument("rrc_value: roll-off must be in (0, 1]");
    const double pi = std::numbers::pi;
    if (std::fabs(t) < 1e-9)
        return 1.0 - a + 4.0 * a / pi;
    if (std::fabs(std::fabs(t) - 1.0 / (4.0 * a)) < 1e-9)
        return a / std::sqrt(2.0) * ((1.0 + 2.0 / pi) * std::sin(pi / (4.0 * a)) + (1.0 - 2.0 / pi) * std::cos(pi / (4.0 * a)));
    const double num = std::sin(pi * t * (1.0 - a)) + 4.0 * a * t * std::cos(pi * t * (1.0 + a));
    const double den = pi * t * (1.0 - 16.0 * a * a * t * t);
    return num / den;
}

std::vector<float> rrc_taps(double rolloff, int sps, int span)
{
    if (sps < 1 || span < 2 || span % 2 != 0)
        throw std::invalid_argument("rrc_taps: samples per symbol >= 1 and an even span >= 2 are required");
    const std::size_t n = static_cast<std::size_t>(span) * static_cast<std::size_t>(sps) + 1;
    const double mid = 0.5 * static_cast<double>(n - 1);
    std::vector<double> h(n);
    double energy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        h[i] = rrc_value((static_cast<double>(i) - mid) / sps, rolloff);
        energy += h[i] * h[i];
    }
    const double scale = 1.0 / std::sqrt(energy);
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = static_cast<float>(h[i] * scale);
    return out;
}

FirFilter::FirFilter(std::vector<float> taps, std::size_t decimation, Isa isa)
    : taps_(taps.size()), decimation_(decimation), k_(&kernels(isa))
{
    if (taps.empty() || decimation == 0)
        throw std::invalid_argument("FirFilter: taps and a decimation >= 1 are required");
    h2_.resize(2 * taps_);
    for (std::size_t j = 0; j < taps_; ++j)
        h2_[2 * j] = h2_[2 * j + 1] = taps[taps_ - 1 - j];
    reset();
}

void FirFilter::reset()
{
    hist_.assign(taps_ - 1, cf{});
    skip_ = 0;
}

void FirFilter::process(const cf* in, std::size_t n, std::vector<cf>& out)
{
    if (n == 0)
        return;
    hist_.insert(hist_.end(), in, in + n);
    // Input i (0-based in this call) sits at hist_[taps - 1 + i]; its
    // output window starts at hist_[i].
    std::size_t i = skip_;
    for (; i < n; i += decimation_)
        out.push_back(k_->dot(hist_.data() + i, h2_.data(), taps_));
    skip_ = i - n;
    hist_.erase(hist_.begin(), hist_.begin() + static_cast<std::ptrdiff_t>(n));
}

}  // namespace yantra::receiver
