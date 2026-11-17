// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the scene exporter with the simulator (RAD-36, IND-11, IND-44,
// TST-36): a context::SceneSource that exports each floor's scene from the
// building model under test. The app does not do this any more: it serves
// the fixed demo scenes of the example floors only (context::demo_scenes,
// DEMO-05); these tests keep checking the exporter, which makes those
// files (yantra-demo --export-scenes), until it is removed.
#pragma once

#include <optional>
#include <string>

#include <yantra/core/context/building_context.h>
#include <yantra/core/radio/scene_export.h>

namespace yantra::test {

inline context::SceneSource model_scenes(radio::SceneExportOptions options = {}) {
    return [options](const building::Building& b, const building::Floor& f) -> std::optional<std::string> {
        radio::SceneExport e = radio::export_scene(b, f.id, options);
        if (!e.ok) return std::nullopt;
        return std::move(e.json);
    };
}

}  // namespace yantra::test
