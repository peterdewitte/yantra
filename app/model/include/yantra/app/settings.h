// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings of the desktop app (UI-14 recent projects, NFR-10 window system,
// DOC-07 h tooltips), stored as JSON in the user's config folder:
// $XDG_CONFIG_HOME/yantra/app.json (default ~/.config/yantra/app.json).
// Plugin settings live in the plugin state file (PLG-07). Later steps add
// their members here (layers 9.2, theme/language 9.9, tour progress 9.10;
// Reset all settings, UI-16, in 9.7 resets this whole file).
// Copied from yantra-v2 68ea20a: app/model/include/yantra/app/settings.h,
// adapted (no layer visibility yet).
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace yantra::app {

inline constexpr std::size_t kMaxRecentProjects = 10;
inline constexpr std::size_t kMaxRecentFolders = 8;

struct AppSettings {
    /// Most recent first, at most kMaxRecentProjects, no duplicates.
    std::vector<std::filesystem::path> recent_projects;
    /// Folders the file browser offers as places: where files were last
    /// opened or saved, most recent first, at most kMaxRecentFolders.
    std::vector<std::filesystem::path> recent_folders;
    /// "auto", "x11" or "wayland" (NFR-10).
    std::string window_system = "auto";
    /// Hover texts on controls (DOC-07 h); "(?)" markers always stay.
    bool tooltips = true;
    double tooltip_delay_s = 0.5;

    bool operator==(const AppSettings&) const = default;
};

/// Puts `project` first in the recent list (dropping an older entry of it
/// and the oldest beyond the limit).
void add_recent_project(AppSettings& settings, const std::filesystem::path& project);
void remove_recent_project(AppSettings& settings, const std::filesystem::path& project);
void add_recent_folder(AppSettings& settings, const std::filesystem::path& folder);

std::string settings_to_json(const AppSettings& settings);
/// Unknown keys are ignored, missing or wrong-typed ones keep their
/// default; a document that is not a JSON object gives the defaults and
/// `error`.
AppSettings settings_from_json(const std::string& text, std::string& error);

/// The config folder of Yantra: $XDG_CONFIG_HOME/yantra, else
/// $HOME/.config/yantra; empty when neither is set (absolute paths only).
std::filesystem::path config_folder(const std::string& xdg_config_home, const std::string& home);
std::filesystem::path config_folder();

/// Loads `file`; a missing file gives the defaults without error, a
/// damaged one the defaults with `error`.
AppSettings load_settings(const std::filesystem::path& file, std::string& error);
/// Writes atomically (temporary file + rename); false with `error`.
bool save_settings(const std::filesystem::path& file, const AppSettings& settings, std::string& error);

}  // namespace yantra::app
