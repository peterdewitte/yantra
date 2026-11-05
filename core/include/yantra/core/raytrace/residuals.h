// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Residual interpolation and confidence mask of the interpolated map
// (IND-41, IND-43). After the fit, the residual of a measured point
// (measured - model) is spread over the floor by inverse-distance
// weighting limited by walls, so the map agrees with the measured points:
//   correction(x) = sum w_i r_i / (w_0 + sum w_i),  w_i = 1 / d_i^p,
//   w_0 = 1 / R^p,
// over the points i within R of x that x sees without crossing a wall of
// the floor (walls_limit). At a measured point the correction is its
// residual (the mean of the points at the same place); far from points it
// fades to 0, the fitted model alone. The confidence mask is 1 up to
// full_confidence_m from the nearest such point, 0 from zero_confidence_m,
// linear between. docs/PROPAGATION.md "Residual interpolation".
// Positions are plan-frame metres of the floor (as the grid).
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/raytrace/residuals.h,
// adapted (plan-frame metres).
#pragma once

#include "yantra/core/raytrace/model.h"

#include <string>
#include <vector>

namespace yantra::raytrace {

struct ResidualOptions {
    double radius_m = 10.0;          ///< R: farther points do not correct (0.5 .. 1000)
    double power = 2.0;              ///< p (0.5 .. 4)
    bool walls_limit = true;         ///< a point behind a wall does not count
    double full_confidence_m = 2.0;  ///< confidence 1 up to here (0 .. zero_confidence_m)
    double zero_confidence_m = 10.0; ///< confidence 0 from here (> 0, <= 1000)

    bool operator==(const ResidualOptions&) const = default;
    std::vector<std::string> validate() const;
};

/// A measured point and its residual (measured - model), dB.
struct Residual {
    Point2 position_m;
    double residual_db = 0.0;
    bool operator==(const Residual&) const = default;
};

/// The correction and the confidence per grid cell (grid.index order).
struct ResidualField {
    Grid grid;
    std::vector<double> correction_db;
    std::vector<float> confidence;  ///< 0 .. 1
    bool cancelled = false;
};

/// The field over `grid` on floor `floor_id` (its walls limit the points a
/// cell sees). std::invalid_argument for invalid options or an unknown
/// floor; std::logic_error when the floor has no scale. Any Isa and thread
/// count give the same values.
ResidualField interpolate_residuals(const building::Building& b, const std::string& floor_id, const Grid& grid,
                                    const std::vector<Residual>& residuals, const ResidualOptions& options = {},
                                    const RunControl& run = {});

/// The correction and confidence at one position (the same rules as the
/// grid), e.g. at the measured points themselves.
struct ResidualAt {
    double correction_db = 0.0;
    double confidence = 0.0;
};
ResidualAt residual_at(const building::Building& b, const std::string& floor_id, Point2 position_m,
                       const std::vector<Residual>& residuals, const ResidualOptions& options = {});

}  // namespace yantra::raytrace
