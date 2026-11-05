// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/raytrace/kernels.cpp.

#include "kernels.h"

namespace yantra::raytrace::detail {

std::size_t select_scalar(double p0x, double p0y, double p1x, double p1y, const double* minx, const double* miny,
                          const double* maxx, const double* maxy, std::size_t n, std::uint32_t* out) {
    const double lx = std::min(p0x, p1x) - kBoxMargin;
    const double hx = std::max(p0x, p1x) + kBoxMargin;
    const double ly = std::min(p0y, p1y) - kBoxMargin;
    const double hy = std::max(p0y, p1y) + kBoxMargin;
    std::size_t k = 0;
    for (std::size_t i = 0; i < n; ++i) {
        out[k] = static_cast<std::uint32_t>(i);
        k += (maxx[i] >= lx) & (minx[i] <= hx) & (maxy[i] >= ly) & (miny[i] <= hy);
    }
    return k;
}

}  // namespace yantra::raytrace::detail
