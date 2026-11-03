// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FIR design, RRC matched filter, FIR filter, NCO and resampler (RAD-100).

#include "yantra/receiver/fft.hpp"
#include "yantra/receiver/fir.hpp"
#include "yantra/receiver/nco.hpp"
#include "yantra/receiver/resampler.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace yantra::receiver;
using cd = std::complex<double>;

namespace {

// |H(f)| in dB of real taps at frequency f (fraction of the rate).
double response_db(const std::vector<float>& h, double f)
{
    cd s;
    for (std::size_t i = 0; i < h.size(); ++i)
        s += static_cast<double>(h[i]) * std::polar(1.0, -2.0 * std::numbers::pi * f * static_cast<double>(i));
    return 20.0 * std::log10(std::abs(s) + 1e-300);
}

std::vector<cf> tone(std::size_t n, double fs, double f)
{
    std::vector<cf> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = std::polar(1.0f, static_cast<float>(std::fmod(2.0 * std::numbers::pi * f * static_cast<double>(i) / fs,
                                                             2.0 * std::numbers::pi)));
    return x;
}

}  // namespace

TEST_CASE("Kaiser low-pass meets its passband and stopband")
{
    const double fs = 1e6;
    const auto h = kaiser_lowpass(50000.0, 20000.0, 70.0, fs);
    CHECK(h.size() % 2 == 1);
    CHECK(h.size() == kaiser_length(20000.0, 71.5, fs));
    for (std::size_t i = 0; i < h.size(); ++i)
        REQUIRE(h[i] == doctest::Approx(h[h.size() - 1 - i]));
    for (double f = 0.0; f <= 40000.0; f += 1000.0)
        REQUIRE(std::fabs(response_db(h, f / fs)) < 0.01);
    for (double f = 60000.0; f <= 500000.0; f += 1000.0)
        REQUIRE(response_db(h, f / fs) < -70.0);
    CHECK_THROWS_AS(kaiser_lowpass(0.0, 1000.0, 60.0, fs), std::invalid_argument);
    CHECK_THROWS_AS(kaiser_lowpass(600000.0, 1000.0, 60.0, fs), std::invalid_argument);
}

TEST_CASE("RRC pulse: known values, unit energy, raised-cosine pair without ISI, spectrum")
{
    const double a = 0.35;
    CHECK(rrc_value(0.0, a) == doctest::Approx(1.0 - a + 4.0 * a / std::numbers::pi));
    // Continuity through the special point t = 1/(4a).
    const double t0 = 1.0 / (4.0 * a);
    CHECK(rrc_value(t0, a) == doctest::Approx(rrc_value(t0 + 1e-6, a)).epsilon(1e-4));
    CHECK(rrc_value(-t0, a) == doctest::Approx(rrc_value(t0, a)));
    // Unit energy of the continuous pulse (numerical integral over +/-40 T).
    double e = 0.0;
    for (double t = -40.0; t <= 40.0; t += 1e-3)
        e += rrc_value(t, a) * rrc_value(t, a) * 1e-3;
    CHECK(e == doctest::Approx(1.0).epsilon(2e-3));
    CHECK_THROWS_AS(rrc_value(0.0, 0.0), std::invalid_argument);

    for (int sps : {2, 4, 8}) {
        CAPTURE(sps);
        const auto h = rrc_taps(a, sps, 16);
        REQUIRE(h.size() == static_cast<std::size_t>(16 * sps + 1));
        double energy = 0.0;
        for (std::size_t i = 0; i < h.size(); ++i) {
            energy += static_cast<double>(h[i]) * h[i];
            REQUIRE(h[i] == h[h.size() - 1 - i]);
            // Same shape as the continuous pulse.
            REQUIRE(h[i] / h[h.size() / 2] ==
                    doctest::Approx(rrc_value((static_cast<double>(i) - 8.0 * sps) / sps, a) / rrc_value(0.0, a))
                        .epsilon(1e-5));
        }
        CHECK(energy == doctest::Approx(1.0).epsilon(1e-6));
        // Matched pair: 1 at the centre, ~0 at other symbol instants.
        const std::size_t n = h.size();
        std::vector<double> rc(2 * n - 1, 0.0);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                rc[i + j] += static_cast<double>(h[i]) * h[j];
        const std::size_t c = n - 1;
        CHECK(rc[c] == doctest::Approx(1.0).epsilon(1e-6));
        for (int k = 1; k <= 15; ++k)
            REQUIRE(std::fabs(rc[c + static_cast<std::size_t>(k * sps)]) < 3e-3);
        // Spectrum: flat to (1-a)/2, -3 dB at 1/2, stopband beyond (1+a)/2.
        const double fsym = 1.0 / sps;  // symbol rate as a fraction of the sample rate
        const double dc = response_db(h, 0.0);
        CHECK(std::fabs(response_db(h, 0.3 * (1 - a) * fsym) - dc) < 0.1);
        CHECK(response_db(h, 0.5 * fsym) - dc == doctest::Approx(-3.01).epsilon(0.03));
        for (double f = 0.70 * fsym; f < 0.5; f += 0.01 * fsym)
            REQUIRE(response_db(h, f) - dc < -38.0);
    }
    CHECK_THROWS_AS(rrc_taps(a, 0, 16), std::invalid_argument);
    CHECK_THROWS_AS(rrc_taps(a, 4, 15), std::invalid_argument);
}

TEST_CASE("FIR filter: impulse response, decimation phase and streaming in pieces")
{
    const std::vector<float> taps = {0.5f, -1.0f, 2.0f, 0.25f};
    FirFilter f(taps, 1);
    std::vector<cf> x(7, cf{});
    x[0] = cf(1.0f, 2.0f);
    std::vector<cf> y;
    f.process(x.data(), x.size(), y);
    REQUIRE(y.size() == 7);
    for (std::size_t k = 0; k < 4; ++k)
        CHECK(y[k] == cf(1.0f, 2.0f) * taps[k]);
    CHECK(y[4] == cf{});
    CHECK(f.delay() == doctest::Approx(1.5));

    // Decimation by 3: outputs at inputs 0, 3, 6, ... whatever the pieces.
    std::vector<cf> ramp(100);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = cf(static_cast<float>(i), -static_cast<float>(i) * 0.5f);
    FirFilter whole(taps, 3);
    FirFilter parts(taps, 3);
    std::vector<cf> a;
    std::vector<cf> b;
    whole.process(ramp.data(), ramp.size(), a);
    for (std::size_t pos = 0; pos < ramp.size();) {
        const std::size_t n = std::min<std::size_t>(ramp.size() - pos, 1 + pos % 5);
        parts.process(ramp.data() + pos, n, b);
        pos += n;
    }
    REQUIRE(a.size() == 34);
    CHECK(a == b);
    // Output k = sum_j h[j] x[3k - j].
    CHECK(a[10] == cf(0.5f * 30 - 1.0f * 29 + 2.0f * 28 + 0.25f * 27, -0.5f * (0.5f * 30 - 1.0f * 29 + 2.0f * 28 + 0.25f * 27)));
    whole.reset();
    std::vector<cf> c;
    whole.process(ramp.data(), ramp.size(), c);
    CHECK(c == a);
    CHECK_THROWS_AS(FirFilter({}, 1), std::invalid_argument);
    CHECK_THROWS_AS(FirFilter(taps, 0), std::invalid_argument);
}

TEST_CASE("NCO: exact phase over long runs, continuous across calls and frequency changes")
{
    const double fs = 1e6;
    for (Isa isa : {Isa::Scalar, Isa::Simd}) {
        Nco nco(-123456.789, fs, isa);
        const std::size_t n = 3'000'000;
        std::vector<cf> ones(n, cf(1.0f, 0.0f));
        std::vector<cf> out(n);
        // Uneven pieces.
        for (std::size_t pos = 0; pos < n;) {
            const std::size_t m = std::min<std::size_t>(n - pos, 777 + pos % 1500);
            nco.mix(ones.data() + pos, out.data() + pos, m);
            pos += m;
        }
        for (std::size_t i : {std::size_t{0}, std::size_t{1}, std::size_t{1023}, std::size_t{1024}, n / 2, n - 1}) {
            const double cyc = -123456.789 * static_cast<double>(i) / fs;
            REQUIRE(std::abs(cd(out[i]) - std::polar(1.0, 2.0 * std::numbers::pi * (cyc - std::floor(cyc)))) < 3e-5);
        }
        double cyc_end = -123456.789 * static_cast<double>(n) / fs;
        CHECK(nco.phase_cycles() == doctest::Approx(cyc_end - std::floor(cyc_end)).epsilon(1e-9));
        // Frequency change: the phase continues from where it was.
        const double before = nco.phase_cycles();
        nco.set_frequency(1000.0);
        cf one(1.0f, 0.0f);
        cf o;
        nco.mix(&one, &o, 1);
        CHECK(std::abs(cd(o) - std::polar(1.0, 2.0 * std::numbers::pi * before)) < 1e-6);
        CHECK(nco.frequency_hz() == doctest::Approx(1000.0));
        nco.reset(0.25);
        nco.mix(&one, &o, 1);
        CHECK(std::abs(o - cf(0.0f, 1.0f)) < 1e-6f);
    }
    // Scalar: the output does not depend on the call pieces at all.
    Nco a(5000.0, 48000.0, Isa::Scalar);
    Nco b(5000.0, 48000.0, Isa::Scalar);
    std::vector<cf> x(5000, cf(0.5f, -0.5f));
    std::vector<cf> ya(x.size());
    std::vector<cf> yb(x.size());
    a.mix(x.data(), ya.data(), x.size());
    b.mix(x.data(), yb.data(), 1500);
    b.mix(x.data() + 1500, yb.data() + 1500, x.size() - 1500);
    CHECK(ya == yb);
}

TEST_CASE("Resampler: exact and fallback ratios, passband gain, stopband, timing and delay")
{
    struct Case {
        double in;
        double out;
        bool exact;
    };
    for (const Case& c : {Case{200000.0, 72000.0, true}, Case{100000.0, 72000.0, true}, Case{72000.0, 72000.0, true},
                          Case{150001.0, 72000.0, false}}) {
        CAPTURE(c.in);
        ResamplerSpec spec;
        spec.input_rate_hz = c.in;
        spec.output_rate_hz = c.out;
        spec.passband_hz = 12500.0;
        spec.stopband_hz = std::min(20000.0, c.in / 2.0);
        Resampler r(spec);
        CHECK(r.exact() == c.exact);
        if (c.in == 200000.0) {
            CHECK(r.phases() == 9);  // 200/72 = 25/9
        }
        // Passband tone: gain 1 and the right frequency (phase advance per
        // output), from piecewise input.
        for (double f : {-12000.0, -3000.0, 0.0, 7000.0, 12500.0}) {
            CAPTURE(f);
            const auto x = tone(40000, c.in, f);
            std::vector<cf> y;
            Resampler rr(spec);
            for (std::size_t pos = 0; pos < x.size();) {
                const std::size_t m = std::min<std::size_t>(x.size() - pos, 1000 + pos % 333);
                rr.process(x.data() + pos, m, y);
                pos += m;
            }
            REQUIRE(y.size() == static_cast<std::size_t>(std::ceil(40000.0 / r.ratio())));
            const std::size_t skip = r.taps_per_phase() + 10;
            double gain_err = 0.0;
            double phase_err = 0.0;
            for (std::size_t k = skip; k < y.size(); ++k) {
                gain_err = std::max(gain_err, std::fabs(20.0 * std::log10(std::abs(y[k]))));
                // Expected: the tone at input time k * ratio - delay.
                const double t = (static_cast<double>(k) * r.ratio() - r.delay()) / c.in;
                const cd want = std::polar(1.0, 2.0 * std::numbers::pi * f * t);
                phase_err = std::max(phase_err, std::fabs(std::arg(cd(y[k]) * std::conj(want))));
            }
            CHECK(gain_err < 0.02);
            // The fallback's nearest-phase time error (<= 1/512 input sample)
            // shows as phase jitter of up to 2 pi f / (512 fs).
            CHECK(phase_err < (c.exact ? 1e-3 : 2.0 * std::numbers::pi * 12500.0 / (512.0 * c.in) + 1e-3));
        }
        // Stopband tones are at least ~atten_db down.
        if (c.in > 2 * spec.stopband_hz) {
            for (double f = spec.stopband_hz; f < c.in / 2.0; f += 1300.0) {
                for (double sign : {-1.0, 1.0}) {
                    const auto x = tone(20000, c.in, sign * f);
                    std::vector<cf> y;
                    Resampler rr(spec);
                    rr.process(x.data(), x.size(), y);
                    double p = 0.0;
                    for (std::size_t k = r.taps_per_phase(); k < y.size(); ++k)
                        p += std::norm(y[k]);
                    p /= static_cast<double>(y.size() - r.taps_per_phase());
                    REQUIRE(10.0 * std::log10(p) < -70.0);
                }
            }
        }
    }
    ResamplerSpec bad;
    bad.input_rate_hz = 1000.0;
    bad.output_rate_hz = 2000.0;
    bad.passband_hz = 100.0;
    bad.stopband_hz = 200.0;
    CHECK_THROWS_AS(Resampler{bad}, std::invalid_argument);
    bad.output_rate_hz = 500.0;
    bad.stopband_hz = 50.0;
    CHECK_THROWS_AS(Resampler{bad}, std::invalid_argument);
}
