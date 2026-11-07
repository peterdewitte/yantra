// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plugins' own UI (PLG-06), apart from drawing: the menu entries and
// windows (panels, windows, dialogs) that enabled plugins of the plugin
// API declare in their yantra_plugin_ui_api, which of them are open, the
// plugins' window requests (yantra_host_api::ui_open_window /
// ui_close_window), menu choices (window or menu_selected), the current
// menu state (menu_state) and the plugins' translations (UI-12). The
// ImGui layer (app/gui/plugin_view.cpp) draws the menus and windows and
// implements the drawing table (yantra_ui_api) on top of this model.
// Radio plugins have no UI code (PLG-16); this is for plugin.h plugins.
// All calls run on the UI (main) thread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <yantra/plugin.h>

#include "yantra/app/result.h"

namespace yantra::plugin {
class PluginLoader;
}

namespace yantra::app {

/// One menu entry a plugin adds, with its state now.
struct PluginMenuEntry {
    std::string plugin_id, item_id;
    std::string id;  ///< ImGui id "plg.<plugin id>.<item id>"
    std::uint32_t menu = YANTRA_MENU_TOOLS;
    std::string submenu, label, tooltip, shortcut;  ///< translated
    std::string window_id;                           ///< "" = menu_selected
    bool enabled = true, checked = false;
    std::string disabled_reason;  ///< translated
};

/// An open plugin window.
struct PluginWindowView {
    std::string plugin_id, window_id;
    std::string id;  ///< ImGui id "plg.<plugin id>.<window id>"
    std::uint32_t kind = YANTRA_UI_WINDOW;
    std::string title, help;  ///< translated
    double width = 0.0, height = 0.0;
};

/// Text of a YANTRA_MENU_* value: "file", "view", "radio", "tools", "help".
const char* plugin_menu_key(std::uint32_t menu);

class PluginUi {
public:
    explicit PluginUi(plugin::PluginLoader& loader);
    PluginUi(const PluginUi&) = delete;
    PluginUi& operator=(const PluginUi&) = delete;

    /// Each frame: applies the plugins' window requests and closes the
    /// windows of plugins that are no longer enabled. A plugin that was
    /// enabled since the last call opens its OPEN_AT_START windows.
    void tick();

    /// The entries of one top-level menu (YANTRA_MENU_*), in plugin and
    /// declaration order, with their state (menu_state when the plugin has it).
    std::vector<PluginMenuEntry> menu(std::uint32_t menu) const;
    bool has_menu(std::uint32_t menu) const;
    /// A menu entry was chosen: opens its window or calls menu_selected.
    Result select(const std::string& plugin_id, const std::string& item_id);

    Result open(const std::string& plugin_id, const std::string& window_id);
    /// Closes a window and tells the plugin (window_closed).
    void close(const std::string& plugin_id, const std::string& window_id);
    bool is_open(const std::string& plugin_id, const std::string& window_id) const;
    const std::vector<PluginWindowView>& open_windows() const { return open_; }

    /// Calls the plugin's draw for an open window with Yantra's drawing table.
    void draw(const PluginWindowView& window, const yantra_ui_api& api) const;

    /// The plugin's translation of an English text into the UI language,
    /// or the text itself.
    std::string translate(const std::string& plugin_id, const std::string& english) const;
    void set_language(std::string language);  ///< "en" (default), "nl", "fr", "de"
    const std::string& language() const { return language_; }

private:
    bool enabled(const std::string& plugin_id) const;

    plugin::PluginLoader& loader_;
    std::string language_ = "en";
    std::vector<PluginWindowView> open_;
    std::vector<std::string> enabled_;  // enabled plugins at the last tick
};

}  // namespace yantra::app
