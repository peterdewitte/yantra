// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// JSON form of the building model ("yantra-building", version 1),
// documented in docs/BUILDING_MODEL.md. The project file (IND-01, NFR-05,
// a later phase) embeds it; tests and fixtures use it directly.
//
// Versioning: a reader refuses another format and a newer version; within
// version 1 members may be added compatibly, so unknown members are
// ignored at every level. Writing and reading again gives an equal model
// (numbers are written with round-trip precision).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "yantra/core/building/model.h"

namespace yantra::building {

inline constexpr const char* kBuildingFormat = "yantra-building";
inline constexpr std::uint32_t kBuildingVersion = 1;

/// The model as JSON text: compact with indent < 0, else pretty-printed
/// with `indent` spaces. Optional values that are not set are left out.
std::string building_to_json(const Building& b, int indent = -1);

/// Parses and validates (Building::validate) JSON text. False with a
/// one-line `error` naming the place ("floors[0].walls[2].class: expected
/// a string"); `out` is only changed on success. A building without
/// "wall_classes" gets the default table; a given table replaces it.
bool parse_building(std::string_view text, Building& out, std::string& error);

}  // namespace yantra::building
