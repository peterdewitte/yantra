// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pi/4-DQPSK modulator of the TETRA generator (RAD-70, RAD-93): pulse shape against
// its frequency-domain definition, symbol mapping known answers,
// demodulating back with the test-only demodulator (ref_demod.*) at
// several sample rates and with noise, streaming invariance, phase
// continuity across bursts, power and spectrum (occupied bandwidth,
// adjacent-channel power), timing offset.
// Copied from yantra 68ea20a (tests/tetra_gen/test_modulator.cpp); timing
// offset and adjacent-channel cases added.

#include "ref_demod.hpp"

#include "yantra/gen/burst.hpp"
#include "yantra/gen/modulator.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <complex>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

using namespace yantra::gen;
using cf = std::complex<float>;

namespace {

Bits random_bits(std::mt19937& rng, size_t n)
{
    Bits b(n);
    for (auto& v : b)
        v = static_cast<uint8_t>(rng() & 1u);
    return b;
}

// Samples that cover symbols 0..symbols-1 plus the filter tail.
size_t samples_for(const Pi4DqpskModulator& m, uint64_t symbols)
{
    return static_cast<size_t>(std::ceil((static_cast<double>(symbols) + 2.0 * Pi4DqpskModulator::kHalfSpanSymbols) *
                                         m.samples_per_symbol()));
}

std::vector<cf> modulate(Pi4DqpskModulator& m, const Bits& bits, size_t n)
{
    m.push_bits(bits);
    // Pad with symbols so the tail of the last real symbol can be read.
    m.push_bits(Bits(4 * Pi4DqpskModulator::kHalfSpanSymbols, 0));
    std::vector<cf> out(n);
    REQUIRE(m.available() >= n);
    m.read(out.data(), n);
    return out;
}

void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> w = std::polar(1.0, -2.0 * std::numbers::pi / static_cast<double>(len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> wk = 1.0;
            for (size_t k = 0; k < len / 2; ++k, wk *= w) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * wk;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// Welch power spectrum (Hann window, 50 % overlap), bins in FFT order.
std::vector<double> power_spectrum(const std::vector<cf>& x, size_t n)
{
    std::vector<double> psd(n, 0.0);
    std::vector<double> win(n);
    for (size_t i = 0; i < n; ++i)
        win[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n));
    for (size_t start = 0; start + n <= x.size(); start += n / 2) {
        std::vector<std::complex<double>> seg(n);
        for (size_t i = 0; i < n; ++i)
            seg[i] = std::complex<double>(x[start + i]) * win[i];
        fft(seg);
        for (size_t i = 0; i < n; ++i)
            psd[i] += std::norm(seg[i]);
    }
    return psd;
}

}  // namespace

TEST_CASE("Root-raised-cosine pulse matches its frequency-domain definition")
{
    CHECK(rrc_pulse(0.0) == doctest::Approx(1.0 - 0.35 + 4.0 * 0.35 / std::numbers::pi));
    const double singular = 1.0 / (4.0 * 0.35);  // where the time formula is 0/0
    for (double x : {0.0, 0.1, 0.25, 0.5, singular - 1e-6, singular, singular + 1e-6, 1.0, 1.5, 2.0, 3.3, 5.0, 7.9,
                     -0.3, -singular, -2.0, -6.5}) {
        CAPTURE(x);
        CHECK(std::fabs(rrc_pulse(x) - ref::rrc_reference(x)) < 1e-6);
    }
}

TEST_CASE("pi/4-DQPSK symbol mapping (known answers)")
{
    int phase = 0;
    // 00 +pi/4, 01 +3pi/4, 11 -3pi/4, 10 -pi/4 -> phases 1, 4, 1, 0.
    const auto s = map_symbols(from_string("00011110"), phase);
    REQUIRE(s.size() == 4);
    const double r = std::numbers::sqrt2 / 2;
    CHECK(s[0].real() == doctest::Approx(r));
    CHECK(s[0].imag() == doctest::Approx(r));
    CHECK(s[1].real() == doctest::Approx(-1.0));
    CHECK(s[1].imag() == doctest::Approx(0.0));
    CHECK(s[2].real() == doctest::Approx(r));
    CHECK(s[2].imag() == doctest::Approx(r));
    CHECK(s[3].real() == doctest::Approx(1.0));
    CHECK(s[3].imag() == doctest::Approx(0.0));
    CHECK(phase == 0);
    // The phase carries over: 01 01 from phase 0 -> 3, 6.
    const auto t = map_symbols(from_string("01"), phase);
    CHECK(phase == 3);
    CHECK(std::arg(t[0]) == doctest::Approx(3.0 * std::numbers::pi / 4));
    map_symbols(from_string("01"), phase);
    CHECK(phase == 6);
    CHECK_THROWS_AS(map_symbols(from_string("011"), phase), std::invalid_argument);
}

TEST_CASE("Demodulating the modulator's output gives the bits back at several sample rates")
{
    for (double fs : {36000.0, 72000.0, 180000.0, 500000.0, 1000000.0}) {
        CAPTURE(fs);
        std::mt19937 rng(static_cast<unsigned>(fs));
        Pi4DqpskModulator m(fs);
        const size_t bursts = 4;
        const Bits bits = random_bits(rng, bursts * kSlotBits);
        const uint64_t symbols = bits.size() / 2;
        const std::vector<cf> iq = modulate(m, bits, samples_for(m, symbols));

        double power = 0.0;
        for (const cf& v : iq)
            power += std::norm(v);
        power /= static_cast<double>(iq.size());

        const auto z = ref::matched_symbols(iq, fs, Pi4DqpskModulator::kHalfSpanSymbols, 0, symbols);
        CHECK(ref::differential_decode(z, {1.0, 0.0}) == bits);
        // Raised-cosine pulses: the matched outputs are the unit symbols.
        double worst = 0.0;
        for (size_t k = 0; k < z.size(); ++k)
            worst = std::max(worst, std::fabs(std::abs(z[k]) - 1.0));
        CHECK(worst < 0.01);
        // Average power 1 (ramp-up at the start and padding at the end
        // included, so only roughly here; exactly below).
        CHECK(power == doctest::Approx(1.0).epsilon(0.1));
    }
}

TEST_CASE("Average power is 1 and the bits survive noise at 20 dB SNR")
{
    std::mt19937 rng(7);
    Pi4DqpskModulator m(72000.0);
    const Bits bits = random_bits(rng, 40 * kSlotBits);
    const uint64_t symbols = bits.size() / 2;
    std::vector<cf> iq = modulate(m, bits, samples_for(m, symbols));
    // Power over the steady part (after the ramp-up, before the padding).
    const size_t a = static_cast<size_t>(2 * Pi4DqpskModulator::kHalfSpanSymbols * 4);
    const size_t b = static_cast<size_t>(symbols * 4);
    double power = 0.0;
    for (size_t i = a; i < b; ++i)
        power += std::norm(iq[i]);
    power /= static_cast<double>(b - a);
    CHECK(power == doctest::Approx(1.0).epsilon(0.02));

    // Noise of power 0.01 per sample over the 72 kHz band; the matched
    // filter keeps 18/72 of it, so the symbol SNR is 26 dB.
    std::normal_distribution<float> noise(0.0f, std::sqrt(0.01f / 2.0f));
    for (cf& v : iq)
        v += cf(noise(rng), noise(rng));
    const auto z = ref::matched_symbols(iq, 72000.0, Pi4DqpskModulator::kHalfSpanSymbols, 0, symbols);
    CHECK(ref::differential_decode(z, {1.0, 0.0}) == bits);
}

TEST_CASE("Streaming: burst-wise pushing and any read size give the same samples")
{
    std::mt19937 rng(3);
    std::vector<Bits> bursts;
    Bits all;
    for (int i = 0; i < 6; ++i) {
        bursts.push_back(random_bits(rng, kSlotBits));
        all.insert(all.end(), bursts.back().begin(), bursts.back().end());
    }
    for (double fs : {72000.0, 500000.0}) {
        CAPTURE(fs);
        Pi4DqpskModulator whole(fs);
        whole.push_bits(all);
        const size_t n = whole.available();
        std::vector<cf> a(n);
        whole.read(a.data(), n);

        Pi4DqpskModulator parts(fs);
        std::vector<cf> b;
        const size_t chunks[] = {1, 7, 333, 2, 1000, 64};
        size_t c = 0;
        for (const Bits& burst : bursts) {
            parts.push_bits(burst);
            while (parts.available() > 0) {
                const size_t k = std::min(parts.available(), chunks[c++ % 6]);
                std::vector<cf> tmp(k);
                parts.read(tmp.data(), k);
                b.insert(b.end(), tmp.begin(), tmp.end());
            }
        }
        REQUIRE(b.size() == a.size());
        CHECK(b == a);
        CHECK(parts.samples() == whole.samples());
        CHECK(parts.phase() == whole.phase());
    }
}

TEST_CASE("Phase continuity across bursts")
{
    std::mt19937 rng(5);
    Pi4DqpskModulator m = Pi4DqpskModulator::with_samples_per_symbol(8);
    CHECK(m.sample_rate() == 144000.0);
    int expected_phase = 0;
    Bits all;
    for (int i = 0; i < 8; ++i) {
        const Bits b = random_bits(rng, kSlotBits);
        all.insert(all.end(), b.begin(), b.end());
        expected_phase = (((expected_phase + phase_sum(b, 1, kSlotSymbols)) % 8) + 8) % 8;
        m.push_bits(b);
        CHECK(m.phase() == expected_phase);  // the phase carries over, it is not reset per burst
    }
    const uint64_t symbols = all.size() / 2;
    std::vector<cf> iq(m.available());
    m.read(iq.data(), iq.size());
    // No jump at the burst boundaries: the largest step between samples
    // near a boundary is no larger than inside the bursts.
    const size_t sps = 8;
    const size_t d = Pi4DqpskModulator::kHalfSpanSymbols * sps;
    double inside = 0.0, boundary = 0.0;
    for (size_t i = d + 1; i < (symbols - 1) * sps && i < iq.size(); ++i) {
        const double step = std::abs(iq[i] - iq[i - 1]);
        const size_t pos = (i - d) % (kSlotSymbols * sps);
        const bool near = pos < 2 * sps || pos > (kSlotSymbols - 2) * sps;
        (near ? boundary : inside) = std::max(near ? boundary : inside, step);
    }
    CHECK(boundary > 0.0);
    CHECK(boundary <= inside);
    // And the symbols across each boundary decode.
    // (The last symbols' filter tails are not readable yet.)
    const uint64_t readable = symbols - 2 * Pi4DqpskModulator::kHalfSpanSymbols;
    const auto z = ref::matched_symbols(iq, 144000.0, Pi4DqpskModulator::kHalfSpanSymbols, 0, readable);
    CHECK(ref::differential_decode(z, {1.0, 0.0}) == Bits(all.begin(), all.begin() + 2 * static_cast<long>(readable)));
}

TEST_CASE("Spectrum: occupied bandwidth and power outside the 25 kHz channel")
{
    std::mt19937 rng(9);
    const double fs = 72000.0;
    Pi4DqpskModulator m(fs);
    const Bits bits = random_bits(rng, 100 * kSlotBits);
    std::vector<cf> iq = modulate(m, bits, samples_for(m, bits.size() / 2));
    // Steady part only.
    iq = std::vector<cf>(iq.begin() + 64, iq.begin() + static_cast<long>(bits.size() / 2 * 4));
    const size_t n = 1024;
    const std::vector<double> psd = power_spectrum(iq, n);
    // Bins ordered by frequency -fs/2 .. fs/2.
    std::vector<double> f(n), p(n);
    double total = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const size_t k = (i + n / 2) % n;
        f[i] = (static_cast<double>(i) - static_cast<double>(n / 2)) * fs / static_cast<double>(n);
        p[i] = psd[k];
        total += p[i];
    }
    double cum = 0.0, lo = 0.0, hi = 0.0;
    bool have_lo = false;
    double outside_12k5 = 0.0, outside_15k = 0.0, centre = 0.0;
    for (size_t i = 0; i < n; ++i) {
        cum += p[i];
        if (!have_lo && cum >= 0.005 * total) {
            lo = f[i];
            have_lo = true;
        }
        if (cum <= 0.995 * total)
            hi = f[i];
        if (std::fabs(f[i]) > 12500.0)
            outside_12k5 += p[i];
        if (std::fabs(f[i]) > 15000.0)
            outside_15k += p[i];
        centre += f[i] * p[i];
    }
    const double obw = hi - lo;
    INFO("99 % occupied bandwidth " << obw << " Hz; outside +/-12.5 kHz "
                                       << 10 * std::log10(outside_12k5 / total) << " dB, outside +/-15 kHz "
                                       << 10 * std::log10(outside_15k / total) << " dB");
    // (1 + 0.35) * 18 kHz = 24.3 kHz absolute bandwidth; 99 % lies inside.
    // About 20.9 kHz for roll-off 0.35.
    CHECK(obw > 19500.0);
    CHECK(obw < 22500.0);
    // Truncated pulse: about -50 dB beyond 12.5 kHz, -58 dB beyond 15 kHz.
    CHECK(10 * std::log10(outside_12k5 / total) < -45.0);
    CHECK(10 * std::log10(outside_15k / total) < -52.0);
    CHECK(std::fabs(centre / total) < 200.0);  // centred on 0 Hz
}

TEST_CASE("Modulator refuses rates below the signal bandwidth and reads beyond its symbols")
{
    CHECK_THROWS_AS(Pi4DqpskModulator(24000.0), std::invalid_argument);
    CHECK_THROWS_AS(Pi4DqpskModulator::with_samples_per_symbol(1), std::invalid_argument);
    Pi4DqpskModulator m(72000.0);
    std::vector<cf> out(10);
    // Nothing is readable before the first symbol (sample 0 sits exactly
    // on the boundary and waits for it, see available()).
    CHECK(m.available() == 0);
    CHECK_THROWS_AS(m.read(out.data(), 1), std::logic_error);
    m.push_bits(Bits(2, 0));
    CHECK(m.available() == 4);
    CHECK_THROWS_AS(m.read(out.data(), 5), std::logic_error);
    CHECK_THROWS_AS(Pi4DqpskModulator(72000.0, -1e-6), std::invalid_argument);
    CHECK_THROWS_AS(Pi4DqpskModulator(72000.0, std::nan("")), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(Pi4DqpskModulator(std::numeric_limits<double>::infinity())), std::invalid_argument);
    CHECK_THROWS_AS(m.push_bits(Bits(3, 0)), std::invalid_argument);
}

TEST_CASE("Timing offset: whole samples shift the output, fractions move the symbol centres")
{
    std::mt19937 rng(21);
    const Bits bits = random_bits(rng, 3 * kSlotBits);
    const uint64_t symbols = bits.size() / 2;
    const double fs = 72000.0;
    Pi4DqpskModulator plain(fs);
    const std::vector<cf> a = modulate(plain, bits, samples_for(plain, symbols));

    // 10 samples late: the same samples, 10 zeros first.
    Pi4DqpskModulator late(fs, 10.0 / fs);
    CHECK(late.delay() == doctest::Approx(10.0 / fs));
    const std::vector<cf> b = modulate(late, bits, a.size() + 10);
    for (size_t i = 0; i < 10; ++i)
        CHECK(std::abs(b[i]) < 1e-6);
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(b[i + 10] - a[i])));
    CHECK(worst < 1e-5);

    // 0.3 ms = 5.4 symbols = 21.6 samples: decodes at the shifted centres
    // and not at the nominal ones.
    const double delay = 0.3e-3;
    Pi4DqpskModulator frac(fs, delay);
    const std::vector<cf> c = modulate(frac, bits, samples_for(frac, symbols) + 20);
    const auto z = ref::matched_symbols(c, fs, Pi4DqpskModulator::kHalfSpanSymbols + delay * kSymbolRate, 0, symbols);
    CHECK(ref::differential_decode(z, {1.0, 0.0}) == bits);
    double ring = 0.0;
    for (const auto& v : z)
        ring = std::max(ring, std::fabs(std::abs(v) - 1.0));
    CHECK(ring < 0.01);
    const auto off = ref::matched_symbols(c, fs, Pi4DqpskModulator::kHalfSpanSymbols, 0, symbols);
    CHECK(ref::differential_decode(off, {1.0, 0.0}) != bits);
}

TEST_CASE("Spectrum mask: adjacent-channel power at +/-25 kHz")
{
    // Adjacent channel power in an 18 kHz band (the RRC receive filter's
    // width) centred 25 kHz away, relative to the carrier's power;
    // EN 300 394-1 asks a base station for 60 dB at 25 kHz spacing.
    std::mt19937 rng(23);
    const double fs = 144000.0;
    Pi4DqpskModulator m(fs);
    const Bits bits = random_bits(rng, 100 * kSlotBits);
    std::vector<cf> iq = modulate(m, bits, samples_for(m, bits.size() / 2));
    iq = std::vector<cf>(iq.begin() + 128, iq.begin() + static_cast<long>(bits.size() / 2 * 8));
    const size_t n = 2048;
    const std::vector<double> psd = power_spectrum(iq, n);
    double total = 0.0, upper = 0.0, lower = 0.0;
    for (size_t k = 0; k < n; ++k) {
        const double f = (k < n / 2 ? static_cast<double>(k) : static_cast<double>(k) - static_cast<double>(n)) * fs /
                         static_cast<double>(n);
        total += psd[k];
        if (std::fabs(f - 25000.0) <= 9000.0)
            upper += psd[k];
        if (std::fabs(f + 25000.0) <= 9000.0)
            lower += psd[k];
    }
    const double acp_upper = 10.0 * std::log10(upper / total);
    const double acp_lower = 10.0 * std::log10(lower / total);
    INFO("adjacent channel power " << acp_lower << " / " << acp_upper << " dBc");
    CHECK(acp_upper < -60.0);
    CHECK(acp_lower < -60.0);
}
