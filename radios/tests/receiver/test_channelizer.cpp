// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Channelizer (RAD-100) with tones: passband flatness, stopband and
// adjacent-channel rejection, any channel list, output rate, group delay,
// reset, scalar vs SIMD. The parameters are generic (25 kHz raster-like
// numbers given here); TETRA carriers are tested in tests/dsp_tetra.

#include "yantra/receiver/channelizer.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace yantra::receiver;
using cd = std::complex<double>;

namespace {

ChannelizerConfig config(double fs, Isa isa = Isa::Simd)
{
    ChannelizerConfig c;
    c.input_rate_hz = fs;
    c.output_rate_hz = 72000.0;
    c.passband_hz = 15000.0;
    c.stopband_hz = 22000.0;
    c.atten_db = 70.0;
    c.isa = isa;
    return c;
}

void add_tone(std::vector<cf>& x, double fs, double f, double amplitude)
{
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] += std::polar(static_cast<float>(amplitude),
                           static_cast<float>(std::fmod(2.0 * std::numbers::pi * f * static_cast<double>(i) / fs,
                                                        2.0 * std::numbers::pi)));
}

// Mean power (dB) of the outputs after the filters have filled.
double out_power_db(const std::vector<cf>& y, std::size_t skip)
{
    double p = 0.0;
    for (std::size_t k = skip; k < y.size(); ++k)
        p += std::norm(cd(y[k]));
    return 10.0 * std::log10(p / static_cast<double>(y.size() - skip) + 1e-300);
}

std::vector<std::vector<cf>> run(Channelizer& ch, const std::vector<cf>& x, std::size_t piece = 4096)
{
    std::vector<std::vector<cf>> out;
    for (std::size_t pos = 0; pos < x.size();) {
        const std::size_t n = std::min(x.size() - pos, piece);
        ch.process(x.data() + pos, n, out);
        pos += n;
    }
    return out;
}

}  // namespace

TEST_CASE("Channelizer stages and output rate for common capture rates")
{
    for (double fs : {72000.0, 100000.0, 500000.0, 1e6, 2.048e6, 2.4e6, 1234567.0}) {
        CAPTURE(fs);
        Channelizer ch(config(fs), {0.0});
        CHECK(ch.decimation() == (fs >= 4 * 72000.0 ? static_cast<std::size_t>(std::floor(fs / 144000.0)) : 1u));
        const std::vector<cf> x(static_cast<std::size_t>(fs / 10), cf(0.1f, 0.0f));  // 0.1 s
        const auto out = run(ch, x, 1000);
        CHECK(std::fabs(static_cast<double>(out[0].size()) - 7200.0) <= 1.0);
        // Rates where the second stage is exact (no fallback phases).
        if (fs != 1234567.0)
            CHECK(ch.stage_b().exact());
    }
}

TEST_CASE("Passband is flat and stopband/adjacent channels are rejected by >= 70 dB")
{
    for (double fs : {1e6, 2.4e6, 1234567.0, 100000.0}) {
        CAPTURE(fs);
        const double centre = fs >= 1e6 ? 125000.0 : 0.0;
        Channelizer probe(config(fs), {centre});
        const std::size_t n = static_cast<std::size_t>(fs * 0.05);
        const std::size_t skip = 300;
        for (double d = -15000.0; d <= 15000.0; d += 2500.0) {
            CAPTURE(d);
            std::vector<cf> x(n);
            add_tone(x, fs, centre + d, 0.5);
            Channelizer ch(config(fs), {centre});
            const auto out = run(ch, x);
            REQUIRE(std::fabs(out_power_db(out[0], skip) - 20.0 * std::log10(0.5)) < 0.05);
            // The tone appears at its offset from the channel centre.
            const std::size_t k = out[0].size() - 10;
            const double turn = std::arg(cd(out[0][k]) * std::conj(cd(out[0][k - 1])));
            // The fallback resampler's nearest-phase time error (<= half of
            // 1/256 of a stage-B input sample) adds phase jitter.
            const double rate_b = fs / static_cast<double>(ch.decimation());
            const double jitter = ch.stage_b().exact() ? 0.0
                                                       : 2.0 * 2.0 * std::numbers::pi * std::fabs(d) /
                                                             (2.0 * Resampler::kFallbackPhases * rate_b);
            REQUIRE(std::fabs(turn - 2.0 * std::numbers::pi * d / 72000.0) < 1e-4 + jitter);
        }
        // Everything from the stopband on, over the whole capture, including
        // the adjacent channels at +/-25 kHz and +/-50 kHz.
        std::vector<double> stop;
        for (double d = 22000.0; d < fs / 2.0 + std::fabs(centre); d += fs / 97.0)
            stop.push_back(d);
        for (double d : {22000.0, 25000.0, 37150.0, 50000.0})
            stop.push_back(d);
        for (double d : stop) {
            for (double sign : {-1.0, 1.0}) {
                const double f = centre + sign * d;
                if (std::fabs(f) >= fs / 2.0)
                    continue;
                CAPTURE(f);
                std::vector<cf> x(n);
                add_tone(x, fs, f, 1.0);
                Channelizer ch(config(fs), {centre});
                const auto out = run(ch, x);
                REQUIRE(out_power_db(out[0], skip) < -70.0);
            }
        }
    }
}

TEST_CASE("Any list of channel centres, each sees only its own signal")
{
    const double fs = 1e6;
    const std::vector<double> offsets = {-312500.0, -25000.0, 0.0, 37500.0, 400000.0};
    std::vector<cf> x(200000);
    for (std::size_t i = 0; i < offsets.size(); ++i)
        add_tone(x, fs, offsets[i] + 1000.0, std::pow(10.0, -0.5 * static_cast<double>(i)));  // 0, -10, ... -40 dB
    Channelizer ch(config(fs), offsets);
    CHECK(ch.channels() == offsets.size());
    CHECK(ch.offset_hz(3) == 37500.0);
    const auto out = run(ch, x, 3333);
    REQUIRE(out.size() == offsets.size());
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        CAPTURE(i);
        CHECK(out[i].size() == out[0].size());
        CHECK(out_power_db(out[i], 300) == doctest::Approx(-10.0 * static_cast<double>(i)).epsilon(0.01).scale(1.0));
    }
    CHECK(channel_offsets({390.0e6, 390.025e6}, 390.0125e6) == std::vector<double>{-12500.0, 12500.0});
}

TEST_CASE("Group delay: a pulse comes out delay_seconds() later")
{
    for (double fs : {1e6, 100000.0}) {
        CAPTURE(fs);
        Channelizer ch(config(fs), {50000.0 * (fs >= 1e6)});
        const double t0 = 0.01;      // pulse centre
        const double width = 2e-4;   // Gaussian envelope, about 2 kHz wide
        std::vector<cf> x(static_cast<std::size_t>(0.03 * fs));
        for (std::size_t i = 0; i < x.size(); ++i) {
            const double t = static_cast<double>(i) / fs;
            const double env = std::exp(-0.5 * std::pow((t - t0) / width, 2));
            x[i] = std::polar(static_cast<float>(env),
                              static_cast<float>(2.0 * std::numbers::pi * ch.offset_hz(0) * t));
        }
        const auto out = run(ch, x);
        std::size_t best = 0;
        for (std::size_t k = 1; k < out[0].size(); ++k)
            if (std::abs(out[0][k]) > std::abs(out[0][best]))
                best = k;
        const double t_out = static_cast<double>(best) / 72000.0 - ch.delay_seconds();
        CHECK(std::fabs(t_out - t0) <= 1.0 / 72000.0);
        CHECK(std::abs(out[0][best]) == doctest::Approx(1.0).epsilon(0.01));
    }
}

TEST_CASE("Reset gives the same output as a new channelizer; scalar and SIMD agree")
{
    const double fs = 1e6;
    std::vector<cf> x(100000);
    add_tone(x, fs, 101000.0, 0.7);
    add_tone(x, fs, 124000.0, 0.2);
    Channelizer simd(config(fs, Isa::Simd), {100000.0, 125000.0});
    Channelizer scalar(config(fs, Isa::Scalar), {100000.0, 125000.0});
    const auto a = run(simd, x, 777);
    const auto b = run(scalar, x, 1234);
    simd.reset();
    const auto c = run(simd, x, 777);
    for (std::size_t i = 0; i < 2; ++i) {
        REQUIRE(a[i].size() == b[i].size());
        CHECK(a[i] == c[i]);
        double err = 0.0;
        for (std::size_t k = 0; k < a[i].size(); ++k)
            err = std::max(err, static_cast<double>(std::abs(a[i][k] - b[i][k])));
        // Different summation order and NCO recurrence lanes only.
        CHECK(err < 2e-5);
    }
}

TEST_CASE("Channelizer refuses configurations that cannot work")
{
    CHECK_THROWS_AS(Channelizer(config(50000.0), {0.0}), std::invalid_argument);  // output above input
    auto c = config(1e6);
    c.stopband_hz = 60000.0;  // aliases into the passband at 72 kHz
    CHECK_THROWS_AS(Channelizer(c, {0.0}), std::invalid_argument);
    c = config(1e6);
    c.passband_hz = 25000.0;  // passband above stopband
    CHECK_THROWS_AS(Channelizer(c, {0.0}), std::invalid_argument);
    CHECK_THROWS_AS(Channelizer(config(1e6), {490000.0}), std::invalid_argument);
    CHECK_NOTHROW(Channelizer(config(1e6), {}));
}
