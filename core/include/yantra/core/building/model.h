// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building model of an indoor project (spec 21; IND-04, IND-06..12): an
// ordered list of floors, each a trimmed part of a plan source with its own
// scale and alignment point, its walls (polylines with a wall class) and
// its antennas; the editable wall-class table; the cells the antennas
// serve. Used by the ray tracer (IND-40), the simulator scene export
// (RAD-36, core/radio/scene_export.h), the context callbacks of the radio
// plugins (PLG-27, core/context/building_context.h) and later the project
// file (IND-01, NFR-05), which embeds the JSON form of model_json.h.
//
// Technology-neutral (PLG-01): wall classes and antennas are generic RF
// data; a cell's identity is a set of named key values of a technology
// schema (e.g. the schema's KEY fields), which only the technology plugin
// interprets.
//
// Frames and units (docs/BUILDING_MODEL.md):
// - page units: the plan source page (PDF points or image pixels), origin
//   top left, y down. Trims (IND-05/07) are outlines in page units.
// - plan units: the trimmed floor plan, origin at its top-left corner, x to
//   the right, y down, the page's unit length. Walls, antennas and the plan
//   size are stored in plan units, so they stay on the plan when the scale
//   changes and a floor without scale can still be drawn on (IND-08).
// - plan frame (metres): plan units x metres per unit, same origin and
//   axes. The positions of the context callbacks and the scene (RAD-18,
//   RAD-36) are in this frame.
// - building frame (metres): (plan units - reference point) x metres per
//   unit (IND-09): the same building point on every aligned floor.
// - Angles: degrees clockwise from the plan's -y axis ("up" on the plan),
//   as in the scene format. Heights in metres above the floor; levels in
//   dB and dBm; frequencies in MHz in the wall-class table, Hz elsewhere.
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

// --- wall classes (IND-11) -------------------------------------------------------------

/// One entry of a wall class's attenuation table: the loss of one crossing
/// at `frequency_mhz`.
struct BandAttenuation {
    double frequency_mhz = 0.0;
    double attenuation_db = 0.0;
    bool operator==(const BandAttenuation&) const = default;
};

/// A wall class with its attenuation per band (IND-11 "attenuation in dB
/// at the measured frequency, from an editable table with defaults").
struct WallClass {
    std::string id;    ///< key used by walls, e.g. "concrete"
    std::string name;  ///< display name (English; the UI translates the default ids)
    /// Attenuation table, ascending frequencies, at least one entry.
    std::vector<BandAttenuation> bands;
    /// Loss of a specular reflection off a wall of this class (IND-40);
    /// the propagation model's default when absent.
    std::optional<double> reflection_loss_db;
    bool operator==(const WallClass&) const = default;

    /// The loss at `mhz`: linear in log(frequency) between the two
    /// neighbouring table entries, the first or last entry's value outside
    /// the table, the first entry's value when mhz <= 0. 0 for an empty table.
    double attenuation_at_db(double mhz) const;
};

/// The default wall-class table (estimates at 400 and 900 MHz, see
/// docs/BUILDING_MODEL.md): concrete 12/23 dB, brick 8/10, plasterboard
/// 3/6, glass 2/4.5, wood 3/6.5, door 4/9, metal 25/25, coated glass 20/20.
std::vector<WallClass> default_wall_classes();

// --- walls (IND-11) --------------------------------------------------------------------

/// A wall: a polyline on one floor.
struct Wall {
    std::string id;              ///< unique on its floor, may be empty
    std::string wall_class;      ///< WallClass::id ("" only with an attenuation override)
    std::vector<Point2> points;  ///< plan units, at least 2
    /// This wall's own attenuation instead of its class's value (at every
    /// frequency).
    std::optional<double> attenuation_db;
    bool operator==(const Wall&) const = default;
};

// --- cells and antennas (IND-12) -------------------------------------------------------

/// A cell the antennas serve. Technology-specific values are named key
/// values of the technology's schema (e.g. {"la": 100, "colour_code": 1,
/// "mcc": 901, "mnc": 1} for a TETRA cell); the core does not interpret them.
struct Cell {
    std::string id;             ///< unique in the building
    std::string label;          ///< display text
    std::string technology_id;  ///< technology plugin's technology id, "" when not set
    std::string schema_id;      ///< the schema whose key values `identity` holds
    std::uint64_t carrier_hz = 0;  ///< main carrier, 0 when not known
    std::map<std::string, std::int64_t> identity;  ///< key field name -> value
    bool operator==(const Cell&) const = default;
};

enum class AntennaType {
    IndoorDas,     ///< indoor DAS antenna (or a repeater's service antenna)
    OutdoorDonor,  ///< outdoor donor/macro direction placed on the plan
};

/// Radiation pattern (IND-12), the pattern types of the scene format.
struct AntennaPattern {
    enum class Type { Omni, Sector, Table };
    Type type = Type::Omni;
    double azimuth_deg = 0.0;       ///< Sector, Table: main direction, [0, 360)
    double beamwidth_deg = 65.0;    ///< Sector: half-power beamwidth, (0, 360]
    double front_to_back_db = 20.0; ///< Sector: cap of the pattern loss, >= 0
    double step_deg = 0.0;          ///< Table: angle between gain_db entries, > 0
    std::vector<double> gain_db;    ///< Table: gains relative to the main direction
    bool operator==(const AntennaPattern&) const = default;
};

/// Pattern gain relative to the main direction (dB, <= 0 for Sector):
/// Omni 0; Sector -min(12 (off / beamwidth)^2, front_to_back); Table
/// linear interpolation between the entries clockwise from the azimuth,
/// from the last entry back to the first at 360 degrees. `bearing_deg` in
/// the angle convention of this file. The formula of the simulator's
/// multi-wall model (yantra-radios docs/CHANNEL_MODEL.md, RAD-73).
double pattern_gain_db(const AntennaPattern& p, double bearing_deg);

/// Bearing (degrees in [0, 360), 0 = towards -y, clockwise) from a to b.
double bearing_deg(Point2 a, Point2 b);

/// An antenna or source on a floor (IND-12).
struct Antenna {
    std::string id;     ///< unique in the building
    std::string name;
    std::string model;  ///< antenna type/product, free text (e.g. "ceiling omni")
    AntennaType type = AntennaType::IndoorDas;
    Point2 position;        ///< plan units of its floor
    double height_m = 2.7;  ///< above the floor
    double gain_dbi = 2.0;
    AntennaPattern pattern;
    /// Known EIRP in the main direction; optional (IND-12: estimated from
    /// the measurements when unknown, IND-42).
    std::optional<double> eirp_dbm;
    /// Power at the DAS feed point, with the losses up to the antenna.
    std::optional<double> feed_power_dbm;
    double cable_loss_db = 0.0;
    double splitter_loss_db = 0.0;
    std::string cell;  ///< Cell::id it serves, "" when not set
    /// OutdoorDonor: bearing from the antenna's plan position towards the
    /// outdoor site (angle convention above) and its elevation (-90..90).
    std::optional<double> direction_deg;
    std::optional<double> elevation_deg;
    bool operator==(const Antenna&) const = default;

    /// eirp_dbm when set, else feed - cable - splitter + gain, else nullopt.
    std::optional<double> effective_eirp_dbm() const;
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

/// One floor (IND-04, IND-06..12).
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
    /// Attenuation of the slab between this floor and the one above
    /// (IND-44); Building::floor_attenuation_db when absent.
    std::optional<double> slab_attenuation_db;
    std::vector<Wall> walls;
    std::vector<Antenna> antennas;
    bool operator==(const Floor&) const = default;
};

/// The building of one indoor project.
struct Building {
    std::string name;  ///< e.g. "Fraunces Tavern"; plan names are "<name>, <floor name>"
    std::vector<WallClass> wall_classes = default_wall_classes();
    std::vector<Cell> cells;
    std::vector<PlanSource> sources;
    std::vector<Floor> floors;  ///< any order; floors_by_level() for the tabs (IND-04)
    double floor_attenuation_db = 15.0;  ///< default slab attenuation (IND-44)
    bool operator==(const Building&) const = default;

    const WallClass* find_class(const std::string& id) const;
    const Cell* find_cell(const std::string& id) const;
    const PlanSource* find_source(const std::string& id) const;
    const Floor* find_floor(const std::string& id) const;
    Floor* find_floor(const std::string& id);
    const Floor* floor_at_level(int level) const;
    /// The floor holding antenna `id` and the antenna, or nulls.
    std::pair<const Floor*, const Antenna*> find_antenna(const std::string& id) const;
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

    /// Attenuation of one crossing of `w` at `mhz` (its override, else its
    /// class's value at mhz); nullopt for an unknown class.
    std::optional<double> wall_attenuation_db(const Wall& w, double mhz) const;
    /// Sum of the slabs between two levels (each boundary k|k+1 is floor
    /// k's slab_attenuation_db, else floor_attenuation_db); 0 for one level.
    double inter_floor_attenuation_db(int from_level, int to_level) const;

    /// Problems that make the model unusable, one line each with its place
    /// ("floors[1].walls[3]: unknown wall class \"steel\""); empty when
    /// valid. Checks ids, references, levels, polylines, scales, trims,
    /// patterns and number ranges.
    std::vector<std::string> validate() const;
};

/// The plan name of a floor for the context callbacks (RAD-18): "<building
/// name>, <floor name>", or the floor name alone without a building name.
std::string plan_name(const Building& b, const Floor& f);

}  // namespace yantra::building
