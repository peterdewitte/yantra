// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building model of an indoor project (spec 21; IND-04, IND-06..09): an
// ordered list of floors, each a trimmed part of a plan source with its own
// scale and alignment point, and the building's cells. Used by the context
// callbacks of the radio plugins (PLG-27, core/context/building_context.h)
// and the project file (IND-01, NFR-05, core/project/project.h).
//
// Technology-neutral (PLG-01): a cell's identity is a set of named key
// values of a technology schema (e.g. the schema's KEY fields), which only
// the technology plugin interprets.
//
// Frames and units (docs/BUILDING_MODEL.md):
// - page units: the plan source page (PDF points or image pixels), origin
//   top left, y down. Trims (IND-05/07) are outlines in page units.
// - plan units: the trimmed floor plan, origin at its top-left corner, x to
//   the right, y down, the page's unit length. Scale points, the reference
//   point and the plan size are stored in plan units, so they stay on the
//   plan when the scale changes (IND-08).
// - plan frame (metres): plan units x metres per unit, same origin and
//   axes. The positions of the context callbacks (RAD-18) are in this
//   frame.
// - building frame (metres): (plan units - reference point) x metres per
//   unit (IND-09): the same building point on every aligned floor.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace yantra::building {

/// A point in plan units, page units or metres (the caller's frame).
struct Point2 {
    double x = 0.0;
    double y = 0.0;
    bool operator==(const Point2&) const = default;
};

// --- cells -----------------------------------------------------------------------------

/// A cell of the building (the cell table, the demo's interpolation cell).
/// Technology-specific values are named key values of the technology's
/// schema (e.g. {"la": 100, "colour_code": 1, "mcc": 901, "mnc": 1} for a
/// TETRA cell); the core does not interpret them.
struct Cell {
    std::string id;             ///< unique in the building
    std::string label;          ///< display text
    std::string technology_id;  ///< technology plugin's technology id, "" when not set
    std::string schema_id;      ///< the schema whose key values `identity` holds
    std::uint64_t carrier_hz = 0;  ///< main carrier, 0 when not known
    std::map<std::string, std::int64_t> identity;  ///< key field name -> value
    bool operator==(const Cell&) const = default;
};

// --- plan sources and trims (IND-02, IND-03, IND-05..07) -------------------------------

/// A plan source of the project (a PDF or an image), as data: the file is
/// read by the plan import (later phase), never by this model.
struct PlanSource {
    std::string id;         ///< unique in the building
    std::string file_name;  ///< original file name, no folder
    std::string sha256;     ///< lower-case hex of the file, "" when not known
    /// True: the project keeps a copy of the file; false: a reference to a
    /// cached licence-restricted file by its SHA-256 (IND-03, DEMO-12).
    bool embedded = true;
    bool operator==(const PlanSource&) const = default;
};

/// The non-destructive trim of a floor (IND-05, IND-07): the outline on
/// the source page and how the floor is turned.
struct Trim {
    enum class Shape {
        None,       ///< no trim (the whole page, or no source)
        Rectangle,  ///< 4 corners in drawing order
        Polygon,    ///< 3 or more points, a simple outline
    };
    Shape shape = Shape::None;
    std::vector<Point2> outline;  ///< page units, closed implicitly
    int quarter_turns = 0;        ///< 0..3, clockwise
    double deskew_deg = 0.0;      ///< fine rotation, clockwise, -45..45
    bool operator==(const Trim&) const = default;
};

// --- scale (IND-08) --------------------------------------------------------------------

/// How a floor's scale is set (IND-08).
struct Scale {
    enum class Method {
        None,       ///< no scale: allowed, but marked "not to scale"
        TwoPoints,  ///< two plan points and their real distance
        ScaleBar,   ///< the two ends of a scale bar and the length it stands for
        Factor,     ///< metres per plan unit given directly
        Shared,     ///< the scale of floor `shared_floor` (floors of one sheet)
    };
    Method method = Method::None;
    Point2 a;                    ///< TwoPoints, ScaleBar: plan units
    Point2 b;
    double distance_m = 0.0;     ///< TwoPoints, ScaleBar: > 0
    double metres_per_unit = 0.0;  ///< Factor: > 0
    std::string shared_floor;    ///< Shared: Floor::id of a floor with its own scale
    bool operator==(const Scale&) const = default;
};

/// Metres per plan unit of a TwoPoints/ScaleBar/Factor scale; nullopt for
/// None, Shared, and invalid values (equal points, distance <= 0,
/// non-finite numbers).
std::optional<double> own_metres_per_unit(const Scale& s);

// --- floors and building ---------------------------------------------------------------

/// One floor (IND-04, IND-06..09).
struct Floor {
    std::string id;     ///< unique in the building, stable (context callbacks, scene)
    std::string name;   ///< e.g. "Ground floor"
    int level = 0;      ///< 0 ground, 1 first floor, -1 basement; unique
    double height_m = 3.0;  ///< floor-to-floor height
    std::string source;     ///< PlanSource::id, "" without a plan source
    /// Stable id of a bundled example plan's floor (DEMO-05, DEMO-12, RAD-18,
    /// e.g. "fraunces-1"); "" for the user's own plans.
    std::string example_id;
    int page = 0;           ///< page of the source (0-based)
    Trim trim;
    double plan_width = 0.0;   ///< trimmed plan size, plan units
    double plan_height = 0.0;
    Scale scale;
    std::optional<Point2> reference_point;  ///< alignment point, plan units (IND-09)
    bool operator==(const Floor&) const = default;
};

/// The building of one indoor project.
struct Building {
    std::string name;  ///< e.g. "Fraunces Tavern"; plan names are "<name>, <floor name>"
    std::vector<Cell> cells;
    std::vector<PlanSource> sources;
    std::vector<Floor> floors;  ///< any order; floors_by_level() for the tabs (IND-04)
    bool operator==(const Building&) const = default;

    const Cell* find_cell(const std::string& id) const;
    const PlanSource* find_source(const std::string& id) const;
    const Floor* find_floor(const std::string& id) const;
    Floor* find_floor(const std::string& id);
    const Floor* floor_at_level(int level) const;
    /// Floor indexes ordered by level (IND-04 tabs).
    std::vector<std::size_t> floors_by_level() const;

    /// Metres per plan unit of `f` (its own scale, or the shared floor's);
    /// nullopt without a usable scale.
    std::optional<double> metres_per_unit(const Floor& f) const;
    bool has_scale(const Floor& f) const { return metres_per_unit(f).has_value(); }
    /// Plan units -> plan-frame metres (IND-08, RAD-18); nullopt without scale.
    std::optional<Point2> to_plan_metres(const Floor& f, Point2 plan) const;
    /// Plan-frame metres -> plan units; nullopt without scale.
    std::optional<Point2> from_plan_metres(const Floor& f, Point2 metres) const;
    /// Plan size in metres; nullopt without scale.
    std::optional<Point2> plan_size_m(const Floor& f) const;
    /// Plan units -> building metres (IND-09: minus the reference point,
    /// (0, 0) when the floor has none); nullopt without scale.
    std::optional<Point2> to_building_metres(const Floor& f, Point2 plan) const;
    /// Building metres -> plan units of `f`; nullopt without scale.
    std::optional<Point2> from_building_metres(const Floor& f, Point2 metres) const;

    /// Problems that make the model unusable, one line each with its place
    /// ("floors[1]: duplicate level 2"); empty when valid. Checks ids,
    /// references, levels, scales, trims and number ranges.
    std::vector<std::string> validate() const;
};

/// The plan name of a floor for the context callbacks (RAD-18): "<building
/// name>, <floor name>", or the floor name alone without a building name.
std::string plan_name(const Building& b, const Floor& f);

}  // namespace yantra::building
