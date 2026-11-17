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
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "plan_canvas.h"
#include "plan_import_view.h"
#include "user_texture.h"
#include "yantra/app/app_state.h"

namespace yantra::app {

/// The look of the UI (UI-13): theme colours, desktop or touch sizes, the
/// UI scale.
struct GuiStyle {
    Theme theme = Theme::Dark;
    bool touch = false;
    float scale = 1.0F;
    bool operator==(const GuiStyle&) const = default;
};

/// A piece of the status bar: the message or an item (Gui::status_pieces).
struct StatusPiece {
    std::string text;
    std::uint32_t colour = 0;  ///< ImU32; 0: the text colour
};

class Gui {
public:
    explicit Gui(AppState& state);
    /// Destroy after the renderer backend's shutdown (user_texture.h).
    ~Gui();
    Gui(const Gui&) = delete;
    Gui& operator=(const Gui&) = delete;
    using Style = GuiStyle;
    static Style style_of(const AppSettings& s);
    /// Applies a style to the current ImGui context (call once after ImGui
    /// is set up; draw() follows the settings afterwards).
    static void apply_style(const Style& style = Style{});
    /// Draws one frame (between ImGui::NewFrame and ImGui::Render).
    void draw();

    /// Ids of the fixed windows (tests, screenshots).
    static constexpr const char* kMainWindow = "###main";
    static constexpr const char* kStatusBar = "###status_bar";

    /// Screenshots and tests: draw the plan only once its tiles are there.
    void set_wait_for_tiles(bool on);
    /// Screenshots (SCR-02, NFR-09): show no folder of this machine; the
    /// radio info window's plugin file reads "<plugins>/<file name>".
    void set_screenshot_mode(bool on) { screenshot_mode_ = on; }
    bool screenshot_mode() const { return screenshot_mode_; }
    /// The radio info window's "File" row: the plugin's path, or in
    /// screenshot mode "<plugins>/<file name>".
    static std::string shown_plugin_file(const std::filesystem::path& path, bool screenshot_mode);
    /// The project window's plan view (tests: what it drew).
    const PlanCanvas& canvas() const { return *canvas_; }
    /// The plan import window's view (tests: what it drew).
    const PlanImportView& import_view() const { return import_view_; }
    /// What the walk-test overlay drew in the last frame (tests).
    struct ToolOverlay {
        std::string hint;               ///< the walk-test hint on the plan (DEMO-14)
        bool move_slower = false;       ///< the DEMO-14 warning
        bool progress = false;          ///< an interpolation progress bar in the panel
    };
    const ToolOverlay& tool_overlay() const { return overlay_; }
    /// What the survey panels and the status bar drew in the last frame (tests).
    struct SurveyOverlay {
        /// The survey windows drawn (UI-18): "neighbours", "graphs",
        /// "scanner", "events", "statistics".
        std::vector<std::string> windows;
        bool live_values = false;         ///< the Walk tab's live values (UI-01)
        bool compact_graph = false;       ///< the Walk tab's level graph of the serving cell (UI-04)
        bool advanced_open = false;       ///< the Map tab's Advanced dropdown is open (UI-19)
        bool shown(std::string_view w) const {
            for (const std::string& x : windows)
                if (x == w) return true;
            return false;
        }
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
    /// The post-processing figures as last drawn (Map tab, Advanced; step 9.8 tests).
    const PostStatsView& post_view() const { return post_view_; }
    /// What the About dialog drew in the last frame (tests).
    struct AboutOverlay {
        bool shown = false;
        bool yantra_logo = false;  ///< the Yantra logo texture is drawn (BRD-06)
        bool curtat_logo = false;  ///< the curtat.be logo texture is drawn (BRD-03)
        std::string curtat_file;   ///< which PNG (light/dark by theme)
        std::size_t libraries = 0, data = 0, plugins = 0;  ///< credit and plugin rows
    };
    const AboutOverlay& about_overlay() const { return about_; }
    /// What the guided tour drew in the last frame (tests, step 9.10).
    struct TourOverlay {
        bool window = false;     ///< the tour window
        bool strip = false;      ///< the tour's strip inside a dialog
        bool offer = false;      ///< the first-start offer on the start screen
        bool consent = false;    ///< the download question (DEMO-12)
        bool banner = false;     ///< the DEMO label in the tour (DEMO-07)
        std::string target;      ///< the control pointed at ("" none drawn)
        std::string folder;      ///< the download folder shown (DEMO-11)
        bool credit = false;     ///< a MAK sheet's credit with the plan (DEMO-12)
        std::string credit_text;
    };
    const TourOverlay& tour_overlay() const { return tour_; }
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
    /// The project window's tab ("walk", "map"; UI-18).
    std::string page_ = "walk";
    void draw_menu_bar();
    void handle_shortcuts();
    void draw_start_screen();
    void draw_project_screen();
    void draw_floor_tabs(ProjectWindow& w);
    void draw_floor_properties();
    void draw_remove_floor();
    // The status bar: the message, then the items (radio, recording, ...)
    // on as many lines as they need (TST-52: nothing cut at the window edge).
    std::vector<StatusPiece> status_pieces();
    static float status_height(const std::vector<StatusPiece>& pieces, float width);
    void draw_status_bar(const std::vector<StatusPiece>& pieces, float height);
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
    void draw_settings_sound(AppSettings& f);
    // Step 9.9: help (DOC-07 e) and the level alerts' popup (UI-10).
    void help_button(const char* section);
    void draw_help_body();
    void draw_help_window();
    void draw_help_popup();
    void draw_alerts_window();
    void draw_plugin_manager();
    void draw_reset_settings();
    // The plugins' own menus and windows (plugin_view.cpp, PLG-06).
    void draw_plugin_menus();
    void draw_plugin_entries(std::uint32_t menu);
    void draw_plugin_windows();
    void restart_ui();
    // Walk test and interpolation (gui_tools.cpp, step 9.4).
    void draw_walk_page(const FloorContent* c);
    void draw_walk_section(const FloorContent* c);
    void draw_map_page(ProjectWindow& w, const FloorContent* c);
    void draw_interpolation_section(const FloorContent* c);
    void draw_interpolation_settings();
    void draw_plan_overlay(ImDrawList* dl);
    void handle_canvas_input();
    void draw_interpolation_dialog();
    // Survey panels, recording and replay (survey_view.cpp, step 9.5).
    void draw_radio_controls(LiveSurvey& live);
    void draw_live_values(const SurveyFrame& f);
    void draw_post_section();  // step 9.8, post_view.cpp (in the Map tab's Advanced dropdown)
    void draw_export_dialog();  // step 10.4, report_view.cpp
    void draw_report_dialog();
    void draw_profiles_dialog();
    void draw_sharing_dialog();
    void draw_recording_controls(LiveSurvey& live);
    void draw_replay_controls();
    void draw_survey_windows();
    void draw_cells_panel(const SurveyFrame& f);
    /// The graphs (UI-04); `compact`: the Walk tab's level graph of the
    /// serving cell only, `height` high.
    void draw_graphs_panel(bool compact = false, float height = 0.0F);
    void draw_scanner_panel(const SurveyFrame& f);
    void draw_events_panel();
    void draw_stats_panel();
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
    // Network menu windows (step 9.8, network_view.cpp).
    void draw_network_windows();
    void draw_cell_db(CellDbEditor& ed);
    void draw_verification(NetworkChecks& nc);
    void draw_reselection(NetworkChecks& nc);
    // The guided tour (step 9.10, tour_view.cpp).
    void draw_tour();
    void draw_tour_body(const TourView& v, bool strip);
    void draw_tour_consent();
    void draw_tour_offer();
    void draw_tour_highlight(const TourView& v);
    void draw_plan_credit(const std::string& floor_id);

    AppState& state_;
    Dialog shown_ = Dialog::None;  // the dialog whose popup is open
    std::unique_ptr<PlanCanvas> canvas_;
    PlanImportView import_view_;
    std::string shown_floor_;  // the floor tab shown last frame
    std::string tab_order_;    // the floor ids of the tabs, in order
    ToolOverlay overlay_;
    // Survey panels (step 9.5).
    SurveyOverlay survey_;
    bool screenshot_mode_ = false;  // set_screenshot_mode
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
    // Step 9.8: the Network windows' typed text.
    struct NetworkText {
        std::string filter, csv_file, log_file;
        std::map<std::string, std::string> match_text;
        bool show_matches = true;
        bool log_not_seen = false;
    };
    NetworkText network_;
    PostStatsView post_view_;  // the post-processing figures (Map tab, Advanced), last frame
    // Step 9.7: theme in use, UI restarts, About logos, plugin windows.
    Style style_;
    std::string help_section_;  // F1: the help of the focused window (last frame)
    std::uint64_t ui_generation_ = 0;
    AboutOverlay about_;
    UserTexture yantra_logo_, curtat_logo_;
    std::string yantra_logo_file_, curtat_logo_file_;  // the files the textures hold
    std::map<std::string, PluginWindowState> plugin_windows_;
    std::vector<PluginFrame> plugin_frame_;
    TourOverlay tour_;
};

/// The ImGui popup id of a dialog ("New indoor project###dlg.new_project").
const char* dialog_id(Dialog d, const AppState& state);

}  // namespace yantra::app
