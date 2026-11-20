// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FFT spectrum (OA-11): tone levels per window, band power of noise and of
// a tone (Parseval), averaging and overlap, streaming in odd chunks, peak
// list and scalar vs SIMD.

#include "yantra/gen/channel.hpp"
#include "yantra/receiver/spectrum.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace yantra::receiver;

namespace {

std::vector<cf> tone(std::size_t n, double fs, double f, double amplitude, double phase = 0.3)
{
    std::vector<cf> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = std::polar(static_cast<float>(amplitude),
                          static_cast<float>(std::fmod(2.0 * std::numbers::pi * f * static_cast<double>(i) / fs + phase,
                                                       2.0 * std::numbers::pi)));
    return x;
}

SpectrumFrame one_frame(const SpectrumConfig& cfg, const std::vector<cf>& x, double fs, double centre = 0.0)
{
    SpectrumAnalyzer a(cfg);
    a.set_stream(centre, fs);
    a.push(x.data(), x.size());
    SpectrumFrame f;
    REQUIRE(a.pop(f));
    return f;
}

}  // namespace

TEST_CASE("A bin-centred tone reads its amplitude in dBFS with every window")
{
    const double fs = 1e6;
    const std::size_t n = 1024;
    for (Window w : {Window::Rectangular, Window::Hann, Window::BlackmanHarris, Window::FlatTop}) {
        CAPTURE(window_name(w));
        SpectrumConfig cfg;
        cfg.fft_size = n;
        cfg.window = w;
        const double f0 = 100.0 * fs / static_cast<double>(n);
        const auto f = one_frame(cfg, tone(n, fs, f0, 0.1), fs, 390e6);
        const std::size_t bin = f.bin_of(f0);
        CHECK(bin == n / 2 + 100);
        CHECK(f.frequency_hz(bin) == doctest::Approx(390e6 + f0));
        CHECK(f.dbfs[bin] == doctest::Approx(-20.0).epsilon(0.0005));
        // The tone's power over its main lobe equals its power.
        CHECK(f.band_power_dbfs(f0 - 6 * f.bin_hz(), f0 + 6 * f.bin_hz()) == doctest::Approx(-20.0).epsilon(0.001));
    }
    // A flat-top window reads an off-centre tone within 0.02 dB.
    SpectrumConfig cfg;
    cfg.fft_size = n;
    cfg.window = Window::FlatTop;
    const double f1 = 200.5 * fs / static_cast<double>(n);
    const auto f = one_frame(cfg, tone(n, fs, f1, 0.5), fs);
    const auto peaks = find_peaks(f, -60.0, 1, 0.0);
    REQUIRE(peaks.size() == 1);
    CHECK(std::fabs(peaks[0].dbfs - 20.0 * std::log10(0.5)) < 0.02);
}

TEST_CASE("Odd FFT sizes keep DC in bin size/2; the DC level is right")
{
    SpectrumConfig cfg;
    cfg.fft_size = 1001;
    cfg.window = Window::Hann;
    const std::vector<cf> dc(1001, cf(0.25f, 0.0f));
    const auto f = one_frame(cfg, dc, 1001.0);
    CHECK(f.bin_of(0.0) == 500);
    CHECK(f.offset_hz(500) == 0.0);
    CHECK(f.offset_hz(0) == doctest::Approx(-500.0));
    CHECK(f.dbfs[500] == doctest::Approx(20.0 * std::log10(0.25)).epsilon(0.0005));
}

TEST_CASE("Band power of white noise over the whole span is its power (Parseval), averaging narrows the spread")
{
    const double fs = 2e6;
    const double power = 1e-3;  // -30 dBFS
    std::vector<cf> x(64 * 2048, cf{});
    yantra::gen::Awgn(power, 7).add(x.data(), x.size());
    for (Window w : {Window::Rectangular, Window::BlackmanHarris}) {
        CAPTURE(window_name(w));
        SpectrumConfig cfg;
        cfg.fft_size = 2048;
        cfg.window = w;
        cfg.averages = 64;
        const auto f = one_frame(cfg, x, fs);
        CHECK(f.frames_averaged == 64);
        CHECK(f.band_power_dbfs(-fs, fs) == doctest::Approx(-30.0).epsilon(0.003));
        // Per bin: noise density plus the window's noise bandwidth; with 64
        // averages the bins scatter by about +/-0.6 dB (1 sigma), so all of
        // them stay within 3 dB of the mean.
        const double expected_bin = -30.0 - 10.0 * std::log10(2048.0) + 10.0 * std::log10(f.enbw_bins);
        for (float v : f.dbfs)
            REQUIRE(std::fabs(v - expected_bin) < 3.0);
    }
    // Rectangular window over one frame: exactly the mean power (Parseval).
    SpectrumConfig cfg;
    cfg.fft_size = 2048;
    cfg.window = Window::Rectangular;
    const std::vector<cf> first(x.begin(), x.begin() + 2048);
    const auto f = one_frame(cfg, first, fs);
    double p = 0.0;
    for (const cf& v : first)
        p += std::norm(v);
    CHECK(f.band_power_dbfs(-fs, fs) == doctest::Approx(10.0 * std::log10(p / 2048.0)).epsilon(1e-4));
}

TEST_CASE("Streaming with overlap gives the same spectra as one push; reset drops partial data")
{
    const double fs = 48000.0;
    auto x = tone(10000, fs, 3000.0, 0.7);
    yantra::gen::Awgn(1e-4, 3).add(x.data(), x.size());
    SpectrumConfig cfg;
    cfg.fft_size = 512;
    cfg.hop = 256;
    cfg.averages = 3;
    SpectrumAnalyzer whole(cfg);
    whole.set_stream(0.0, fs);
    // Frames start every 256 samples: (10000 - 512) / 256 + 1 = 38 frames,
    // 12 spectra of 3.
    CHECK(whole.push(x.data(), x.size()) == 12);
    SpectrumAnalyzer parts(cfg);
    parts.set_stream(0.0, fs);
    for (std::size_t pos = 0; pos < x.size();) {
        const std::size_t n = std::min<std::size_t>(x.size() - pos, 333);
        parts.push(x.data() + pos, n);
        pos += n;
    }
    SpectrumFrame a;
    SpectrumFrame b;
    int count = 0;
    while (whole.pop(a)) {
        REQUIRE(parts.pop(b));
        CHECK(a.first_sample == b.first_sample);
        CHECK(a.first_sample == static_cast<uint64_t>(count) * 3 * 256);
        CHECK(a.dbfs == b.dbfs);
        ++count;
    }
    CHECK(count == 12);
    CHECK_FALSE(parts.pop(b));

    parts.push(x.data(), 700);  // one frame into the next average
    parts.reset();
    parts.push(x.data(), 511);
    CHECK_FALSE(parts.pop(b));

    CHECK_THROWS_AS(SpectrumAnalyzer({1, Window::Hann, 1, 0, Isa::Simd}), std::invalid_argument);
    CHECK_THROWS_AS(SpectrumAnalyzer({64, Window::Hann, 0, 0, Isa::Simd}), std::invalid_argument);
    CHECK_THROWS_AS(SpectrumAnalyzer({64, Window::Hann, 1, 65, Isa::Simd}), std::invalid_argument);
}

TEST_CASE("Peak list: strongest first, interpolated frequency, minimum separation, threshold")
{
    const double fs = 1e6;
    const std::size_t n = 4096;
    auto x = tone(n, fs, 125300.0, 0.3);
    const double f2 = -819.0 * fs / static_cast<double>(n);  // bin-centred, about -200 kHz
    const auto t2 = tone(n, fs, f2, 0.01, 1.1);
    const auto t3 = tone(n, fs, 126500.0, 0.05, 2.0);
    for (std::size_t i = 0; i < n; ++i)
        x[i] += t2[i] + t3[i];
    SpectrumConfig cfg;
    cfg.fft_size = n;
    const auto f = one_frame(cfg, x, fs);
    auto peaks = find_peaks(f, -60.0, 10, 0.0);
    REQUIRE(peaks.size() == 3);
    CHECK(peaks[0].offset_hz == doctest::Approx(125300.0).epsilon(0.1 * f.bin_hz() / 125300.0));
    CHECK(peaks[1].offset_hz == doctest::Approx(126500.0).epsilon(0.1 * f.bin_hz() / 126500.0));
    CHECK(std::fabs(peaks[2].offset_hz - f2) < 0.01 * f.bin_hz());
    CHECK(std::fabs(peaks[2].dbfs + 40.0) < 0.01);
    // 25 kHz minimum separation drops the weaker tone next to the strong one.
    peaks = find_peaks(f, -60.0, 10, 25000.0);
    REQUIRE(peaks.size() == 2);
    CHECK(std::fabs(peaks[1].offset_hz - f2) < 0.01 * f.bin_hz());
    CHECK(find_peaks(f, -30.0, 10, 0.0).size() == 2);
    CHECK(find_peaks(f, -60.0, 1, 0.0).size() == 1);
}

TEST_CASE("Scalar and SIMD spectra agree within 0.001 dB above -100 dBFS")
{
    const double fs = 1e6;
    auto x = tone(8 * 2048, fs, 100000.0, 0.5);
    yantra::gen::Awgn(1e-6, 9).add(x.data(), x.size());
    SpectrumConfig cfg;
    cfg.fft_size = 2048;
    cfg.averages = 8;
    cfg.isa = Isa::Scalar;
    const auto s = one_frame(cfg, x, fs);
    cfg.isa = Isa::Simd;
    const auto v = one_frame(cfg, x, fs);
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s.dbfs[i] > -100.0f)
            REQUIRE(std::fabs(s.dbfs[i] - v.dbfs[i]) < 1e-3);
}
