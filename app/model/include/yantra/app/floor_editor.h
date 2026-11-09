// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wall and antenna editing on a floor of the project window (IND-11,
// IND-12), without drawing: the edit tools, pointer input in plan units,
// the selection, the property forms (wall class or own attenuation;
// antenna type, gain, pattern, height, EIRP, cell), the wall-class table
// (materials) and undo/redo. The ImGui layer (app/gui) draws the tools and
// the overlay and passes the pointer; tests drive this model directly
// (TST-50). Geometry comes from core building/edit.h (hit tests, vertex
// edits, split, aiming); every change is stored at once through
// project::Project (the project file is live).
//
// Undo: every edit is one step holding the floor's walls and antennas, the
// wall-class table and the cells before and after it. Undo and redo put
// one of those states back (Project::update_floor for the floor, set or
// remove for classes and cells), so a step undoes exactly what it did,
// also a drag of many mouse moves. Edits made elsewhere (the plan import
// window's trim, another window) are not in the stack: a step whose floor
// has changed since is dropped instead of undone (undo_note() says so).
//
// Threading: the UI thread.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yantra/app/result.h"
#include "yantra/core/building/model.h"

namespace yantra::project {
class Project;
}

namespace yantra::app {

using building::Point2;

enum class EditTool {
    Pan,      ///< drag pans (no editing)
    Select,   ///< pick and drag walls, vertices and antennas
    Wall,     ///< draw a wall: click the corners, double click or Enter ends it
    Antenna,  ///< click places an antenna
    Aim,      ///< click aims the selected directional antenna or outdoor donor
};
const char* edit_tool_key(EditTool t) noexcept;  ///< "pan", "select", "wall", "antenna", "aim"

struct EditToolView {
    EditTool tool = EditTool::Pan;
    std::string id;  ///< "edit.tool.<key>"
    std::string label;
    std::string tooltip;  ///< what it does and its keys (DOC-07 b)
    bool enabled = true;
    std::string reason;  ///< why disabled (DOC-07 c)
};

struct Selection {
    enum class Kind { None, Wall, Antenna };
    Kind kind = Kind::None;
    std::string id;                     ///< wall id (on the floor) or antenna id
    std::optional<std::size_t> vertex;  ///< a selected wall corner
    bool operator==(const Selection&) const = default;
};

/// The selected wall's properties (IND-11).
struct WallForm {
    std::string wall_id;
    std::string wall_class;  ///< a class of the table
    bool own_value = false;  ///< use attenuation_db instead of the class
    double attenuation_db = 10.0;
};

/// The selected antenna's properties (IND-12).
struct AntennaForm {
    std::string antenna_id;
    std::string name;
    std::string model;
    bool outdoor = false;  ///< outdoor donor/macro direction
    double height_m = 2.7;
    double gain_dbi = 2.0;
    bool directional = false;  ///< sector pattern with azimuth
    double azimuth_deg = 0.0;
    double beamwidth_deg = 65.0;
    double front_to_back_db = 20.0;
    std::optional<double> direction_deg;  ///< outdoor donor: towards the site
    bool eirp_known = false;  ///< else estimated from the measurements (IND-42)
    double eirp_dbm = 10.0;
    double cable_loss_db = 0.0;
    std::string cell;  ///< Cell::id it serves, "" not set
};

/// A row of the wall-class table (materials).
struct MaterialRow {
    std::string id;
    std::string name;
    std::string bands;  ///< "400 MHz: 12.0 dB, 800 MHz: 14.0 dB"
    std::size_t walls = 0;  ///< walls using it (all floors)
    bool default_class = false;
};

/// The material being edited: one attenuation per frequency band.
struct MaterialForm {
    bool is_new = false;
    std::string id;
    std::string name;
    std::vector<building::BandAttenuation> bands;
    std::optional<double> reflection_loss_db;
};

/// A cell an antenna can serve.
struct CellChoice {
    std::string id;
    std::string label;  ///< "Indoor DAS (390.0125 MHz)"
};

class FloorEditor {
public:
    explicit FloorEditor(project::Project& project);

    /// The floor edited (the project window's selected floor). Another
    /// floor clears the selection and the drawing.
    void set_floor(const std::string& floor_id);
    const std::string& floor() const noexcept { return floor_; }

    // -- tools ----------------------------------------------------------------
    EditTool tool() const noexcept { return tool_; }
    Result set_tool(EditTool t);
    std::vector<EditToolView> tools() const;
    /// Whether the left mouse button goes to the tool (else it pans).
    bool wants_left_button() const noexcept { return tool_ != EditTool::Pan; }
    /// The class new walls get (the last one chosen).
    const std::string& draw_class() const noexcept { return draw_class_; }
    Result set_draw_class(const std::string& id);

    // -- pointer input (plan units; tolerance = a few screen pixels) ----------
    void pointer_down(Point2 p, double tolerance);
    void pointer_move(Point2 p, double tolerance);
    void pointer_up(Point2 p, double tolerance);
    /// Double click: ends a wall being drawn; in Select over the selected
    /// wall inserts a corner there.
    void double_click(Point2 p, double tolerance);
    /// The mouse over the plan (rubber band of the wall tool), nullopt when off it.
    void hover(std::optional<Point2> p);
    /// Enter: ends the wall being drawn (>= 2 corners).
    Result finish_wall();
    /// Walls proposed from the plan and accepted (IND-11): ordinary straight
    /// walls of the class for new walls, one undo step; a single one is selected.
    Result add_proposed_walls(const std::vector<std::pair<Point2, Point2>>& segments);
    /// Esc: drops the wall being drawn, else the selection.
    void cancel();
    /// Backspace: removes the last corner of the wall being drawn.
    bool remove_last_corner();

    /// The wall being drawn (corners, then the rubber band to the mouse).
    const std::vector<Point2>& draft() const noexcept { return draft_; }
    const std::optional<Point2>& rubber_band() const noexcept { return hover_; }
    /// A drag in progress: the wall or antenna as it would be (drawn over the stored one).
    const std::optional<building::Wall>& drag_wall() const noexcept { return drag_wall_; }
    const std::optional<Point2>& drag_antenna() const noexcept { return drag_antenna_; }
    const Selection& selection() const noexcept { return selection_; }
    /// The selected wall or antenna as stored (nullopt when none).
    std::optional<building::Wall> selected_wall() const;
    std::optional<building::Antenna> selected_antenna() const;
    /// One line about the selection for the panel ("Wall wall-3: concrete, 12.0 dB, 4.20 m").
    std::string selection_text() const;

    // -- commands on the selection -----------------------------------------------
    /// Removes the selected corner (a wall keeps >= 2), else the wall or antenna.
    Result delete_selected();
    /// Splits the selected wall at its selected inner corner into two walls.
    Result split_selected();
    std::string split_reason() const;  ///< "" when split_selected() can run

    WallForm& wall_form() { return wall_form_; }
    Result apply_wall_form();
    AntennaForm& antenna_form() { return antenna_form_; }
    Result apply_antenna_form();
    /// The cells of the project (for the antenna's cell).
    std::vector<CellChoice> cells() const;
    /// Adds a cell (label, main carrier in MHz, 0 unknown) and lets the
    /// antenna form serve it.
    Result add_cell(const std::string& label, double carrier_mhz);

    // -- materials (the wall-class table, IND-11) ------------------------------
    std::vector<MaterialRow> materials() const;
    /// Starts editing a class ("" for a new one).
    Result edit_material(const std::string& id);
    MaterialForm& material_form() { return material_form_; }
    Result apply_material();
    /// Refused while walls use the class.
    Result remove_material(const std::string& id);
    /// Puts the default classes back to their default values.
    Result reset_materials();

    // -- undo (every edit above is one step) -----------------------------------
    bool can_undo() const noexcept { return !undo_.empty(); }
    bool can_redo() const noexcept { return !redo_.empty(); }
    std::string undo_label() const;  ///< "Undo: move wall" ("" none)
    std::string redo_label() const;
    Result undo();
    Result redo();
    /// Why the last undo/redo dropped steps ("" none).
    const std::string& undo_note() const noexcept { return undo_note_; }

private:
    struct State {
        std::vector<building::Wall> walls;
        std::vector<building::Antenna> antennas;
        std::vector<building::WallClass> classes;
        std::vector<building::Cell> cells;
        bool operator==(const State&) const = default;
    };
    struct Step {
        std::string label;
        std::string floor_id;
        State before, after;
    };
    State capture() const;
    void restore(const State& s);
    /// Runs `change` as one step; the result's message on failure (nothing kept).
    template <class F>
    Result edit(const std::string& label, F&& change);
    Result step(std::vector<Step>& from, std::vector<Step>& to, bool undoing);
    void select(Selection s);
    void load_forms();
    std::optional<building::Floor> floor_data() const;

    project::Project& project_;
    std::string floor_;
    EditTool tool_ = EditTool::Pan;
    std::string draw_class_ = "brick";
    std::vector<Point2> draft_;
    std::optional<Point2> hover_;
    Selection selection_;
    // A drag: where it started, and what is dragged.
    std::optional<Point2> drag_from_;
    bool dragged_ = false;
    std::optional<building::Wall> drag_wall_;
    std::optional<Point2> drag_antenna_;
    WallForm wall_form_;
    AntennaForm antenna_form_;
    MaterialForm material_form_;
    std::vector<Step> undo_, redo_;
    std::string undo_note_;
};

}  // namespace yantra::app
