// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plain C++ kernels (the fallback path, always built).

#include "kernels_internal.hpp"

namespace yantra::receiver {
namespace {

cf dot_scalar(const cf* x, const float* h2, std::size_t n)
{
    float re = 0.0f;
    float im = 0.0f;
    for (std::size_t k = 0; k < n; ++k) {
        re += x[k].real() * h2[2 * k];
        im += x[k].imag() * h2[2 * k + 1];
    }
    return {re, im};
}

cf rotate_scalar(const cf* in, cf* out, std::size_t n, cf phasor, cf step)
{
    for (std::size_t k = 0; k < n; ++k) {
        out[k] = in[k] * phasor;
        phasor *= step;
    }
    return phasor;
}

void accumulate_power_scalar(const cf* x, float* acc, std::size_t n)
{
    for (std::size_t k = 0; k < n; ++k)
        acc[k] += x[k].real() * x[k].real() + x[k].imag() * x[k].imag();
}

void apply_window_scalar(const cf* x, const float* w, cf* out, std::size_t n)
{
    for (std::size_t k = 0; k < n; ++k)
        out[k] = x[k] * w[k];
}

}  // namespace

namespace detail {
const Kernels kScalarKernels{Isa::Scalar, "scalar", dot_scalar, rotate_scalar, accumulate_power_scalar,
                             apply_window_scalar};
}  // namespace detail

bool simd_built() noexcept { return YANTRA_RECEIVER_SIMD != 0; }

const Kernels& kernels(Isa isa) noexcept
{
#if YANTRA_RECEIVER_SIMD
    if (isa == Isa::Simd)
        return detail::kSimdKernels;
#else
    (void)isa;
#endif
    return detail::kScalarKernels;
}

}  // namespace yantra::receiver
