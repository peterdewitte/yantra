// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host-side check of a plugin descriptor and its kind tables (PLG-02..08,
// PLG-10, API-04): turns what a plugin's entry point returned into checked
// C++ data, or into a list of clear reasons why the plugin is refused.
// Loading the shared library itself is the loader's job (PLG-09); this
// file has no app, UI toolkit or platform dependency.
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plugin/
// descriptor.h and ui.h (UiInfo), adapted for plugin API 1.0 (no radio
// kind, technology rules of API-04, translations).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/plugin/schema.h"

namespace yantra::plugin {

struct Band {
    std::string id;
    std::string label;
    std::uint64_t first_hz = 0;
    std::uint64_t last_hz = 0;
    std::uint64_t raster_hz = 0;
    std::uint64_t first_channel = 0;
    std::uint64_t channel_count() const noexcept {
        return raster_hz == 0 ? 0 : (last_hz - first_hz) / raster_hz + 1;
    }
};

struct FieldLink {
    std::string neighbour_field;
    std::string cell_field;
};

struct TechnologyInfo {
    std::string technology_id;  // stored with data (PLG-12); = yr_descriptor::technology_id
    std::string label;
    double channel_bandwidth_hz = 0.0;
    std::vector<Band> bands;
    std::vector<Schema> schemas;
    std::size_t cell_schema = 0;                     // index of the cell list schema
    std::optional<std::size_t> neighbour_schema;     // index, when declared
    std::vector<FieldLink> links;                    // neighbour cross-check (DB-05)
    std::vector<EnumItem> serving_states;            // DB-08
    bool has_frequency_to_channel = false;
    bool has_channel_to_frequency = false;
    bool has_c1 = false;          // RF-06
    bool has_reselection = false; // DB-08
    bool has_values_match = false;// DB-04
};

struct FeatureInfo {
    std::string feature_id;
    std::string label;
    std::vector<Schema> schemas;  // RECORDING schemas
    std::vector<std::string> required_plugins;
    bool has_on_record = false;
};

struct ExportFormat {
    std::string id;
    std::string label;
    std::string extension;
    std::string help;
    std::uint32_t flags = 0;
};

struct ExporterInfo {
    std::vector<ExportFormat> formats;
};

struct UiWindow {
    std::string id;
    std::string title;
    std::string help;
    std::string manual_section;
    std::uint32_t kind = YANTRA_UI_PANEL;
    bool open_at_start = false;
};

struct UiMenuItem {
    std::string id;
    std::uint32_t menu = YANTRA_MENU_VIEW;
    std::string submenu;
    std::string label;
    std::string tooltip;
    std::string shortcut;
    std::string window_id;  // empty: menu_selected is called
    std::uint32_t flags = 0;
};

// Panels, windows, dialogs and menu entries of a plugin (PLG-06).
struct UiInfo {
    std::vector<UiWindow> windows;
    std::vector<UiMenuItem> menu_items;
    bool empty() const noexcept { return windows.empty() && menu_items.empty(); }
};

struct Translation {
    std::string key;
    std::string lang;
    std::string text;
};

// Everything the host needs to know about a plugin, checked.
struct PluginInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string licence;  // SPDX expression as declared (PLG-10)
    std::string description;
    std::string source_url;
    yantra_api_version api_version{};
    std::uint32_t kinds = 0;            // YANTRA_KIND_* bits
    std::uint32_t data_categories = 0;  // YANTRA_CATEGORY_* bits
    std::optional<TechnologyInfo> technology;
    std::optional<FeatureInfo> feature;
    std::optional<ExporterInfo> exporter;
    UiInfo ui;
    std::vector<Translation> translations;
    bool has_on_timer = false;
};

// True for a plugin id: lower case reverse domain style
// `[a-z0-9]+([._-][a-z0-9]+)*`, at most 128 characters ("org.example.ctest").
bool is_plugin_id(const std::string& id) noexcept;

// Checks a descriptor against the host's API version (PLG-04) and the
// rules of plugin.h. The version is checked first, from the frozen prefix;
// an incompatible plugin is refused before anything else is read.
// `source` names the plugin in messages (file name).
Parsed<PluginInfo> parse_descriptor(const yantra_plugin_descriptor* descriptor,
                                    yantra_api_version host, const std::string& source);

}  // namespace yantra::plugin
