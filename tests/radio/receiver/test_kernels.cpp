// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DSP kernels (RAD-100): the scalar and SIMD paths against a double
// precision reference and against each other, within the documented
// tolerances (kernels.h, docs/DSP.md), for lengths that hit every tail.

#include "yantra/gen/random.hpp"
#include "yantra/receiver/kernels.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <vector>

using namespace yantra::receiver;

namespace {

std::vector<cf> random_samples(std::size_t n, uint64_t seed)
{
    yantra::gen::Rng rng(seed);
    std::vector<cf> x(n);
    for (auto& v : x)
        v = {static_cast<float>(rng.uniform(-1, 1)), static_cast<float>(rng.uniform(-1, 1))};
    return x;
}

const std::size_t kLengths[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 63, 64, 65, 1000, 4096};

}  // namespace

TEST_CASE("The SIMD kernels are built exactly when YANTRA_SIMD is on")
{
    CHECK(simd_built() == (YANTRA_TEST_EXPECT_SIMD != 0));
    CHECK(kernels(Isa::Scalar).isa == Isa::Scalar);
    CHECK(kernels(Isa::Simd).isa == (simd_built() ? Isa::Simd : Isa::Scalar));
}

TEST_CASE("dot: complex samples with real taps match a double reference on both paths")
{
    for (std::size_t n : kLengths) {
        CAPTURE(n);
        const auto x = random_samples(n, 11 + n);
        yantra::gen::Rng rng(99 + n);
        std::vector<float> h2(2 * n);
        std::complex<double> ref;
        double mag = 1e-30;
        for (std::size_t k = 0; k < n; ++k) {
            const float h = static_cast<float>(rng.uniform(-1, 1));
            h2[2 * k] = h2[2 * k + 1] = h;
            ref += std::complex<double>(x[k]) * static_cast<double>(h);
            mag += std::abs(std::complex<double>(x[k])) * std::fabs(h);
        }
        const cf s = kernels(Isa::Scalar).dot(x.data(), h2.data(), n);
        const cf v = kernels(Isa::Simd).dot(x.data(), h2.data(), n);
        CHECK(std::abs(std::complex<double>(s) - ref) <= kDotTolerance * mag);
        CHECK(std::abs(std::complex<double>(v) - ref) <= kDotTolerance * mag);
        CHECK(std::abs(std::complex<double>(v) - std::complex<double>(s)) <= kDotTolerance * mag);
    }
}

TEST_CASE("rotate: samples turned by a phasor recurrence, in place or not, on both paths")
{
    for (std::size_t n : kLengths) {
        if (n > kRotateMaxBlock)
            continue;
        CAPTURE(n);
        const auto x = random_samples(n, 5 + n);
        const double w = 0.0123456;  // rad per sample
        const double p0 = 0.7;
        const cf phasor(static_cast<float>(std::cos(p0)), static_cast<float>(std::sin(p0)));
        const cf step(static_cast<float>(std::cos(w)), static_cast<float>(std::sin(w)));
        for (Isa isa : {Isa::Scalar, Isa::Simd}) {
            std::vector<cf> out(n);
            const cf end = kernels(isa).rotate(x.data(), out.data(), n, phasor, step);
            std::vector<cf> inplace = x;
            kernels(isa).rotate(inplace.data(), inplace.data(), n, phasor, step);
            for (std::size_t k = 0; k < n; ++k) {
                // Against exact powers of the (float) step given.
                const auto ref = std::complex<double>(x[k]) * std::complex<double>(phasor) *
                                 std::pow(std::complex<double>(step), static_cast<double>(k));
                REQUIRE(std::abs(std::complex<double>(out[k]) - ref) <= kRotateTolerance * std::abs(ref) + 1e-7);
                REQUIRE(inplace[k] == out[k]);
            }
            CHECK(std::abs(std::complex<double>(end) -
                           std::complex<double>(phasor) * std::pow(std::complex<double>(step), static_cast<double>(n))) <=
                  kRotateTolerance);
        }
    }
}

TEST_CASE("accumulate_power and apply_window match the reference on both paths")
{
    for (std::size_t n : kLengths) {
        CAPTURE(n);
        const auto x = random_samples(n, 77 + n);
        std::vector<float> w(n);
        for (std::size_t k = 0; k < n; ++k)
            w[k] = 0.5f + 0.25f * static_cast<float>(k % 3);
        for (Isa isa : {Isa::Scalar, Isa::Simd}) {
            std::vector<float> acc(n, 1.0f);
            kernels(isa).accumulate_power(x.data(), acc.data(), n);
            std::vector<cf> out(n);
            kernels(isa).apply_window(x.data(), w.data(), out.data(), n);
            for (std::size_t k = 0; k < n; ++k) {
                const double p = 1.0 + std::norm(std::complex<double>(x[k]));
                REQUIRE(std::fabs(acc[k] - p) <= kPowerTolerance * p);
                REQUIRE(out[k] == x[k] * w[k]);
            }
        }
    }
}
