// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inner loops of the ray tracer (IND-40): one ray leg against the wall
// segments of a floor. A bounding-box test picks the candidate segments
// (plain C++, or GCC/Clang vector extensions - SSE2 on x86-64, NEON on
// aarch64 - built with YANTRA_SIMD=ON); the exact crossing test, shared by
// both paths, is building::segment_intersection() followed by the
// end-point rule of building::walls_crossed(). Both paths select the same
// candidates (the box test is exact comparisons with a conservative
// margin), so they give the same levels bit for bit.
//
// Copied from yantra-v2 68ea20a: core/src/raytrace/kernels.h.
#pragma once

#include "yantra/core/building/geometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace yantra::raytrace::detail {

/// Segment boxes are padded to a multiple of kLanes with empty boxes
/// (min +inf, max -inf), which never overlap.
inline constexpr std::size_t kLanes = 4;

/// Margin of the box test, metres: far more than the 1e-12 parameter
/// tolerance of the exact test at building sizes (<= 10 km).
inline constexpr double kBoxMargin = 1e-6;

/// Writes the indices i < n (n a multiple of kLanes) of the segments whose
/// box overlaps the box of leg p0-p1 (grown by kBoxMargin) to `out`, in
/// increasing order; returns their count.
using SelectFn = std::size_t (*)(double p0x, double p0y, double p1x, double p1y, const double* minx,
                                 const double* miny, const double* maxx, const double* maxy, std::size_t n,
                                 std::uint32_t* out);

std::size_t select_scalar(double p0x, double p0y, double p1x, double p1y, const double* minx, const double* miny,
                          const double* maxx, const double* maxy, std::size_t n, std::uint32_t* out);
#if YANTRA_RAYTRACE_SIMD
std::size_t select_simd(double p0x, double p0y, double p1x, double p1y, const double* minx, const double* miny,
                        const double* maxx, const double* maxy, std::size_t n, std::uint32_t* out);
#endif

/// The crossing parameter of leg p0-p1 with segment a-b strictly inside
/// (kCrossingEndEps, 1 - kCrossingEndEps), else -1: the same operations
/// as building::segment_intersection() and the walls_crossed() end rule.
inline double crossing(building::Point2 p0, building::Point2 p1, building::Point2 a, building::Point2 b) {
    auto t = building::segment_intersection(p0, p1, a, b);
    if (!t || *t <= building::kCrossingEndEps || *t >= 1.0 - building::kCrossingEndEps) return -1.0;
    return *t;
}

}  // namespace yantra::raytrace::detail
