// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The thin ImGui layer of the app shell (UI-13, UI-14, UI-15 hook, UI-17,
// DOC-07): draws what AppState returns and calls its commands. No logic
// here that a test would need; that lives in app/model. It needs an ImGui
// context only (no window system, no OpenGL), so the headless UI tests
// (tests/app/harness) draw it exactly as the app does.
//
// Window and item ids are "<label>###<stable id>" (ids: docs/APP.md), so
// tests and the screenshot script find them whatever the language.
// Copied from yantra-v2 68ea20a: app/src/gui.{h,cpp}, adapted (shell of
// step 9.1; project window 9.2, plan import 9.3; no radio list yet).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "plan_canvas.h"
#include "plan_import_view.h"
#include "user_texture.h"
#include "yantra/app/app_state.h"

namespace yantra::app {

class Gui {
public:
    explicit Gui(AppState& state);
    /// Destroy after the renderer backend's shutdown (user_texture.h).
    ~Gui();
    Gui(const Gui&) = delete;
    Gui& operator=(const Gui&) = delete;
    /// Applies the theme and sizes (call once after ImGui is set up).
    static void apply_style(Theme theme = Theme::Dark);
    /// The theme's colours only (the settings window switches it).
    static void apply_theme(Theme theme);
    /// Draws one frame (between ImGui::NewFrame and ImGui::Render).
    void draw();

    /// Ids of the fixed windows (tests, screenshots).
    static constexpr const char* kMainWindow = "###main";
    static constexpr const char* kStatusBar = "###status_bar";

    /// Screenshots and tests: draw the plan only once its tiles are there.
    void set_wait_for_tiles(bool on);
    /// The project window's plan view (tests: what it drew).
    const PlanCanvas& canvas() const { return *canvas_; }
    /// The plan import window's view (tests: what it drew).
    const PlanImportView& import_view() const { return import_view_; }
    /// What the editing and walk-test overlay drew in the last frame (tests).
    struct ToolOverlay {
        std::size_t draft_corners = 0;  ///< corners of the wall being drawn
        bool selection = false;         ///< the selection outline
        std::string hint;               ///< the walk-test hint on the plan (DEMO-14)
        bool move_slower = false;       ///< the DEMO-14 warning
        bool progress = false;          ///< an interpolation progress bar in the panel
    };
    const ToolOverlay& tool_overlay() const { return overlay_; }
    /// What the survey panels and the status bar drew in the last frame (tests).
    struct SurveyOverlay {
        bool panels = false;              ///< the panels under the plan are shown
        std::string tab;                  ///< "cells", "graphs", "scanner", "events", "stats"
        std::string serving;              ///< the serving cell line ("" none)
        std::vector<std::string> large;   ///< UI-01 large values, "label value"
        std::size_t neighbour_rows = 0;
        std::size_t listed_only_rows = 0;
        std::size_t scan_rows = 0;
        std::size_t event_rows = 0;
        std::size_t graph_samples = 0;    ///< history samples drawn
        std::size_t graph_traces = 0;     ///< cells drawn
        std::size_t threshold_lines = 0;
        bool cursor = false;              ///< the graphs show a cursor
        bool interference_banner = false; ///< NFR-07 warning shown
        std::string stats;                ///< the statistics panel's first line
        AppState::StatusBar status;       ///< UI-08
    };
    const SurveyOverlay& survey_overlay() const { return survey_; }
    /// What the About dialog drew in the last frame (tests).
    struct AboutOverlay {
        bool shown = false;
        bool yantra_logo = false;  ///< the Yantra logo texture is drawn (BRD-06)
        bool curtat_logo = false;  ///< the curtat.be logo texture is drawn (BRD-03)
        std::string curtat_file;   ///< which PNG (light/dark by theme)
        std::size_t libraries = 0, data = 0, plugins = 0;  ///< credit and plugin rows
    };
    const AboutOverlay& about_overlay() const { return about_; }
    /// State a plugin window keeps between frames (PLG-06).
    struct PluginWindowState {
        std::string confirm, confirm_text;  ///< a CONFIRM button asks
        std::string confirmed;              ///< answered yes: reported next frame
        std::map<std::string, std::string> values;       ///< value() texts by control id (tests)
        std::map<std::string, std::uint64_t> table_rows;  ///< table() rows by control id (tests)
    };
    /// What the plugin windows drew in the last frame (tests).
    struct PluginFrame {
        std::string id;
        bool drawn = false;
        int controls = 0;
    };
    const std::vector<PluginFrame>& plugin_frame() const { return plugin_frame_; }
    const PluginWindowState* plugin_window_state(const std::string& id) const {
        const auto it = plugin_windows_.find(id);
        return it == plugin_windows_.end() ? nullptr : &it->second;
    }

private:
    void draw_menu_bar();
    void handle_shortcuts();
    void draw_start_screen();
    void draw_project_screen();
    void draw_floor_tabs(ProjectWindow& w);
    void draw_layer_panel(ProjectWindow& w, const FloorContent* c);
    void draw_floor_properties();
    void draw_remove_floor();
    void draw_status_bar();
    void draw_dialogs();
    void draw_dialog_body(Dialog d);
    void draw_new_project();
    void draw_browser();
    void draw_add_floor();
    void draw_plan_import();
    void draw_about();
    void draw_logo(UserTexture& tex, std::string& loaded, const std::filesystem::path& file, float size);
    // Settings window and Reset all settings (settings_view.cpp, step 9.7).
    void draw_settings();
    void draw_settings_general(AppSettings& f);
    void draw_settings_survey(AppSettings& f);
    void draw_plugin_manager();
    void draw_reset_settings();
    // The plugins' own menus and windows (plugin_view.cpp, PLG-06).
    void draw_plugin_menus();
    void draw_plugin_entries(std::uint32_t menu);
    void draw_plugin_windows();
    void restart_ui();
    // Editing, walk test and interpolation (gui_tools.cpp, step 9.4).
    void draw_edit_section();
    void draw_walk_section(const FloorContent* c);
    void draw_interpolation_section(const FloorContent* c);
    void draw_plan_overlay(ImDrawList* dl);
    void handle_canvas_input();
    void draw_antenna_dialog();
    void draw_materials_dialog();
    void draw_propagation_dialog();
    // Survey panels, recording and replay (survey_view.cpp, step 9.5).
    void draw_survey_section();
    void draw_recording_controls(LiveSurvey& live);
    void draw_replay_controls();
    void draw_survey_panels(float height);
    void draw_cells_panel(const SurveyFrame& f);
    void draw_graphs_panel();
    void draw_scanner_panel(const SurveyFrame& f);
    void draw_events_panel();
    void draw_stats_panel();
    void draw_status_items();
    void run(Action a);
    // Radio windows (step 9.6, radio_view.cpp).
    void draw_radio_menu();
    void draw_radio_windows();
    void draw_radio_list(RadioWindows& rw);
    void draw_radio_settings(RadioWindows& rw);
    void draw_setting_row(RadioWindows& rw, const SettingRow& r);
    void draw_radio_debug(RadioWindows& rw);
    void draw_radio_calibration(RadioWindows& rw);
    void draw_radio_info(RadioWindows& rw);
    void draw_radio_confirmation(RadioWindows& rw);

    AppState& state_;
    Dialog shown_ = Dialog::None;  // the dialog whose popup is open
    std::unique_ptr<PlanCanvas> canvas_;
    PlanImportView import_view_;
    std::string shown_floor_;  // the floor tab shown last frame
    std::string tab_order_;    // the floor ids of the tabs, in order
    ToolOverlay overlay_;
    bool material_editing_ = false;  // the materials dialog shows its form
    std::string new_cell_name_;
    double new_cell_mhz_ = 0.0;
    // Survey panels (step 9.5).
    SurveyOverlay survey_;
    bool panels_ = false;          // the panels under the plan
    bool panels_auto_ = true;      // shown by themselves when a radio or replay starts
    bool was_active_ = false;
    std::string marker_label_;
    std::string ring_name_;
    std::string cut_name_;
    int replay_pick_ = -1;
    double replay_speed_ = 1.0;
    bool graph_level_ = true, graph_c1_ = true, graph_quality_ = true;
    std::vector<std::string> threshold_on_;  // threshold lines shown (field names)
    PlotData plot_;                          // copied when the monitor's version changes
    std::uint64_t plot_version_ = ~0ull;
    // Radio windows (step 9.6): the input fields' text while typed.
    std::map<std::string, std::string> setting_text_;
    std::map<std::string, std::string> setting_input_error_;
    std::string setting_editing_;  // the field being typed into
    std::string calibration_file_;
    // Step 9.7: theme in use, UI restarts, About logos, plugin windows.
    Theme theme_ = Theme::Dark;
    std::uint64_t ui_generation_ = 0;
    AboutOverlay about_;
    UserTexture yantra_logo_, curtat_logo_;
    std::string yantra_logo_file_, curtat_logo_file_;  // the files the textures hold
    std::map<std::string, PluginWindowState> plugin_windows_;
    std::vector<PluginFrame> plugin_frame_;
};

/// The ImGui popup id of a dialog ("New indoor project###dlg.new_project").
const char* dialog_id(Dialog d, const AppState& state);

}  // namespace yantra::app
