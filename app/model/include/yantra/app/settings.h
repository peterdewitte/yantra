// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings of the desktop app (UI-14 recent projects, NFR-10 window system,
// DOC-07 h tooltips), stored as JSON in the user's config folder:
// $XDG_CONFIG_HOME/yantra/app.json (default ~/.config/yantra/app.json).
// Plugin settings live in the plugin state file (PLG-07). Step 9.7 added
// the theme, the survey defaults (coverage threshold, graph span, ring
// buffer) and the settings window; 9.9 the language (UI-12), the touch
// layout and scale (UI-13) and the sounds (UI-09/UI-10); later steps add
// theirs (tour progress 9.10). Reset all settings
// (UI-16, settings_reset.h) resets this whole file. The project window's
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
/// (The walls and antennas layers of 0.3 are gone; their keys in a
/// settings file are ignored.)
enum class Layer { Plan, Interpolation, Binned, Points };
/// Every layer in the default drawing order, bottom first.
inline constexpr Layer kLayers[] = {Layer::Plan, Layer::Interpolation, Layer::Binned, Layer::Points};
/// "plan", "interpolation", "binned", "points" (settings keys, ImGui ids
/// "layer.<key>").
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

/// The app's colour theme (UI-13): dark for dark rooms, light, and high
/// contrast for bright sites (white on black, yellow focus). The logos
/// follow it (BRD-03: the light PNG on the light theme, else the dark one).
enum class Theme { Dark, Light, HighContrast };
inline constexpr Theme kThemes[] = {Theme::Dark, Theme::Light, Theme::HighContrast};
const char* theme_key(Theme theme);  ///< "dark", "light", "high_contrast"
std::optional<Theme> parse_theme(std::string_view key);
/// The theme has a light background (logos, graph palettes).
inline bool theme_is_light(Theme theme) { return theme == Theme::Light; }

/// UI-13 layout: the touch layout for 7-12" tablets makes every control
/// at least kTouchTargetPx high and spaces them out; the scale follows
/// the display scaling (1 = 100 %), on top of the monitor's own scale.
inline constexpr double kTouchTargetPx = 48.0;
inline constexpr double kMinUiScale = 0.75, kMaxUiScale = 2.5;

/// UI-09 audible events and UI-10 level alerts.
inline constexpr std::size_t kMaxLevelAlerts = 8;  ///< UI-10 "up to N cells"
inline constexpr double kMinLevelIntervalS = 2.0, kMaxLevelIntervalS = 120.0;
struct LevelAlert {
    /// The cell: its name in the cell database or its identity text as
    /// the panels show it ("la=1;mcc=..."), or a carrier in MHz ("390.0125").
    std::string cell;
    double threshold_dbm = -95.0;  ///< kMinThresholdDbm .. kMaxThresholdDbm
    bool operator==(const LevelAlert&) const = default;
};
struct SoundSettings {
    /// Sounds at all (UI-09/UI-10); the popups of level alerts stay.
    bool enabled = false;
    double volume = 0.6;                ///< 0 .. 1
    bool level_tone = false;            ///< UI-09: the serving level as a tone at an interval
    double level_interval_s = 10.0;     ///< kMinLevelIntervalS .. kMaxLevelIntervalS
    bool sync_loss = true;              ///< UI-09: loss of sync (and its return)
    bool reselection = true;            ///< UI-09: a new serving cell
    bool low_battery = true;            ///< UI-09
    std::vector<LevelAlert> alerts;     ///< UI-10, at most kMaxLevelAlerts
    bool alert_popup = true;            ///< UI-10: a popup besides the sound
    bool operator==(const SoundSettings&) const = default;
};

/// Survey defaults kept across runs (UI-11, REC-09, graphs of UI-02..04).
struct SurveySettings {
    /// The serving level that counts as covered (UI-11), -150 .. -20 dBm.
    double threshold_dbm = -95.0;
    /// The live graphs' time span, 1 .. 60 minutes.
    double graph_span_min = 10.0;
    /// REC-09: the ring buffer is on when a project opens, and its length
    /// (2 .. 60 minutes).
    bool ring = false;
    double ring_minutes = 10.0;
    bool operator==(const SurveySettings&) const = default;
};
inline constexpr double kMinThresholdDbm = -150.0, kMaxThresholdDbm = -20.0;
inline constexpr double kMinGraphSpanMin = 1.0, kMaxGraphSpanMin = 60.0;
inline constexpr double kMinRingMinutes = 2.0, kMaxRingMinutes = 60.0;
inline constexpr double kMaxTooltipDelayS = 5.0;

/// EXP-07: where exports and reports are sent after they are written
/// (share.h), on request in the export and report dialogs, or by itself
/// when a walk test finishes. Sending over the internet is optional:
/// Off sends nothing. The password of an HTTP or SMTP login is typed per
/// run and never stored.
struct SharingSettings {
    enum class Method { Off, Folder, Http, Smtp };
    Method method = Method::Off;
    std::filesystem::path folder;  ///< Folder: a shared (mounted network) folder
    /// Http: each file goes to <url>/<file name> with an HTTP PUT (a WebDAV
    /// folder or the team's own upload service); http:// or https://.
    std::string url;
    /// Smtp: the team's own mail server, smtp://host[:port] (STARTTLS when
    /// offered) or smtps://host[:port].
    std::string smtp_server;
    std::string mail_from;
    std::string mail_to;  ///< one or more addresses, separated by commas
    std::string user;     ///< login of the HTTP or SMTP server; "" none
    /// Send the floor's CSV export by itself when a walk test finishes.
    bool on_walk_end = false;
    bool operator==(const SharingSettings&) const = default;
};
const char* sharing_method_key(SharingSettings::Method m);  ///< "off", "folder", "http", "smtp"
std::optional<SharingSettings::Method> parse_sharing_method(std::string_view key);

/// The guided tour's progress (DEMO-11): remembered between runs and
/// reset with the other settings (UI-16).
struct TourProgress {
    bool offered = false;   ///< the first-start offer was answered (shown once)
    std::string building;   ///< the example chosen: "mak_beer_house", ..., "fraunces"; "" none yet
    std::string step;       ///< the step to continue at (tour.h step key); "" the first
    std::vector<std::string> done;  ///< steps done or skipped (keys)
    bool finished = false;  ///< reached the end once
    bool operator==(const TourProgress&) const = default;
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
    Theme theme = Theme::Dark;
    /// UI-12: "en", "nl", "fr", "de" (i18n.h language_key).
    std::string language = "en";
    /// UI-13: touch layout and the UI scale (kMinUiScale .. kMaxUiScale).
    bool touch = false;
    double ui_scale = 1.0;
    SurveySettings survey;
    SoundSettings sound;
    TourProgress tour;
    SharingSettings sharing;

    bool operator==(const AppSettings&) const = default;
};

/// Puts `project` first in the recent list (dropping an older entry of it
/// and the oldest beyond the limit).
void add_recent_project(AppSettings& settings, const std::filesystem::path& project);
void remove_recent_project(AppSettings& settings, const std::filesystem::path& project);
void add_recent_folder(AppSettings& settings, const std::filesystem::path& folder);

/// Why `settings` cannot be used ("" when every value is in range): the
/// settings window's check before it applies a change.
std::string check_settings(const AppSettings& settings);

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
