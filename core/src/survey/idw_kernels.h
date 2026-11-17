// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inner loop of the point interpolation (IND-41, internal): the squared
// distances from a cell centre to a run of measured points (structure of
// arrays). Plain C++, or GCC/Clang vector extensions (SSE2 on x86-64, NEON
// on aarch64) built with YANTRA_SIMD=ON. Both compute dx * dx + dy * dy
// with the same IEEE operations and floating-point contraction is off in
// these files, so both give the same distances bit for bit (TST-53).
#pragma once

#include <cstddef>

namespace yantra::survey::detail {

/// out[i] = (x[i] - cx)^2 + (y[i] - cy)^2 for i < n.
using Dist2Fn = void (*)(const double* x, const double* y, std::size_t n, double cx, double cy, double* out);

inline void dist2_scalar(const double* x, const double* y, std::size_t n, double cx, double cy, double* out) {
    for (std::size_t i = 0; i < n; ++i) {
        const double dx = x[i] - cx;
        const double dy = y[i] - cy;
        out[i] = dx * dx + dy * dy;
    }
}

#if YANTRA_IDW_SIMD
void dist2_simd(const double* x, const double* y, std::size_t n, double cx, double cy, double* out);
#endif

}  // namespace yantra::survey::detail
