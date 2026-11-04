// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * \file yantra/radios/scene.hpp
 * \brief Scene JSON v1.1 (RAD-36): parser and writer.
 *
 * The scene of one floor (walls with their attenuation, antennas, cells)
 * that a #YR_CAP_SCENE plugin pulls with yr_host_api::get_scene (RAD-18).
 * The format ("yantra-radio-scene", version #YR_SCENE_VERSION) is defined
 * in yantra/radio.h; minor version 1 (member "minor_version", absent in
 * 1.0 scenes) adds optional members that 1.0 readers ignore:
 * SceneFloor::storeys, SceneWall::polyline and SceneWall::bands
 * (docs/SCENE_FORMAT.md). Used by the simulator plugin (RAD-61), the radio
 * test host's emulated callbacks (RAD-91) and Yantra's tests.
 */
#ifndef YANTRA_RADIOS_SCENE_HPP
#define YANTRA_RADIOS_SCENE_HPP

#include <yantra/radio.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::radios {

/** Minor version of the scene JSON this code reads and writes (1.1). The
 *  major version is #YR_SCENE_VERSION; a newer minor of the same major is
 *  read (its unknown members are ignored), a newer major is refused. */
inline constexpr uint32_t kSceneMinorVersion = 1;

/** Since 1.1: another floor as seen from the scene's floor, for antennas
 *  with SceneAntenna::floor_offset (per-floor heights and slabs). */
struct SceneStorey {
    int32_t floor_offset = 0;        ///< -200..200, not 0, unique.
    /** Height of that floor's level above this floor's level in metres
     *  (negative below; the sign of floor_offset). */
    double level_m = 0.0;
    /** Total attenuation of the slabs between this floor and that one. */
    double slab_attenuation_db = 0.0;
    bool operator==(const SceneStorey&) const = default;
};

/** Since 1.1: attenuation of a wall at one frequency. */
struct SceneBand {
    uint64_t frequency_hz = 0;
    double attenuation_db = 0.0;
    bool operator==(const SceneBand&) const = default;
};

/** The floor the scene belongs to. */
struct SceneFloor {
    std::string id;
    std::string name;
    bool scale_set = false;
    double width_m = 0.0;
    double height_m = 0.0;
    /** Optional: attenuation of one floor slab (antennas on other floors,
     *  SceneAntenna::floor_offset); readers use their default when absent. */
    std::optional<double> slab_attenuation_db;
    /** Optional: floor-to-floor height in metres (> 0) for the distance to
     *  antennas on other floors; readers use their default when absent. */
    std::optional<double> storey_height_m;
    /** Since 1.1, optional: other floors with their own level and slab sum;
     *  an antenna whose floor_offset is listed uses them instead of
     *  floor_offset x storey_height_m and |floor_offset| x
     *  slab_attenuation_db. */
    std::vector<SceneStorey> storeys;
    /** The storey with `floor_offset`, nullptr when not listed. */
    const SceneStorey* find_storey(int32_t floor_offset) const;
    bool operator==(const SceneFloor&) const = default;
};

/** A wall class with its default attenuation. */
struct SceneWallClass {
    std::string id;
    std::string label;
    double attenuation_db = 0.0;
    /** Optional: loss of a specular reflection off a wall of this class
     *  (multi-wall model with reflections, RAD-73). */
    std::optional<double> reflection_loss_db;
    bool operator==(const SceneWallClass&) const = default;
};

/** A wall segment in plan metres. */
struct SceneWall {
    double x1_m = 0.0;
    double y1_m = 0.0;
    double x2_m = 0.0;
    double y2_m = 0.0;
    std::string wall_class; ///< wall_classes[].id, "" if none ("class" in JSON).
    double attenuation_db = 0.0; ///< Effective attenuation.
    /** Since 1.1, optional: id of the polyline this segment belongs to;
     *  segments with the same non-empty id are one wall, which a path
     *  through their shared vertex crosses once (RAD-73). */
    std::string polyline;
    /** Since 1.1, optional: attenuation per frequency, strictly ascending
     *  frequency_hz; when present it replaces attenuation_db at the
     *  carrier (attenuation_at_db). */
    std::vector<SceneBand> bands;
    /** The attenuation at `frequency_hz`: attenuation_db without bands or
     *  for frequency_hz 0; else linear in log(f) between the two bands
     *  around it, the first or last band's value outside them. */
    double attenuation_at_db(double frequency_hz) const;
    bool operator==(const SceneWall&) const = default;
};

/** A cell (identity and carrier). */
struct SceneCell {
    std::string id;
    std::string label;
    uint32_t mcc = 0;
    uint32_t mnc = 0;
    uint32_t la = 0;
    uint32_t colour_code = 0;
    uint64_t carrier_hz = 0;
    bool operator==(const SceneCell&) const = default;
};

/** Antenna pattern. */
struct ScenePattern {
    enum class Type { omni, sector, table };
    Type type = Type::omni;
    double azimuth_deg = 0.0;      ///< sector, table: main direction, clockwise from -y.
    double beamwidth_deg = 0.0;    ///< sector.
    double front_to_back_db = 0.0; ///< sector.
    double step_deg = 0.0;         ///< table: angle between gain_db entries.
    std::vector<double> gain_db;   ///< table: gains relative to the main direction.
    bool operator==(const ScenePattern&) const = default;
};

/** An antenna serving one cell. */
struct SceneAntenna {
    std::string id;
    std::string cell; ///< cells[].id.
    double x_m = 0.0;
    double y_m = 0.0;
    double height_m = 0.0;
    double eirp_dbm = 0.0;
    ScenePattern pattern;
    /** Optional ("floor_offset", default 0): the antenna is on the floor
     *  this many storeys above (> 0) or below (< 0) the scene's floor;
     *  x_m/y_m are in this floor's plan frame. */
    int32_t floor_offset = 0;
    bool operator==(const SceneAntenna&) const = default;
};

/** A parsed scene. */
struct Scene {
    uint32_t version = YR_SCENE_VERSION;
    /** "minor_version"; 0 when absent (a 1.0 scene). scene_to_json writes
     *  it when above 0, and 1 at least when a 1.1 member is set. */
    uint32_t minor_version = 0;
    /** True when a member added in 1.1 is set (storeys, polyline, bands). */
    bool uses_minor_1() const;
    SceneFloor floor;
    std::vector<SceneWallClass> wall_classes;
    std::vector<SceneWall> walls;
    std::vector<SceneCell> cells;
    std::vector<SceneAntenna> antennas;
    /** The cell `id`, nullptr when absent. */
    const SceneCell* find_cell(std::string_view id) const;
    /** The wall class `id`, nullptr when absent. */
    const SceneWallClass* find_wall_class(std::string_view id) const;
    bool operator==(const Scene&) const = default;
};

/**
 * Parses and checks scene JSON: format name, version (a newer major is
 * refused, any minor of major 1 is read), required members and their types, unique ids, references
 * (wall class, antenna cell) and value ranges. Unknown members are ignored.
 * False with a one-line `error` ("antennas[1].cell: unknown cell \"c9\"").
 */
bool parse_scene(std::string_view json_text, Scene& out, std::string& error);

/** Scene JSON of `scene` (compact, one line); the 1.1 members only when
 *  set, so a 1.0 scene read and written again stays a 1.0 scene. */
std::string scene_to_json(const Scene& scene);

} // namespace yantra::radios

#endif // YANTRA_RADIOS_SCENE_HPP
