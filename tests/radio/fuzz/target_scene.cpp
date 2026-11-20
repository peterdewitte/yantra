// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target scene (RAD-96): a yantra-radio-scene document (RAD-36, what
// the host hands the simulator through get_scene). What parse_scene
// accepts must survive scene_to_json and a second parse unchanged, and the
// multi-wall model (RAD-73) must give a level for every cell at a point.

#include "fuzz.hpp"

#include "yantra/gen/multiwall.hpp"
#include "yantra/radios/scene.hpp"

#include <cmath>
#include <string>

namespace radios = yantra::radios;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 256 * 1024)
        return 0;
    radios::Scene scene;
    std::string error;
    if (!radios::parse_scene(yantra::fuzz::text_of(data, size), scene, error))
        return 0;
    radios::Scene again;
    const std::string json = radios::scene_to_json(scene);
    if (!radios::parse_scene(json, again, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        fail("a scene written by scene_to_json is refused");
    }
    if (radios::scene_to_json(again) != json)
        fail("scene_to_json + parse_scene changed the scene");
    if (scene.walls.size() <= 64 && scene.antennas.size() <= 16) {
        yantra::gen::MultiWallModel model;
        for (const radios::SceneCell& c : scene.cells) {
            const yantra::gen::CellLevel l = model.cell(scene, c.id, {1.0, 2.0});
            if (std::isnan(l.level_dbm))
                fail("multi-wall level is NaN");
        }
    }
    return 0;
}
