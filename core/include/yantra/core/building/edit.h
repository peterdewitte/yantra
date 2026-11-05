// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Editing the building model (spec 21.2/21.3): what the scale and
// reference tools, the wall drawing tools and the antenna placement of the
// project window do with the model, without UI (the window comes in phase
// 9 and only calls these). The project file stores the result
// (core/project/project.h: set_scale, set_reference_point, walls, wall
// classes, cells, antennas).
//
// - Scale (IND-08): from two plan points and their real distance in
//   metres, or a scale bar; floors of one sheet may share one scale. A
//   floor without scale is allowed: its distances and areas are "not to
//   scale" (format_length/format_area), and it has no metres.
// - Alignment (IND-09): a common reference point on every floor; a point
//   of one floor maps to the same building point on another (through the
//   building frame, model.h), across sources and scales.
// - Walls (IND-11): polylines in plan units with a wall class from the
//   editable table; vertex edits, split, hit test for selection.
// - Antennas (IND-12): placement, aiming (azimuth or donor direction) and
//   the directional pattern.
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
/// other aligned floors (multi-floor view, IND-44).
bool aligned(const Building& b, const Floor& f);
/// The plan point of floor `to` at the building position of plan point `p`
/// of floor `from` (the same building point, IND-09). nullopt unless both
/// floors are aligned.
std::optional<Point2> point_on_floor(const Building& b, const Floor& from, Point2 p, const Floor& to);

// --- walls (IND-11) -----------------------------------------------------------------------

/// The wall nearest to a plan point within `tolerance` plan units (the
/// selection of the wall tool): wall index, segment index (points
/// segment..segment+1), parameter t along the segment, nearest point and
/// its distance. nullopt when no wall is that close. Ties go to the wall
/// drawn last (on top).
struct WallHit {
    std::size_t wall = 0;
    std::size_t segment = 0;
    double t = 0.0;
    Point2 nearest;
    double distance = 0.0;
};
std::optional<WallHit> nearest_wall(const Floor& f, Point2 p, double tolerance);
/// The vertex of wall `w` within `tolerance` of `p` (nearest), or nullopt.
std::optional<std::size_t> nearest_vertex(const Wall& w, Point2 p, double tolerance);

/// Vertex edits; false (and no change) for an index out of range or, for
/// remove_vertex, when the wall would keep fewer than 2 points.
bool move_vertex(Wall& w, std::size_t index, Point2 to);
/// Inserts before `index` (index == size appends).
bool insert_vertex(Wall& w, std::size_t index, Point2 p);
bool remove_vertex(Wall& w, std::size_t index);
/// Moves every point by (dx, dy).
void move_wall(Wall& w, Point2 by);
/// Splits a wall at inner vertex `index` (1..size-2) into two walls that
/// both hold that vertex; the second gets id `second_id`, the same class
/// and override. nullopt for an end or out-of-range vertex.
std::optional<std::pair<Wall, Wall>> split_wall(const Wall& w, std::size_t index, const std::string& second_id = {});
/// Total length of a wall in plan units.
double wall_length_units(const Wall& w);

/// Adds `c` at the end of the table, or replaces the class with its id.
void put_wall_class(std::vector<WallClass>& table, const WallClass& c);
/// Number of walls (all floors) using class `id`.
std::size_t walls_of_class(const Building& b, const std::string& id);
/// Puts the default classes back to their default values (missing ones
/// are added at the end); other classes stay as they are.
void reset_default_wall_classes(std::vector<WallClass>& table);

// --- antennas (IND-12) --------------------------------------------------------------------

/// A directional pattern (IND-12 "directional with azimuth"): Sector with
/// the azimuth (wrapped into [0, 360)), half-power beamwidth and
/// front-to-back ratio.
AntennaPattern directional_pattern(double azimuth_deg, double beamwidth_deg = 65.0, double front_to_back_db = 20.0);
/// Aims an antenna at plan point `target`: an outdoor donor gets its
/// direction_deg, a directional (Sector/Table) antenna its azimuth; an omni
/// antenna is unchanged. False when target is the antenna's position.
bool aim_antenna(Antenna& a, Point2 target);
/// The antenna nearest to `p` within `tolerance` plan units (index into
/// f.antennas), or nullopt.
std::optional<std::size_t> nearest_antenna(const Floor& f, Point2 p, double tolerance);

}  // namespace yantra::building
