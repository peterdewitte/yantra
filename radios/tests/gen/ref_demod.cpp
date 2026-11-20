// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test-only pi/4-DQPSK demodulator (see ref_demod.h).

#include "ref_demod.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace yantra::gen::ref {

namespace {

constexpr double kAlpha = 0.35;
constexpr double kHalfSpan = 8.0;  // symbols on each side used by the matched filter
constexpr int kPerSymbol = 256;    // table resolution

// Square root of the raised-cosine spectrum at f (in units of the symbol
// rate); its integral over f is 1.
double sqrt_raised_cosine(double f)
{
    f = std::fabs(f);
    const double f1 = (1.0 - kAlpha) / 2.0;
    const double f2 = (1.0 + kAlpha) / 2.0;
    if (f <= f1)
        return 1.0;
    if (f >= f2)
        return 0.0;
    return std::sqrt(0.5 * (1.0 + std::cos(std::numbers::pi / kAlpha * (f - f1))));
}

// Simpson's rule of g over [a, b] with n (even) intervals.
template <class G>
double simpson(G g, double a, double b, int n)
{
    const double h = (b - a) / n;
    double s = g(a) + g(b);
    for (int i = 1; i < n; ++i)
        s += (i % 2 == 1 ? 4.0 : 2.0) * g(a + i * h);
    return s * h / 3.0;
}

const std::vector<double>& table()
{
    static const std::vector<double> t = [] {
        const int n = static_cast<int>(2 * kHalfSpan * kPerSymbol) + 1;
        std::vector<double> v(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            v[static_cast<size_t>(i)] = rrc_reference(static_cast<double>(i) / kPerSymbol - kHalfSpan);
        return v;
    }();
    return t;
}

double pulse(double x)
{
    const double pos = (x + kHalfSpan) * kPerSymbol;
    if (pos < 0.0 || pos >= static_cast<double>(table().size() - 1))
        return 0.0;
    const size_t i = static_cast<size_t>(pos);
    const double f = pos - static_cast<double>(i);
    return table()[i] * (1.0 - f) + table()[i + 1] * f;
}

}  // namespace

double rrc_reference(double x)
{
    // h(x) = 2 * integral over f >= 0 of H(f) cos(2 pi f x); split at the
    // edge of the flat part where H has a kink.
    const auto g = [x](double f) { return sqrt_raised_cosine(f) * std::cos(2.0 * std::numbers::pi * f * x); };
    const double f1 = (1.0 - kAlpha) / 2.0;
    const double f2 = (1.0 + kAlpha) / 2.0;
    return 2.0 * (simpson(g, 0.0, f1, 400) + simpson(g, f1, f2, 400));
}

std::vector<std::complex<double>> matched_symbols(const std::vector<std::complex<float>>& iq, double fs,
                                                  double delay_symbols, uint64_t first, size_t count,
                                                  double offset_hz)
{
    const double sps = fs / 18000.0;
    std::vector<std::complex<double>> out;
    out.reserve(count);
    for (size_t j = 0; j < count; ++j) {
        const double centre = (static_cast<double>(first + j) + delay_symbols) * sps;  // in samples
        const long lo = static_cast<long>(std::ceil(centre - kHalfSpan * sps));
        const long hi = static_cast<long>(std::floor(centre + kHalfSpan * sps));
        if (lo < 0 || hi >= static_cast<long>(iq.size()))
            throw std::out_of_range("matched_symbols: symbol window outside the samples");
        std::complex<double> acc{0.0, 0.0};
        for (long n = lo; n <= hi; ++n) {
            const double h = pulse((centre - static_cast<double>(n)) / sps);
            const double phi = -2.0 * std::numbers::pi * offset_hz * static_cast<double>(n) / fs;
            acc += std::complex<double>(iq[static_cast<size_t>(n)]) * std::polar(1.0, phi) * h;
        }
        out.push_back(acc / sps);
    }
    return out;
}

Bits differential_decode(const std::vector<std::complex<double>>& z, std::complex<double> previous)
{
    Bits bits;
    bits.reserve(2 * z.size());
    for (const std::complex<double>& s : z) {
        const double angle = std::arg(s * std::conj(previous));  // -pi .. pi
        previous = s;
        const double q = angle / (std::numbers::pi / 4.0);
        // Nearest odd multiple of pi/4: +1 00, +3 01, -1 10, -3 11.
        if (q >= 0.0) {
            bits.push_back(0);
            bits.push_back(q < 2.0 ? 0 : 1);
        } else {
            bits.push_back(1);
            bits.push_back(q > -2.0 ? 0 : 1);
        }
    }
    return bits;
}

}  // namespace yantra::gen::ref
