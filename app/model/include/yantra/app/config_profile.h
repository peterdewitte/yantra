// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Configuration profiles (NFR-06): the settings that make up one way of
// working, saved to one file and loaded again on this or another PC. A
// profile holds the app settings (thresholds, palettes and theme, the
// layer order and visibility of the UI layout, sound and survey defaults,
// language, touch layout), the plugin state (the plugins switched on or
// off, their settings and the stored settings of each radio device,
// RAD-47) and the level calibration table (RF-02). Not in a profile: the
// recent projects and folders and the tour's progress, which belong to
// the PC. Cell databases live in their projects (DB-01) and travel with
// them. The format is docs/formats/PROFILE.md ("yantra-profile/1").
#pragma once

#include "yantra/app/settings.h"
#include "yantra/app/settings_reset.h"

#include <filesystem>
#include <string>

namespace yantra::app {

/// The profile text of `settings` and the plugin state and calibration
/// files named in `files` (a missing file is left out; settings_file is
/// not read). False with `error` when a file exists but is not JSON.
bool profile_to_json(const AppSettings& settings, const SettingsFiles& files, std::string& out, std::string& error);

/// Checks `text` as a whole before anything is written, then writes the
/// plugin state and calibration files of `files` (both atomically; a part
/// the profile does not hold removes that file, so the defaults apply)
/// and the settings file when it is named. `settings` is the profile's
/// settings with the recent lists and tour progress of `current`. False
/// with `error` (and nothing written) for a damaged profile.
bool apply_profile(const std::string& text, const AppSettings& current, const SettingsFiles& files,
                   AppSettings& settings, std::string& error);

}  // namespace yantra::app
