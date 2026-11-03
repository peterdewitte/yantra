// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Channel model (RAD-72, RAD-93): path-loss formulas against
// hand-calculated values, shadowing statistics and correlation, fading
// statistics (power, distribution, Doppler autocorrelation, Rician K), the
// TETRA multipath profiles, thermal noise and AWGN power, frequency
// shift, receiver front-end impairments and sample drops.
#include <doctest/doctest.h>

#include "yantra/gen/channel.hpp"

#include <cmath>
#include <limits>
#include <complex>
#include <numbers>
#include <vector>

using namespace yantra::gen;
using cf = std::complex<float>;

namespace {

double mean_power(const std::vector<cf>& x, size_t from = 0)
{
    double p = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        p += std::norm(std::complex<double>(x[i]));
    return p / static_cast<double>(x.size() - from);
}

// Power of the component at `cycles_per_sample` (single-bin DFT, mean).
double tone_power(const std::vector<cf>& x, double cycles_per_sample)
{
    std::complex<double> acc(0.0, 0.0);
    for (size_t n = 0; n < x.size(); ++n) {
        const double a = -2.0 * std::numbers::pi * cycles_per_sample * static_cast<double>(n);
        acc += std::complex<double>(x[n]) * std::complex<double>(std::cos(a), std::sin(a));
    }
    return std::norm(acc / static_cast<double>(x.size()));
}

std::vector<cf> tone(size_t n, double cycles_per_sample, double amplitude = 1.0)
{
    std::vector<cf> x(n);
    for (size_t i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * cycles_per_sample * static_cast<double>(i);
        x[i] = cf(static_cast<float>(amplitude * std::cos(a)), static_cast<float>(amplitude * std::sin(a)));
    }
    return x;
}

}  // namespace

TEST_CASE("channel: free-space and log-distance path loss")
{
    // 20 log10(4 pi d f / c): 1 m at 390 MHz; 1 km at 1 GHz is the
    // textbook 32.45 + 20 log10(1 km) + 20 log10(1000 MHz) = 92.45 dB.
    CHECK(free_space_path_loss_db(1.0, 390e6) == doctest::Approx(24.2691).epsilon(1e-5));
    CHECK(free_space_path_loss_db(1000.0, 1e9) == doctest::Approx(92.4478).epsilon(1e-5));
    CHECK(free_space_path_loss_db(20.0, 390e6) - free_space_path_loss_db(10.0, 390e6) ==
          doctest::Approx(6.0206).epsilon(1e-4));
    // Below 1 cm the distance is held.
    CHECK(free_space_path_loss_db(0.0, 390e6) == free_space_path_loss_db(0.01, 390e6));

    LogDistanceModel m{3.5, 1.0, 40.0};
    CHECK(m.loss_db(10.0, 390e6) == doctest::Approx(75.0));
    CHECK(m.loss_db(100.0, 390e6) == doctest::Approx(110.0));
    CHECK(m.loss_db(0.5, 390e6) == doctest::Approx(40.0));  // below d0: PL(d0)
    LogDistanceModel fs{2.0, 1.0, std::nullopt};             // free space at d0
    CHECK(fs.loss_db(37.0, 390e6) == doctest::Approx(free_space_path_loss_db(37.0, 390e6)));
    LogDistanceModel d10{3.0, 10.0, std::nullopt};
    CHECK(d10.loss_db(100.0, 390e6) == doctest::Approx(free_space_path_loss_db(10.0, 390e6) + 30.0));
}

TEST_CASE("channel: shadowing has the set sigma, zero mean and exp(-d/L) correlation")
{
    const double sigma = 6.0;
    const double L = 10.0;
    double sum = 0.0, sum2 = 0.0;
    double corr_l = 0.0, corr_2l = 0.0, corr_near = 0.0;
    size_t n = 0;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        ShadowingField f(sigma, L, seed);
        Rng rng(seed + 1000);
        for (int i = 0; i < 1500; ++i) {
            const double x = rng.uniform(-5000.0, 5000.0);
            const double y = rng.uniform(-5000.0, 5000.0);
            const double dir = rng.uniform(0.0, 2.0 * std::numbers::pi);
            const double v = f.value(x, y);
            sum += v;
            sum2 += v * v;
            corr_near += v * f.value(x + 0.1 * L * std::cos(dir), y + 0.1 * L * std::sin(dir));
            corr_l += v * f.value(x + L * std::cos(dir), y + L * std::sin(dir));
            corr_2l += v * f.value(x + 2.0 * L * std::cos(dir), y + 2.0 * L * std::sin(dir));
            ++n;
        }
    }
    const double mean = sum / static_cast<double>(n);
    const double var = sum2 / static_cast<double>(n);
    CHECK(std::fabs(mean) < 0.15);
    CHECK(std::sqrt(var) == doctest::Approx(sigma).epsilon(0.03));
    CHECK(corr_near / static_cast<double>(n) / var == doctest::Approx(std::exp(-0.1)).epsilon(0.04));
    CHECK(corr_l / static_cast<double>(n) / var == doctest::Approx(std::exp(-1.0)).epsilon(0.12));
    CHECK(corr_2l / static_cast<double>(n) / var == doctest::Approx(std::exp(-2.0)).epsilon(0.25));

    // Seeded: the same seed gives the same field, another seed another one.
    ShadowingField a(sigma, L, 7), b(sigma, L, 7), c(sigma, L, 8);
    CHECK(a.value(3.0, 4.0) == b.value(3.0, 4.0));
    CHECK(a.value(3.0, 4.0) != c.value(3.0, 4.0));
    ShadowingField zero(0.0, L, 7);
    CHECK(zero.value(3.0, 4.0) == 0.0);
    CHECK_THROWS_AS(ShadowingField(-1.0, L, 1), std::invalid_argument);
    CHECK_THROWS_AS(ShadowingField(1.0, 0.0, 1), std::invalid_argument);
}

TEST_CASE("channel: Rayleigh fading power, distribution and Doppler autocorrelation")
{
    const double fd = 10.0;
    const double dt = 0.004;  // 25 samples per 1/fd
    double p = 0.0, below = 0.0, r01 = 0.0, r_zero = 0.0;
    const double tau01 = 0.1 / fd;                       // J0(0.2 pi) = 0.9037
    const double tau_zero = 2.404825557695773 / (2.0 * std::numbers::pi * fd);  // J0 = 0
    size_t n = 0;
    for (uint64_t seed = 1; seed <= 16; ++seed) {
        FadingGenerator g({FadingType::rayleigh, 0.0, 32}, fd, seed);
        for (int i = 0; i < 20000; ++i) {
            const double t = dt * i;
            const auto v = g.gain(t);
            p += std::norm(v);
            below += std::norm(v) < 0.1 ? 1.0 : 0.0;
            r01 += std::real(v * std::conj(g.gain(t + tau01)));
            r_zero += std::real(v * std::conj(g.gain(t + tau_zero)));
            ++n;
        }
    }
    const double dn = static_cast<double>(n);
    CHECK(p / dn == doctest::Approx(1.0).epsilon(0.05));
    // Rayleigh: P(|g|^2 < x) = 1 - exp(-x).
    CHECK(below / dn == doctest::Approx(1.0 - std::exp(-0.1)).epsilon(0.12));
    // Clarke: R(tau) = J0(2 pi fd tau).
    CHECK(r01 / p == doctest::Approx(std::cyl_bessel_j(0.0, 2.0 * std::numbers::pi * fd * tau01)).epsilon(0.05));
    CHECK(std::fabs(r_zero / p) < 0.06);

    // No Doppler: a constant gain; none: always 1.
    FadingGenerator still({FadingType::rayleigh, 0.0, 32}, 0.0, 3);
    CHECK(still.gain(0.0) == still.gain(12.5));
    FadingGenerator none({FadingType::none, 0.0, 32}, 5.0, 3);
    CHECK(none.gain(1.0) == std::complex<double>(1.0, 0.0));
    CHECK_THROWS_AS(FadingGenerator({FadingType::rayleigh, 0.0, 32}, -1.0, 1), std::invalid_argument);
    CHECK(fading_type_from_string("rician") == FadingType::rician);
    CHECK_THROWS_AS(fading_type_from_string("nakagami"), std::invalid_argument);
    CHECK(doppler_hz(50.0 / 3.6, 390e6) == doctest::Approx(18.07).epsilon(1e-3));
}

TEST_CASE("channel: Rician fading has its K factor")
{
    for (double k_db : {0.0, 6.0, 10.0}) {
        double m1 = 0.0, m2 = 0.0;
        size_t n = 0;
        for (uint64_t seed = 1; seed <= 16; ++seed) {
            FadingGenerator g({FadingType::rician, k_db, 32}, 8.0, seed);
            for (int i = 0; i < 20000; ++i) {
                const double v = std::norm(g.gain(0.005 * i));
                m1 += v;
                m2 += v * v;
                ++n;
            }
        }
        m1 /= static_cast<double>(n);
        m2 /= static_cast<double>(n);
        // Moment estimator: gamma = var(|g|^2) / mean^2 = (1 + 2K) / (1 + K)^2.
        const double gamma = (m2 - m1 * m1) / (m1 * m1);
        const double s = std::sqrt(std::max(0.0, 1.0 - gamma));
        const double k = s / (1.0 - s);
        INFO("K = " << k_db << " dB, estimated " << 10.0 * std::log10(k));
        CHECK(m1 == doctest::Approx(1.0).epsilon(0.05));
        CHECK(std::fabs(10.0 * std::log10(k) - k_db) < 1.0);
    }
}

TEST_CASE("channel: TETRA propagation conditions (static, TU50, HT200)")
{
    const Propagation tu = propagation("TU50");
    REQUIRE(tu.taps.size() == 2);
    CHECK(tu.taps[1].delay_us == 5.0);
    CHECK(tu.taps[1].power_db == -22.3);
    CHECK(tu.speed_kmh == 50.0);
    const Propagation ht = propagation("ht200");
    CHECK(ht.name == "HT200");
    CHECK(ht.taps[1].delay_us == 15.0);
    CHECK(ht.taps[1].power_db == -8.6);
    CHECK_THROWS_AS(propagation("RA250"), std::invalid_argument);

    // Static: the identity.
    MultipathChannel st(propagation("static"), 500e3, 390e6, 1);
    CHECK(st.identity());
    std::vector<cf> x = tone(64, 0.01);
    const std::vector<cf> x0 = x;
    st.apply(x.data(), x.size());
    CHECK(x == x0);

    // HT200 at 1 Msps: 15 us is 15 whole samples, so an impulse comes out
    // at exactly kHalfLength and kHalfLength + 15.
    MultipathChannel h(ht, 1e6, 390e6, 5);
    CHECK(h.doppler_hz() == doctest::Approx(200.0 / 3.6 / kSpeedOfLight * 390e6));
    std::vector<cf> imp(64, cf(0.0f, 0.0f));
    imp[0] = cf(1.0f, 0.0f);
    h.apply(imp.data(), imp.size());
    for (size_t i = 0; i < imp.size(); ++i) {
        if (i == 16 || i == 31)
            CHECK(std::abs(imp[i]) > 0.0f);
        else
            CHECK(std::abs(imp[i]) < 1e-6f);
    }

    // Mean power 1 over many fades, and a stream gives the same samples
    // in any block size.
    for (const char* name : {"TU50", "HT200"}) {
        double p = 0.0;
        int runs = 0;
        for (uint64_t seed = 1; seed <= 12; ++seed) {
            MultipathChannel ch(propagation(name), 36e3, 390e6, seed);
            std::vector<cf> y = tone(36000, 0.05);
            ch.apply(y.data(), y.size());
            p += mean_power(y, 64);
            ++runs;
        }
        INFO(name);
        CHECK(p / runs == doctest::Approx(1.0).epsilon(0.15));
    }
    MultipathChannel a(propagation("TU50"), 72e3, 390e6, 9), b(propagation("TU50"), 72e3, 390e6, 9);
    std::vector<cf> ya = tone(5000, 0.03), yb = ya;
    a.apply(ya.data(), ya.size());
    for (size_t at = 0; at < yb.size(); at += 333)
        b.apply(yb.data() + at, std::min<size_t>(333, yb.size() - at));
    CHECK(ya == yb);
}

TEST_CASE("channel: thermal noise and AWGN power")
{
    CHECK(thermal_noise_dbm(1.0, 0.0) == doctest::Approx(-173.975).epsilon(1e-5));
    CHECK(thermal_noise_dbm(18000.0, 0.0) == doctest::Approx(-131.422).epsilon(1e-5));
    CHECK(thermal_noise_dbm(500000.0, 7.0) == doctest::Approx(-173.975 + 56.990 + 7.0).epsilon(1e-5));
    CHECK(dbm_to_power(-80.0, -20.0) == doctest::Approx(1e-6));
    CHECK(power_to_dbm(1e-6, -20.0) == doctest::Approx(-80.0));

    Awgn noise(0.01, 42);
    std::vector<cf> x(400000, cf(0.0f, 0.0f));
    noise.add(x.data(), x.size());
    CHECK(std::fabs(10.0 * std::log10(mean_power(x) / 0.01)) < 0.03);
    double mi = 0.0, mq = 0.0, pi = 0.0, pq = 0.0;
    for (const cf& v : x) {
        mi += v.real();
        mq += v.imag();
        pi += v.real() * v.real();
        pq += v.imag() * v.imag();
    }
    CHECK(std::fabs(mi / x.size()) < 5e-4);
    CHECK(std::fabs(mq / x.size()) < 5e-4);
    CHECK(pi / pq == doctest::Approx(1.0).epsilon(0.01));  // circular
    Awgn again(0.01, 42);
    CHECK(again.sample() == Awgn(0.01, 42).sample());
    CHECK_THROWS_AS(Awgn(-1.0, 1), std::invalid_argument);
}

TEST_CASE("channel: frequency shift (CFO, Doppler shift)")
{
    CHECK(ppm_to_hz(2.0, 390e6) == doctest::Approx(780.0));
    const double fs = 48000.0;
    std::vector<cf> x(4800, cf(1.0f, 0.0f));
    FrequencyShifter s(1000.0, fs);
    s.apply(x.data(), 1000);
    s.apply(x.data() + 1000, x.size() - 1000);  // continuous across calls
    for (size_t n : {0u, 1u, 999u, 1000u, 4799u}) {
        const double a = 2.0 * std::numbers::pi * 1000.0 * static_cast<double>(n) / fs;
        CHECK(x[n].real() == doctest::Approx(std::cos(a)).epsilon(1e-5));
        CHECK(x[n].imag() == doctest::Approx(std::sin(a)).epsilon(1e-5));
    }
    CHECK(tone_power(x, 1000.0 / fs) == doctest::Approx(1.0).epsilon(1e-4));
}

TEST_CASE("channel: receiver front end (gain, IQ imbalance, swap, DC, clipping, 8-bit)")
{
    // Gain step.
    {
        FrontEnd fe({.gain_db = 6.0});
        std::vector<cf> x = tone(100, 0.01, 0.1);
        fe.apply(x.data(), x.size());
        CHECK(mean_power(x) == doctest::Approx(0.01 * std::pow(10.0, 0.6)).epsilon(1e-4));
        fe.set_gain_db(-6.0);
        std::vector<cf> y = tone(100, 0.01, 0.1);
        fe.apply(y.data(), y.size());
        CHECK(mean_power(y) == doctest::Approx(0.01 * std::pow(10.0, -0.6)).epsilon(1e-4));
    }
    // IQ imbalance: the image of a tone at -f is down by the image
    // rejection ratio |mu|^2 / |nu|^2.
    {
        FrontEnd fe({.iq_gain_db = 1.0, .iq_phase_deg = 3.0});
        const double g = std::pow(10.0, 1.0 / 20.0);
        const std::complex<double> e = std::polar(g, 3.0 * std::numbers::pi / 180.0);
        const double irr = 10.0 * std::log10(std::norm(1.0 + std::conj(e)) / std::norm(1.0 - e));
        CHECK(fe.image_rejection_db() == doctest::Approx(irr));
        std::vector<cf> x = tone(10000, 0.1);
        fe.apply(x.data(), x.size());
        const double main = tone_power(x, 0.1), image = tone_power(x, -0.1);
        CHECK(10.0 * std::log10(main / image) == doctest::Approx(irr).epsilon(0.002));
        // Only Q changes: I passes untouched.
        const std::vector<cf> ref = tone(10000, 0.1);
        CHECK(x[123].real() == ref[123].real());
        CHECK(FrontEnd({}).image_rejection_db() == std::numeric_limits<double>::infinity());
    }
    // Swapped I/Q and DC spike.
    {
        FrontEnd fe({.swap_iq = true, .dc = {0.01, -0.02}});
        std::vector<cf> x{cf(0.5f, -0.25f)};
        fe.apply(x.data(), 1);
        CHECK(x[0].real() == doctest::Approx(-0.25 + 0.01));
        CHECK(x[0].imag() == doctest::Approx(0.5 - 0.02));
    }
    // Clipping at full scale.
    {
        FrontEnd fe({.full_scale = 0.5, .clip = true});
        std::vector<cf> x{cf(0.2f, 0.1f), cf(0.7f, 0.0f), cf(0.0f, -0.9f), cf(-0.5f, 0.1f)};
        fe.apply(x.data(), x.size());
        CHECK(x[0] == cf(0.2f, 0.1f));
        CHECK(x[1] == cf(0.5f, 0.0f));
        CHECK(x[2] == cf(0.0f, -0.5f));
        CHECK(fe.clipped() == 3);  // at or beyond full scale
    }
    // 8-bit quantisation: steps of full_scale / 128, codes -128..127.
    {
        FrontEnd fe({.full_scale = 1.0, .quantise_bits = 8});
        std::vector<cf> x = tone(5000, 0.0123, 0.7);
        const std::vector<cf> ref = x;
        fe.apply(x.data(), x.size());
        const double step = 1.0 / 128.0;
        double err = 0.0;
        for (size_t i = 0; i < x.size(); ++i) {
            const double ci = x[i].real() / step, cq = x[i].imag() / step;
            CHECK(ci == std::round(ci));
            CHECK(cq == std::round(cq));
            CHECK(std::fabs(x[i].real() - ref[i].real()) <= step / 2 + 1e-7);
            err += std::norm(std::complex<double>(x[i]) - std::complex<double>(ref[i]));
        }
        // Uniform quantisation noise: step^2 / 12 per component.
        CHECK(err / x.size() == doctest::Approx(2.0 * step * step / 12.0).epsilon(0.05));
        CHECK(fe.clipped() == 0);
        std::vector<cf> big{cf(2.0f, -2.0f)};
        fe.apply(big.data(), 1);
        CHECK(big[0] == cf(127.0f / 128.0f, -1.0f));
        CHECK(fe.clipped() == 1);
    }
    CHECK_THROWS_AS(FrontEnd({.full_scale = 0.0}), std::invalid_argument);
    CHECK_THROWS_AS(FrontEnd({.quantise_bits = 1}), std::invalid_argument);
}

TEST_CASE("channel: sample drops")
{
    SampleDropper d({.first = 10, .interval = 100, .length = 5});
    std::vector<cf> in(1000);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = cf(static_cast<float>(i), 0.0f);
    std::vector<cf> out(in.size());
    size_t m = 0;
    for (size_t at = 0; at < in.size(); at += 37)
        m += d.process(in.data() + at, std::min<size_t>(37, in.size() - at), out.data() + m);
    CHECK(m == 950);
    CHECK(d.dropped_total() == 50);
    CHECK(out[9].real() == 9.0f);
    CHECK(out[10].real() == 15.0f);  // 10..14 dropped
    CHECK(d.dropped(110));
    CHECK_FALSE(d.dropped(115));
    CHECK(d.dropped_before(10) == 0);
    CHECK(d.dropped_before(13) == 3);
    CHECK(d.dropped_before(200) == 10);
    SampleDropper none({});
    CHECK(none.process(in.data(), in.size(), out.data()) == in.size());
    CHECK_THROWS_AS(SampleDropper({.first = 0, .interval = 5, .length = 5}), std::invalid_argument);
}
