// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reset all settings (UI-16, TST-37): the files that hold the user's
// settings are copied into a time-stamped backup folder and removed, so
// the next start (or AppState::reset_all_settings, which restarts the UI)
// uses the defaults, as on first start. Reset: the app settings (app.json:
// layout, theme, language, thresholds, palettes, tooltips, recent files,
// tour progress) and the plugin state (plugin_state.json: enabled plugins,
// plugin and radio device settings). Kept: projects, recordings, cell
// databases (they live in project folders, never touched here) and the
// calibration table unless `also_calibration`. Used by the Reset all
// settings dialog and by `yantra --reset-settings`.
#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace yantra::app {

/// The files of the user's settings ("" = not kept in a file).
struct SettingsFiles {
    std::filesystem::path settings_file;      ///< app.json
    std::filesystem::path plugin_state;       ///< plugin_state.json
    std::filesystem::path calibration_table;  ///< RF-02 table
};

/// What the confirmation dialog lists (UI-16).
struct ResetSummary {
    std::vector<std::string> reset;
    std::vector<std::string> kept;
};
ResetSummary reset_summary(bool also_calibration);

/// Where backups go: `<folder of app.json (else of plugin_state.json)>/backups`;
/// "" when no settings file is known.
std::filesystem::path backup_root(const SettingsFiles& files);

struct ResetOutcome {
    bool ok = false;
    std::string message;                       ///< one line for the status line / stderr
    std::filesystem::path backup_folder;       ///< "" when nothing was stored
    std::vector<std::filesystem::path> removed;  ///< the settings files reset
};

/// Copies every existing settings file (and the calibration table when
/// `also_calibration`) into `backup_root/<YYYYMMDD-HHMMSS>` (local time of
/// `now`; "-2", "-3" ... when it exists), then removes the originals. A
/// file that cannot be backed up is not removed (ok = false, message
/// says which).
ResetOutcome reset_settings_files(const SettingsFiles& files, bool also_calibration, std::time_t now);

}  // namespace yantra::app
