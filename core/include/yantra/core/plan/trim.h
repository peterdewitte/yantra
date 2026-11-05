// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Non-destructive trims (IND-05, IND-07): the part of a source page that
// is one floor, and how it is turned. A floor stores its source, page and
// Trim; its plan is rendered from the source each time (render_floor),
// never stored as a new image.
//
// Coordinate spaces (docs/PLANS.md):
// - page units: the source page (source.h), origin top-left, y down;
// - floor units: the trimmed floor plan. Same unit length as page units,
//   turned by the trim's rotation, origin at the top-left corner of the
//   turned outline's bounding box, y down. Floor::plan_width/plan_height,
//   and later walls, antennas and walk points (IND-08..IND-20), are in
//   floor units; the floor scale (metres per unit, IND-08) converts them
//   to metres;
// - pixels: either space times a render scale (pixels per unit).
//
// Rotation: quarter_turns x 90 degrees plus deskew_deg, clockwise as seen
// on screen (y down). Quarter turns are applied exactly (no rounding), the
// deskew with cos/sin.
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plan/trim.h, adapted (plan::Trim is building::Trim).
#pragma once

#include <optional>
#include <vector>

#include "yantra/core/building/model.h"
#include "yantra/core/plan/source.h"

namespace yantra::plan {

using building::Point2;

/// The trim of the building model (building::Trim, IND-07): shape,
/// outline in page units, quarter turns and deskew.
using Trim = building::Trim;

/// Largest deskew angle (degrees): larger turns are quarter turns.
inline constexpr double kMaxDeskewDeg = 45.0;

/// A rectangle trim with sides along the turned frame of `turned`
/// (its quarter turns and deskew are kept), through the page points
/// `a` and `b` (opposite corners as the user dragged them in the view).
Trim rectangle_trim(Point2 a, Point2 b, int quarter_turns = 0, double deskew_deg = 0.0);
/// The whole page as a rectangle trim.
Trim whole_page_trim(const PageSize& page, int quarter_turns = 0, double deskew_deg = 0.0);

/// Deskew angle (degrees, -45..45) that makes the line a -> b horizontal or
/// vertical, whichever is closer (IND-05 "fine deskew" from two points
/// on a wall or the sheet's border). Invalid when a == b.
double deskew_from_line(Point2 a, Point2 b);

/// Throws PlanError Invalid with the reason when the trim cannot be used:
/// shape None, too few points, non-finite numbers, zero area, quarter
/// turns outside 0..3, deskew outside -45..45, or a self-intersecting
/// polygon.
void validate_trim(const Trim& trim);

/// The geometry of a valid trim: page <-> floor coordinate mapping.
class TrimTransform {
public:
    /// Validates the trim (validate_trim).
    explicit TrimTransform(const Trim& trim);

    const Trim& trim() const noexcept { return trim_; }
    /// Floor plan size in floor units (plan_width x plan_height).
    PageSize floor_size() const noexcept { return size_; }
    /// Rotation in degrees, clockwise (quarter turns + deskew).
    double rotation_deg() const noexcept { return trim_.quarter_turns * 90.0 + trim_.deskew_deg; }

    Point2 page_to_floor(Point2 page) const noexcept;
    Point2 floor_to_page(Point2 floor) const noexcept;
    /// The outline in floor units.
    std::vector<Point2> floor_outline() const;
    /// True when the page point is inside the outline (even-odd rule).
    bool contains_page(Point2 page) const noexcept;
    /// Bounding box of the outline on the page: {x, y} and {x + w, y + h}.
    std::pair<Point2, Point2> page_bounds() const noexcept;

private:
    Point2 turn(Point2 p) const noexcept;    // page -> turned frame
    Point2 unturn(Point2 p) const noexcept;  // turned frame -> page
    Trim trim_;
    double cos_ = 1.0;
    double sin_ = 0.0;
    Point2 origin_;  // min corner of the turned outline
    PageSize size_;
};

/// Pixel <-> unit mapping at a render scale: pixel (i, j) covers the unit
/// square [i / s, (i + 1) / s) x [j / s, (j + 1) / s).
inline Point2 to_pixels(Point2 units, double scale) noexcept { return {units.x * scale, units.y * scale}; }
inline Point2 from_pixels(Point2 pixels, double scale) noexcept { return {pixels.x / scale, pixels.y / scale}; }

/// Floor raster size at `scale` (as page_pixels of floor_size()).
PixelSize floor_pixels(const TrimTransform& t, double scale);

/// Renders `window` of the floor raster at `scale` pixels per floor unit
/// (IND-07): the page turned and trimmed, alpha 0 outside the outline and
/// outside the page. Quarter turns without deskew copy pixels exactly;
/// a deskew samples bilinearly. Windows of one scale fit together
/// exactly (tiles).
Raster render_floor(const PlanSource& source, int page, const TrimTransform& t, double scale,
                    const PixelRect& window);

}  // namespace yantra::plan
