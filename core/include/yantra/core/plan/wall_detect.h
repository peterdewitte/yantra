// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Walls proposed from the plan image (IND-11, priority S): dark straight
// lines on a floor's raster, for the user to accept or edit. Nothing here
// touches the project: proposals are plain segments until the user accepts
// them (app/model wall_proposals.h). Image processing of the drawing only;
// no radio data (docs/BUILDING_MODEL.md "Walls proposed from the plan").
//
// detect_dark_lines() works on any raster, in pixels, deterministically (the
// same raster and options give the same list, in any tree):
//   1. dark pixels: luminance below dark_level and alpha at least one half
//      (alpha 0 outside a trim, so what lies outside the floor is ignored);
//   2. connected groups of dark pixels (8-neighbours) with a bounding box
//      smaller than min_component_px are removed: noise, text, dimensions;
//   3. a Hough transform finds the strongest straight lines, strongest
//      first; the dark run along each (breaks up to max_gap_px bridged)
//      becomes a segment, on the centre of the line's width; the pixels it
//      used stop voting. Width above max_thickness_px: a filled area, no wall;
//   4. collinear segments are merged across gaps up to merge_gap_px, and,
//      when parallel_px > 0, two parallel lines closer than that (the two
//      faces of a drawn wall) become their centre line;
//   5. segments shorter than min_length_px are dropped.
//
// propose_walls() renders a floor at a bounded size (render_floor respects
// the trim's quarter turns and deskew) and returns the segments in floor
// units, with its lengths given in metres.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "yantra/core/plan/source.h"
#include "yantra/core/plan/trim.h"

namespace yantra::plan {

struct WallDetectOptions {
    int dark_level = 110;           ///< luminance 0..255: darker counts as drawn
    double min_length_px = 60.0;    ///< shorter segments are dropped
    double max_gap_px = 4.0;        ///< a break this long does not end a line
    double merge_gap_px = 10.0;     ///< collinear segments this close become one
    double merge_offset_px = 3.0;   ///< how far off the line "collinear" may be
    double merge_angle_deg = 2.0;   ///< how far turned "collinear" or "parallel" may be
    double parallel_px = 0.0;       ///< two parallel lines this close become their centre line (0 off)
    double max_thickness_px = 24.0; ///< wider: a filled area, not a wall
    double min_component_px = 0.0;  ///< smaller dark groups are removed (0: half of min_length_px)
};

/// Progress and cancel of a detection (both optional).
struct WallDetectControl {
    std::function<bool()> cancelled;  ///< true: stop and return nothing
    std::function<void(double fraction)> progress;
};

/// A line found on the raster (pixels; pixel (i, j) covers [i, i + 1) x [j, j + 1)).
struct DetectedLine {
    Point2 a;
    Point2 b;
    double thickness = 0.0;  ///< its drawn width
    double length() const noexcept;
};

/// The lines of `raster`, longest first.
std::vector<DetectedLine> detect_dark_lines(const Raster& raster, const WallDetectOptions& options = {},
                                            const WallDetectControl& control = {});

/// What propose_walls() looks for, in metres (the floor's scale).
struct WallProposalOptions {
    double min_length_m = 1.5;     ///< shorter proposals are dropped
    double max_gap_m = 0.05;       ///< a break in a drawn line
    double merge_gap_m = 0.15;     ///< smaller openings do not split a wall (a door stays open)
    double thickness_m = 0.8;      ///< the two faces of a wall at most this far apart give one centre line
    double max_thickness_m = 1.5;  ///< a wider dark area is no wall
    int dark_level = 110;
    std::uint64_t max_pixels = 6'000'000;  ///< the raster's bound: big sheets are looked at less sharply
};

/// A proposed wall in floor units.
struct ProposedSegment {
    Point2 a;
    Point2 b;
    double length_m = 0.0;
    bool operator==(const ProposedSegment&) const = default;
};

/// The render scale (pixels per floor unit) for a floor of `size` floor units
/// at `metres_per_unit`: at most `base_scale` (the plan's own detail), at most
/// 50 pixels per metre (2 cm is finer than any wall) and at most max_pixels.
double wall_detect_scale(const PageSize& size, double base_scale, double metres_per_unit, std::uint64_t max_pixels);

/// The walls proposed for a floor drawn on `page` of `source` with `trim`
/// (shape None: the whole page). Empty when cancelled; PlanError as
/// render_floor. metres_per_unit > 0.
std::vector<ProposedSegment> propose_walls(const PlanSource& source, int page, const Trim& trim, double metres_per_unit,
                                           const WallProposalOptions& options = {},
                                           const WallDetectControl& control = {});

}  // namespace yantra::plan
