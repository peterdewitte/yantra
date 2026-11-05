// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SIMD box test with GCC/Clang vector extensions: 2-double vectors (one
// SSE2 register on x86-64, one NEON register on aarch64), two per round,
// no intrinsics and no run-time dispatch. The comparisons are exact, so
// the selected segments are those of select_scalar().
//
// Copied from yantra-v2 68ea20a: core/src/raytrace/kernels_simd.cpp.

#include "kernels.h"

#include <cstring>

namespace yantra::raytrace::detail {
namespace {

typedef double v2 __attribute__((vector_size(16)));
typedef long long m2 __attribute__((vector_size(16)));

inline v2 load(const double* p) {
    v2 v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

}  // namespace

std::size_t select_simd(double p0x, double p0y, double p1x, double p1y, const double* minx, const double* miny,
                        const double* maxx, const double* maxy, std::size_t n, std::uint32_t* out) {
    const double lxs = std::min(p0x, p1x) - kBoxMargin;
    const double hxs = std::max(p0x, p1x) + kBoxMargin;
    const double lys = std::min(p0y, p1y) - kBoxMargin;
    const double hys = std::max(p0y, p1y) + kBoxMargin;
    const v2 lx = {lxs, lxs};
    const v2 hx = {hxs, hxs};
    const v2 ly = {lys, lys};
    const v2 hy = {hys, hys};
    std::size_t k = 0;
    for (std::size_t i = 0; i + kLanes <= n; i += kLanes) {
        const m2 a = (load(maxx + i) >= lx) & (load(minx + i) <= hx) & (load(maxy + i) >= ly) & (load(miny + i) <= hy);
        const m2 b = (load(maxx + i + 2) >= lx) & (load(minx + i + 2) <= hx) & (load(maxy + i + 2) >= ly) &
                     (load(miny + i + 2) <= hy);
        if (((a[0] | a[1]) | (b[0] | b[1])) == 0) continue;
        const std::uint32_t base = static_cast<std::uint32_t>(i);
        out[k] = base;
        k += a[0] != 0;
        out[k] = base + 1;
        k += a[1] != 0;
        out[k] = base + 2;
        k += b[0] != 0;
        out[k] = base + 3;
        k += b[1] != 0;
    }
    return k;
}

}  // namespace yantra::raytrace::detail
