// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Geometry queries on the building model for the propagation model
// (IND-40, IND-44): the walls of a floor as segments in plan-frame metres,
// the walls a straight path crosses with their attenuation, and the
// segment intersection they use. Copied from yantra-v2 68ea20a:
// core/include/yantra/core/building/geometry.h, adapted (plan-frame
// metres, optional results instead of exceptions for a floor without scale).
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "yantra/core/building/model.h"

namespace yantra::building {

/// One straight wall segment in plan-frame metres.
struct WallSegment {
    Point2 a;
    Point2 b;
    std::size_t wall = 0;     ///< index into Floor::walls
    std::size_t segment = 0;  ///< segment index within the wall's polyline
    double attenuation_db = 0.0;
};

/// All wall segments of floor `f` in plan-frame metres, with the walls'
/// attenuation at `mhz` (Building::wall_attenuation_db). nullopt when the
/// floor has no scale or a wall has an unknown class.
std::optional<std::vector<WallSegment>> wall_segments_m(const Building& b, const Floor& f, double mhz);

/// One crossing of a path with a wall.
struct WallCrossing {
    std::size_t wall = 0;
    std::size_t segment = 0;
    double t = 0.0;  ///< position along the path, 0 (start) .. 1 (end)
    Point2 point;    ///< plan-frame metres
    double attenuation_db = 0.0;
};

struct WallCrossings {
    std::vector<WallCrossing> crossings;  ///< ordered by t
    double total_db = 0.0;
};

/// Tolerance on the path parameter t of the crossing rules below.
inline constexpr double kCrossingEndEps = 1e-9;

/// Where segment p0-p1 meets segment q0-q1: the parameter t along p (0..1)
/// of a proper or touching intersection; nullopt when they do not meet or
/// are parallel (a collinear overlap is no crossing).
std::optional<double> segment_intersection(Point2 p0, Point2 p1, Point2 q0, Point2 q1);

/// The walls the straight path a-b (plan-frame metres on floor `f`)
/// crosses and their total attenuation at `mhz`. Rules: a crossing at a
/// path end point (t within kCrossingEndEps of 0 or 1) does not count; a
/// path through a polyline vertex counts once for that wall; a path along
/// a wall does not cross it; two different walls at the same place both
/// count. nullopt as wall_segments_m().
std::optional<WallCrossings> walls_crossed(const Building& b, const Floor& f, Point2 a, Point2 b_m, double mhz);

}  // namespace yantra::building
