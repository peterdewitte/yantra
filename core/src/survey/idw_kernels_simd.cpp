// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SIMD squared distances of the point interpolation (IND-41) with GCC/Clang
// vector extensions: 2-double vectors (one SSE2 register on x86-64, one
// NEON register on aarch64), two per round, no intrinsics and no run-time
// dispatch. The operations are those of dist2_scalar(), so the distances
// are the same bit for bit.

#include "idw_kernels.h"

#include <cstring>

namespace yantra::survey::detail {
namespace {

typedef double v2 __attribute__((vector_size(16)));

inline v2 load(const double* p) {
    v2 v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
inline void put(double* p, v2 v) { std::memcpy(p, &v, sizeof v); }

}  // namespace

void dist2_simd(const double* x, const double* y, std::size_t n, double cx, double cy, double* out) {
    const v2 vx = {cx, cx};
    const v2 vy = {cy, cy};
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        const v2 ax = load(x + i) - vx;
        const v2 ay = load(y + i) - vy;
        const v2 bx = load(x + i + 2) - vx;
        const v2 by = load(y + i + 2) - vy;
        put(out + i, ax * ax + ay * ay);
        put(out + i + 2, bx * bx + by * by);
    }
    dist2_scalar(x + i, y + i, n - i, cx, cy, out + i);
}

}  // namespace yantra::survey::detail
