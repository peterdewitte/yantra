// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scene export (RAD-36, PLG-27 get_scene): one floor of the building model
// (IND-11, IND-12) as scene JSON in the format yantra-radios defines
// (YR_SCENE_FORMAT, YR_SCENE_VERSION in yantra/radio.h; yantra-radios
// docs/SCENE_FORMAT.md), the input of radio plugins with YR_CAP_SCENE such
// as the simulator (RAD-61, multi-wall model RAD-73).
//
// Mapping (docs/BUILDING_MODEL.md, "Scene export"); scene format 1.1
// ("minor_version": 1 when a 1.1 member is written):
// - floor: id, name, scale_set and plan size in metres (IND-08);
//   slab_attenuation_db = Building::floor_attenuation_db, storey_height_m =
//   the floor's height; storeys (1.1): every other floor with antennas in
//   the scene, with its level above this floor (the floor-to-floor
//   heights in between) and the model's slab sum in between (IND-44).
// - wall_classes: every class with its attenuation at the scene frequency
//   (IND-11) and its reflection loss when set.
// - walls: each polyline segment in plan-frame metres with its class and
//   effective attenuation at the scene frequency (for 1.0 readers), the
//   polyline id (1.1: the wall id, else "<floor>/<index>"; a path through
//   a vertex crosses the wall once) and, for a wall of a class without its
//   own loss, the class table as bands (1.1, in Hz).
// - antennas: the indoor antennas with a cell and an EIRP, in plan-frame
//   metres; with `other_floors`, also those of other floors when both
//   floors have a scale and an alignment point (IND-09, IND-44), with
//   floor_offset = level difference (their storey gives the slabs and
//   the height).
// - cells: the cells those antennas serve. The identity values are taken
//   by the radio API's core field names (YR_FIELD_MCC, YR_FIELD_MNC,
//   YR_FIELD_LA, YR_FIELD_COLOUR_CODE); a cell without them or without a
//   carrier cannot be part of a scene.
// What cannot be exported is left out with a warning (outdoor donor
// directions, antennas without cell or EIRP, cells without identity, a
// floor without scale has no walls or antennas). Numbers are rounded to
// 1e-6 (micrometres, micro-dB), so the text is stable across platforms.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "yantra/core/building/model.h"

namespace yantra::radio {

struct SceneExportOptions {
    /// Frequency for the wall attenuations (Hz); default: the mean carrier
    /// of the exported cells, else the classes' first band.
    std::optional<double> frequency_hz;
    /// EIRP for antennas without one (IND-12: unknown EIRP); default: such
    /// antennas are left out.
    std::optional<double> default_eirp_dbm;
    /// Antennas of other aligned floors (IND-44).
    bool other_floors = true;
    /// Pretty-print with this indent (< 0: compact, one line).
    int indent = -1;
};

struct SceneExport {
    bool ok = false;
    std::string error;                  ///< when !ok (unknown floor, invalid model)
    std::string json;                   ///< scene JSON when ok
    std::vector<std::string> warnings;  ///< what was left out and why
    double frequency_hz = 0.0;          ///< frequency used for the wall attenuations (0: first band)
};

/// The scene of floor `floor_id`. Refuses an unknown floor and a model
/// that does not validate (Building::validate).
SceneExport export_scene(const building::Building& b, const std::string& floor_id,
                         const SceneExportOptions& options = {});

}  // namespace yantra::radio
