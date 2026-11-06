// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings of the desktop app (UI-14 recent projects, NFR-10 window system,
// DOC-07 h tooltips), stored as JSON in the user's config folder:
// $XDG_CONFIG_HOME/yantra/app.json (default ~/.config/yantra/app.json).
// Plugin settings live in the plugin state file (PLG-07). Later steps add
// their members here (theme/language 9.9, tour progress 9.10; Reset all
// settings, UI-16, in 9.7 resets this whole file). The project window's
// layers (IND-04) are a viewing preference kept here, not in the project
// file: a project looks the same to everyone and reports are not affected.
// Copied from yantra-v2 68ea20a: app/model/include/yantra/app/settings.h,
// adapted (layer order, plan and binned layers).
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::app {

inline constexpr std::size_t kMaxRecentProjects = 10;
inline constexpr std::size_t kMaxRecentFolders = 8;

/// The layers of the project window's plan view (IND-04, IND-30, IND-43,
/// PP-05).
enum class Layer { Plan, Interpolation, Binned, Walls, Antennas, Points };
/// Every layer in the default drawing order, bottom first.
inline constexpr Layer kLayers[] = {Layer::Plan,  Layer::Interpolation, Layer::Binned,
                                    Layer::Walls, Layer::Antennas,      Layer::Points};
/// "plan", "interpolation", "binned", "walls", "antennas", "points"
/// (settings keys, ImGui ids "layer.<key>").
const char* layer_key(Layer layer);
std::optional<Layer> parse_layer(std::string_view key);

/// Which layers are shown and in which order (IND-04 "each layer can be
/// shown or hidden"). A new user sees every layer but the binned view
/// (an extra view, IND-30), in kLayers order.
struct LayerSettings {
    /// Drawing order, bottom first; always every layer once.
    std::vector<Layer> order{std::begin(kLayers), std::end(kLayers)};
    /// Hidden layers (all others are shown).
    std::vector<Layer> hidden{Layer::Binned};
    /// The binned view's square size (PP-05), 0.5 .. 50 m.
    double bin_size_m = 2.0;

    bool visible(Layer layer) const;
    void set_visible(Layer layer, bool visible);
    /// Moves a layer one place up (+1, drawn later) or down (-1); false
    /// when it is already at that end.
    bool move(Layer layer, int steps);
    /// Position in the drawing order (0 = bottom).
    std::size_t position(Layer layer) const;
    bool operator==(const LayerSettings&) const = default;
};

struct AppSettings {
    /// Most recent first, at most kMaxRecentProjects, no duplicates.
    std::vector<std::filesystem::path> recent_projects;
    /// Folders the file browser offers as places: where files were last
    /// opened or saved, most recent first, at most kMaxRecentFolders.
    std::vector<std::filesystem::path> recent_folders;
    LayerSettings layers;
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
