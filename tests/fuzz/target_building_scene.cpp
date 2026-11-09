// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target building_scene (TST-12): a building model ("yantra-building"
// v1, docs/BUILDING_MODEL.md) and its scene export (RAD-36). The parser
// refuses the text with an error or gives a valid model; that model
// writes JSON that parses again, and the scene of every floor is either
// refused with an error or valid JSON.

#include "fuzz.hpp"

#include "yantra/core/building/model_json.h"
#include "yantra/core/radio/scene_export.h"

#include <nlohmann/json.hpp>

namespace yb = yantra::building;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 256 * 1024) return 0;
    yb::Building b;
    std::string error;
    if (!yb::parse_building(yantra::fuzz::text_of(data, size), b, error)) {
        if (error.empty()) fail("a refused building has no error");
        return 0;
    }
    const std::string json = yb::building_to_json(b);
    yb::Building again;
    if (!yb::parse_building(json, again, error)) fail("a building's own JSON is refused");
    if (yb::building_to_json(again) != json) fail("building JSON round trip changed the model");
    for (const yb::Floor& f : b.floors) {
        yantra::radio::SceneExportOptions o;
        o.default_eirp_dbm = 20.0;
        const auto scene = yantra::radio::export_scene(b, f.id, o);
        if (!scene.ok) {
            if (scene.error.empty()) fail("a refused scene has no error");
            continue;
        }
        if (!nlohmann::json::accept(scene.json)) fail("the scene export is not JSON");
    }
    return 0;
}
