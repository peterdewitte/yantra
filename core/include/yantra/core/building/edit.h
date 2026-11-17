// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Editing the building model (spec 21.2/21.3): what the scale and
// reference tools of the project window do with the model, without UI.
// The project file stores the result (core/project/project.h: set_scale,
// set_reference_point, cells).
//
// - Scale (IND-08): from two plan points and their real distance in
//   metres, or a scale bar; floors of one sheet may share one scale. A
//   floor without scale is allowed: its distances and areas are "not to
//   scale" (format_length/format_area), and it has no metres.
// - Alignment (IND-09): a common reference point on every floor; a point
//   of one floor maps to the same building point on another (through the
//   building frame, model.h), across sources and scales.
//
// All functions are pure (no I/O, no locks). Points are plan units of the
// floor unless the name says metres.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/building/model.h"

namespace yantra::building {

// --- scale (IND-08) --------------------------------------------------------------------

/// A TwoPoints scale: plan points a and b are `distance_m` metres apart.
/// nullopt for equal or non-finite points or a distance that is not a
/// finite number > 0.
std::optional<Scale> two_point_scale(Point2 a, Point2 b, double distance_m);
/// A ScaleBar scale: the ends of a scale bar drawn on the plan and the
/// length it stands for. nullopt as two_point_scale.
std::optional<Scale> scale_bar(Point2 a, Point2 b, double length_m);
/// A Factor scale (metres per plan unit); nullopt unless finite and > 0.
std::optional<Scale> factor_scale(double metres_per_unit);
/// The scale of floor `floor_id` (Shared).
Scale shared_scale(const std::string& floor_id);

/// True when both floors are cut from the same page of the same source:
/// their plan units have the same length (a trim only turns), so they can
/// share one scale (IND-08 "floors from one sheet").
bool same_sheet(const Floor& a, const Floor& b);
/// Ids of the floors whose scale is Shared from `floor_id`, by level.
std::vector<std::string> floors_sharing_scale(const Building& b, const std::string& floor_id);

/// Plan-unit distance and polyline length.
double distance_units(Point2 a, Point2 b);
/// Distance in metres between two plan points of `f`; nullopt without scale.
std::optional<double> distance_m(const Building& b, const Floor& f, Point2 p, Point2 q);
/// Length in metres of a polyline; nullopt without scale.
std::optional<double> length_m(const Building& b, const Floor& f, const std::vector<Point2>& polyline);
/// Area in square metres of a polygon (closed implicitly, shoelace,
/// always >= 0); nullopt without scale.
std::optional<double> area_m2(const Building& b, const Floor& f, const std::vector<Point2>& polygon);
/// "12.35 m" (centimetres, 2 decimals) or "not to scale" (IND-08 report).
std::string format_length(std::optional<double> metres);
/// "48.2 m²" (0.1 m²) or "not to scale".
std::string format_area(std::optional<double> square_metres);

// --- alignment (IND-09) -------------------------------------------------------------------

/// True when `f` has a scale and a reference point: it lines up with the
/// other aligned floors.
bool aligned(const Building& b, const Floor& f);
/// The plan point of floor `to` at the building position of plan point `p`
/// of floor `from` (the same building point, IND-09). nullopt unless both
/// floors are aligned.
std::optional<Point2> point_on_floor(const Building& b, const Floor& from, Point2 p, const Floor& to);

}  // namespace yantra::building
