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

#include <memory>
#include <string>

#include "plan_canvas.h"
#include "plan_import_view.h"
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
    static void apply_style();
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
    // Editing, walk test and interpolation (gui_tools.cpp, step 9.4).
    void draw_edit_section();
    void draw_walk_section(const FloorContent* c);
    void draw_interpolation_section(const FloorContent* c);
    void draw_plan_overlay(ImDrawList* dl);
    void handle_canvas_input();
    void draw_antenna_dialog();
    void draw_materials_dialog();
    void draw_propagation_dialog();
    void run(Action a);

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
};

/// The ImGui popup id of a dialog ("New indoor project###dlg.new_project").
const char* dialog_id(Dialog d, const AppState& state);

}  // namespace yantra::app
