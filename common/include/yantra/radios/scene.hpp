// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * \file yantra/radios/scene.hpp
 * \brief Scene JSON v1 (RAD-36): parser and writer.
 *
 * The scene of one floor (walls with their attenuation, antennas, cells)
 * that a #YR_CAP_SCENE plugin pulls with yr_host_api::get_scene (RAD-18).
 * The format ("yantra-radio-scene", version #YR_SCENE_VERSION) is defined
 * in yantra/radio.h. Used by the simulator plugin (RAD-61), the radio test
 * host's emulated callbacks (RAD-91) and Yantra's tests.
 */
#ifndef YANTRA_RADIOS_SCENE_HPP
#define YANTRA_RADIOS_SCENE_HPP

#include <yantra/radio.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::radios {

/** The floor the scene belongs to. */
struct SceneFloor {
    std::string id;
    std::string name;
    bool scale_set = false;
    double width_m = 0.0;
    double height_m = 0.0;
    bool operator==(const SceneFloor&) const = default;
};

/** A wall class with its default attenuation. */
struct SceneWallClass {
    std::string id;
    std::string label;
    double attenuation_db = 0.0;
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
    bool operator==(const SceneAntenna&) const = default;
};

/** A parsed scene. */
struct Scene {
    uint32_t version = YR_SCENE_VERSION;
    SceneFloor floor;
    std::vector<SceneWallClass> wall_classes;
    std::vector<SceneWall> walls;
    std::vector<SceneCell> cells;
    std::vector<SceneAntenna> antennas;
    /** The cell `id`, nullptr when absent. */
    const SceneCell* find_cell(std::string_view id) const;
    bool operator==(const Scene&) const = default;
};

/**
 * Parses and checks scene JSON: format name, version (a newer one is
 * refused), required members and their types, unique ids, references
 * (wall class, antenna cell) and value ranges. Unknown members are ignored.
 * False with a one-line `error` ("antennas[1].cell: unknown cell \"c9\"").
 */
bool parse_scene(std::string_view json_text, Scene& out, std::string& error);

/** Scene JSON v1 of `scene` (compact, one line). */
std::string scene_to_json(const Scene& scene);

} // namespace yantra::radios

#endif // YANTRA_RADIOS_SCENE_HPP
