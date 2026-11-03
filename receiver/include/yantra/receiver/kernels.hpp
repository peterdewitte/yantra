// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inner loops of the DSP library (RAD-100) with two implementations: plain
// C++ (always built) and SIMD (GCC/Clang vector extensions: SSE2 on
// x86-64, NEON on aarch64; built with YANTRA_SIMD=ON). Every DSP object
// takes an Isa choice, so both paths can run in one program and be
// compared. The paths add in a different order, so results differ by
// float rounding only; the documented tolerances are below and in
// docs/RECEIVER_DSP.md, and tests/receiver checks them.
#pragma once

#include <complex>
#include <cstddef>

namespace yantra::receiver {

using cf = std::complex<float>;

enum class Isa {
    Scalar,  ///< plain C++ loops
    Simd,    ///< vector kernels; the same as Scalar when built without them
};

/// Tolerances between the paths (relative to the sum of magnitudes of the
/// products for dot(); absolute for unit phasors in rotate(); relative per
/// element for accumulate_power()).
inline constexpr double kDotTolerance = 1e-5;
inline constexpr double kRotateTolerance = 1e-5;
inline constexpr std::size_t kRotateMaxBlock = 1024;
inline constexpr double kPowerTolerance = 1e-6;

struct Kernels {
    Isa isa;
    const char* name;
    /// sum_k x[k] * h[k] for complex x and real taps h, the taps given
    /// interleaved (h2[2k] = h2[2k+1] = h[k]); n complex samples.
    cf (*dot)(const cf* x, const float* h2, std::size_t n);
    /// out[k] = in[k] * phasor * step^k for k < n; returns phasor * step^n.
    /// in == out is allowed. The phasor is a recurrence and drifts by float
    /// roundings: kRotateTolerance holds for n <= kRotateMaxBlock (callers
    /// such as Nco restart the phasor from double precision per block).
    cf (*rotate)(const cf* in, cf* out, std::size_t n, cf phasor, cf step);
    /// acc[k] += |x[k]|^2.
    void (*accumulate_power)(const cf* x, float* acc, std::size_t n);
    /// out[k] = x[k] * w[k] for a real window w; in == out is allowed.
    void (*apply_window)(const cf* x, const float* w, cf* out, std::size_t n);
};

/// True when this build has the SIMD kernels (YANTRA_SIMD=ON).
bool simd_built() noexcept;
/// The kernels of `isa`; Isa::Simd gives the scalar kernels when the SIMD
/// ones are not built.
const Kernels& kernels(Isa isa) noexcept;

}  // namespace yantra::receiver
