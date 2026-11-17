// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A context::SceneSource for tests that need a simulator scene (RAD-36,
// PLG-27): it serves one checked-in scene file of tests/fixtures for every
// floor, with the floor's own id (as context::demo_scenes does for the
// fixed demo scenes, DEMO-05). Yantra makes no scenes from its floors.
#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <yantra/core/context/building_context.h>

namespace yantra::test {

/// `name` relative to tests/fixtures, e.g. "building/office_wall.scene.json".
inline context::SceneSource fixture_scene(const std::string& name) {
    std::ifstream in(std::filesystem::path(YANTRA_TEST_DATA_DIR) / name, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    const nlohmann::json scene = nlohmann::json::parse(text.str());
    return [scene](const building::Building&, const building::Floor& f) -> std::optional<std::string> {
        nlohmann::json j = scene;
        j["floor"]["id"] = f.id;
        return j.dump();
    };
}

}  // namespace yantra::test
