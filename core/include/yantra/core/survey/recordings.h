// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project's list of its recording files (REC-02, REC-03, IND-20;
// project schema 4 `recordings`, docs/formats/PROJECT.md).
#pragma once

#include <cstdint>
#include <filesystem>

#include "yantra/core/project/project.h"

namespace yantra::survey {

/// Lists the recording file `path` in the project: file name, session,
/// file number, size, SHA-256, the header line and the path as a hint.
/// ProjectError Io when it cannot be read, Invalid when it is not a
/// recording (docs/formats/RECORDING.md), Exists when already listed.
std::int64_t add_recording_file(project::Project& project, const std::filesystem::path& path);

}  // namespace yantra::survey
