// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FFT (pocketfft, RAD-100): known answers, a direct DFT reference for
// power-of-two, composite and prime sizes, Parseval and the round trip.

#include "yantra/gen/random.hpp"
#include "yantra/receiver/fft.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace yantra::receiver;
using cd = std::complex<double>;
using cf = std::complex<float>;

TEST_CASE("FFT known answers: impulse, DC and a bin-centred tone")
{
    for (std::size_t n : {8u, 210u, 1000u, 4096u}) {
        CAPTURE(n);
        const Fft fft(n);
        CHECK(fft.size() == n);
        std::vector<cf> x(n);
        x[0] = 1.0f;
        fft.forward(x.data());
        for (const cf& v : x)
            REQUIRE(std::abs(v - cf(1.0f)) < 1e-6f);

        std::fill(x.begin(), x.end(), cf(0.5f, -0.25f));
        fft.forward(x.data());
        CHECK(std::abs(x[0] - cf(0.5f, -0.25f) * static_cast<float>(n)) < 1e-4f * static_cast<float>(n));
        for (std::size_t k = 1; k < n; ++k)
            REQUIRE(std::abs(x[k]) < 1e-4f);

        const std::size_t bin = n / 8 + 1;
        for (std::size_t i = 0; i < n; ++i)
            x[i] = std::polar(1.0f, static_cast<float>(2.0 * std::numbers::pi * static_cast<double>(bin * i % n) /
                                                       static_cast<double>(n)));
        fft.forward(x.data());
        for (std::size_t k = 0; k < n; ++k)
            REQUIRE(std::abs(x[k] - cf(k == bin ? static_cast<float>(n) : 0.0f)) < 2e-4f * static_cast<float>(n));
    }
    CHECK_THROWS_AS(Fft(0), std::invalid_argument);
}

TEST_CASE("FFT matches a direct DFT; Parseval and the inverse round trip hold")
{
    for (std::size_t n : {16u, 97u, 360u, 1024u, 1031u}) {
        CAPTURE(n);
        yantra::gen::Rng rng(n);
        std::vector<cf> x(n);
        for (auto& v : x)
            v = {static_cast<float>(rng.gaussian()), static_cast<float>(rng.gaussian())};
        std::vector<cf> X = x;
        const Fft fft(n);
        fft.forward(X.data());

        double err = 0.0;
        double ref_norm = 0.0;
        double time_energy = 0.0;
        double freq_energy = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            cd ref;
            for (std::size_t i = 0; i < n; ++i)
                ref += cd(x[i]) * std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(k * i % n) /
                                                      static_cast<double>(n));
            err += std::norm(cd(X[k]) - ref);
            ref_norm += std::norm(ref);
            time_energy += std::norm(cd(x[k]));
            freq_energy += std::norm(cd(X[k]));
        }
        CHECK(std::sqrt(err / ref_norm) < 1e-6);
        CHECK(freq_energy == doctest::Approx(static_cast<double>(n) * time_energy).epsilon(1e-5));

        fft.inverse(X.data());
        for (std::size_t i = 0; i < n; ++i)
            REQUIRE(std::abs(X[i] / static_cast<float>(n) - x[i]) < 1e-5f);
    }
}
