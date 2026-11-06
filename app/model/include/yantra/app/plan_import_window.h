// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plan import window's editing state (IND-02, IND-03, IND-05..IND-10),
// apart from drawing (TST-50): the ImGui layer (app/gui/plan_import_view)
// passes pointer input in page units and draws what this model returns.
//
// Over plan::PlanImport (core) it adds what a window needs:
// - tools: pan/select, rectangle (drag; drag a corner to adjust),
//   polygon (click points, close on the first point or Finish; drag a
//   corner to adjust), deskew line, scale line (two points + distance),
//   scale bar, reference point;
// - a live preview of the current trim as a floor (PlanImport::preview);
// - "Add as floor" as a small form (name, level, height) with suggested
//   values; the window stays open for the next floor (IND-06);
// - the trims already added, outlined and labelled, selectable, editable
//   (IND-07: Apply or Cancel) and removable with confirmation;
// - the scale of the selected floor (IND-08): two points and their real
//   distance, the plan's scale bar, or the drawing scale (1:N) when the
//   page has a physical size (PDF, an image with a stored resolution);
//   floors of one sheet can share it; "not to scale" is allowed and
//   marked. The alignment point (IND-09, S) is set here too, since it is
//   picked on the sheet like the scale;
// - licence-restricted sources (IND-03, DEMO-12): a file whose SHA-256 is
//   a licence-restricted sheet of a bundled example description is
//   referenced (never copied into the project), and the window shows its
//   credit, licence and the "approximate scale" note (restricted()).
//
// Positions passed in are page units of the current page (the GUI maps
// screen -> page with view()); scales and reference points are stored in
// floor units (plan units of the floor), as the building model wants.
//
// Threading: the UI thread.
// Copied from yantra-v2 68ea20a: app/model/include/yantra/app/plan_import_window.h,
// adapted (string ids, building::Scale, core reanchoring, drawing scale,
// live preview, restricted-source credits).
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yantra/app/result.h"
#include "yantra/core/plan/import.h"

namespace yantra::project {
class Project;
}

namespace yantra::app {

using building::Point2;

enum class ImportTool {
    Pan,        ///< drag to pan; click a trim to select its floor
    Rectangle,  ///< drag a rectangle; drag a corner of the current one
    Polygon,    ///< click the corners; drag a corner of the current one
    Deskew,     ///< drag along a line that should be straight
    ScaleLine,  ///< two points with a known distance (IND-08)
    ScaleBar,   ///< the plan's scale bar, end to end (IND-08)
    Reference,  ///< click the floor's alignment point (IND-09)
};

/// A tool button: label, tooltip (DOC-07 b), enabled with the reason.
struct ToolView {
    ImportTool tool = ImportTool::Pan;
    std::string id;  ///< "tool.rectangle" (ImGui id "pi.tool.rectangle")
    std::string label;
    std::string tooltip;
    bool enabled = true;
    std::string disabled_reason;
};

/// A trim already added as a floor, as drawn on the source (IND-06).
struct PlacedView {
    std::string floor_id;
    std::string label;  ///< "Ground floor (level 0)"
    int page = 0;
    std::vector<Point2> outline;  ///< page units
    Point2 label_at;              ///< page units
    bool on_page = false;         ///< on the page shown
    bool selected = false;
    bool editing = false;
    bool to_scale = false;
    bool aligned = false;
    std::string scale_text;   ///< "0.025 m per unit (scale bar 2.5 m)" or "not to scale"
    std::string shared_with;  ///< names of the floors sharing its scale, "" when none
    /// The stored scale line (two points or scale bar) and alignment point on the page.
    std::optional<std::pair<Point2, Point2>> scale_line;
    std::optional<Point2> reference;
};

/// What is being drawn now (page units), for the overlay.
struct DraftView {
    std::vector<Point2> trim;  ///< the current trim's outline
    bool trim_is_polygon = false;
    std::vector<Point2> polygon;                    ///< polygon points clicked so far
    std::optional<std::pair<Point2, Point2>> line;  ///< deskew or scale line being drawn
    std::vector<Point2> handles;                    ///< corners that can be dragged
};

/// The "Add as floor" form (IND-06).
struct ImportFloorForm {
    std::string name;
    int level = 0;
    double height_m = 3.0;
};

/// Scale entry of the selected floor (IND-08).
struct ScaleForm {
    enum class Kind { TwoPoints, ScaleBar, DrawingScale };
    Kind kind = Kind::TwoPoints;
    std::optional<Point2> a;  ///< page units (TwoPoints, ScaleBar)
    std::optional<Point2> b;
    std::string distance_m;   ///< as typed ("3", "2,5")
    std::string ratio;        ///< DrawingScale: "100" or "1:100"
};

/// A floor of the same sheet that can share the selected floor's scale.
struct ShareChoice {
    std::string floor_id;
    std::string label;
    bool shared = false;  ///< already follows the selected floor's scale
};

/// Credit and licence of a licence-restricted source (IND-03, DEMO-12),
/// shown with the plan; such a source is never copied into the project,
/// a package or an export.
struct RestrictedView {
    std::string title;          ///< the sheet's title
    std::string credit;         ///< "MAK - Museum of Applied Arts, Vienna; Josef Frank and Oskar Wlach, 1929-1930, inv. KI 23126-36"
    std::string licence;        ///< "CC BY-NC-ND 4.0"
    std::string licence_url;
    std::string item_url;
    bool scale_approximate = false;  ///< the example floors' scales are approximate (DEMO-12)
    std::string note;           ///< what Yantra does with the file (reference only)
};

/// Parses a distance in metres as typed: "3", "2.5", "2,5", " 4 m";
/// nullopt with `error` for anything else, <= 0, or not finite.
std::optional<double> parse_distance_m(const std::string& text, std::string& error);
/// Parses a drawing scale as typed: "100", "1:100", "1 : 50"; nullopt with
/// `error` unless it is a finite number >= 1.
std::optional<double> parse_ratio(const std::string& text, std::string& error);

/// The licence-restricted sheet of a bundled example description
/// (<folder>/demo/*/*.json in `data_folders`) with this SHA-256, as a
/// credit view; nullopt when there is none.
std::optional<RestrictedView> find_restricted_sheet(const std::vector<std::filesystem::path>& data_folders,
                                                    const std::string& sha256);

class PlanImportWindow {
public:
    /// A new plan file (IND-10): copied into the project (IND-03) and
    /// opened. A file that is a licence-restricted example sheet
    /// (find_restricted_sheet in `data_folders`) is added as a reference
    /// with its SHA-256 instead of a copy. nullptr with `error` (the
    /// reason, IND-02) when it cannot be read; nothing is then kept.
    static std::unique_ptr<PlanImportWindow> open_file(project::Project& project, const std::filesystem::path& file,
                                                       const std::vector<std::filesystem::path>& data_folders,
                                                       std::string& error, const plan::OpenLimits& limits = {});
    /// A source the project already has (IND-10, IND-07).
    static std::unique_ptr<PlanImportWindow> open_source(project::Project& project, const std::string& source_id,
                                                         const std::vector<std::filesystem::path>& data_folders,
                                                         std::string& error, const plan::OpenLimits& limits = {});

    plan::PlanImport& import() noexcept { return *import_; }
    const plan::PlanImport& import() const noexcept { return *import_; }
    plan::PlanView& view() noexcept { return import_->view(); }

    std::string title() const;        ///< the source's file name
    std::string description() const;  ///< "PNG image, 400 x 300 px" / "PDF, 3 pages, page 1: ..."
    /// True when the page needs tiled rendering (> 15000 px on a side at
    /// the base scale, IND-02); the GUI always tiles, this is for the note.
    bool large_sheet() const;
    /// Set for a licence-restricted source (IND-03, DEMO-12).
    const std::optional<RestrictedView>& restricted() const noexcept { return restricted_; }
    /// The source is a reference, not a copy (embedded false).
    bool referenced() const;

    // -- pages (IND-02) --------------------------------------------------
    int page_count() const { return import_->page_count(); }
    int page() const noexcept { return import_->page(); }
    std::string page_label(int page) const;  ///< "Page 2 (1 floor)"
    Result set_page(int page);

    // -- tools ------------------------------------------------------------
    ImportTool tool() const noexcept { return tool_; }
    std::vector<ToolView> tools() const;
    /// Switches tools; an unfinished polygon or line is dropped.
    Result set_tool(ImportTool tool);
    /// What to do with the current tool (shown under the canvas).
    std::string hint() const;
    /// Page units that count as "on" a point (the GUI sets 8 screen px).
    void set_pick_radius(double page_units) noexcept { pick_ = page_units; }

    // -- pointer input (page units) -----------------------------------------
    /// Returns true when the press starts something (the GUI then does
    /// not pan).
    bool pointer_down(Point2 p);
    void pointer_move(Point2 p);
    Result pointer_up(Point2 p);
    /// Polygon: closes the outline (3 or more points).
    Result finish_polygon();
    /// Polygon: removes the last point.
    void undo_point();
    /// Drops an unfinished polygon or line (Esc).
    void cancel_draft();
    bool drafting() const noexcept;
    DraftView draft() const;

    // -- rotation (IND-05) ------------------------------------------------
    Result rotate(int quarter_steps);
    Result set_deskew(double deg);
    double deskew() const noexcept { return import_->trim().deskew_deg; }
    int quarter_turns() const noexcept { return import_->trim().quarter_turns; }
    Result reset_trim();
    std::string trim_text() const;  ///< "Rectangle 200 x 200 units, turned 90.00 deg"
    /// Changes whenever the current trim or page does (the GUI re-renders
    /// its live preview then).
    unsigned trim_generation() const noexcept { return trim_gen_; }

    // -- add as floor (IND-06) --------------------------------------------
    /// Opens the form with a suggested name and the next free level.
    void begin_add_floor();
    ImportFloorForm* add_form() noexcept { return add_form_ ? &*add_form_ : nullptr; }
    /// Adds the current trim as a floor; the floor is selected for its
    /// scale, the window stays on the source.
    Result confirm_add_floor();
    void cancel_add_floor() noexcept { add_form_.reset(); }
    /// Floor ids added in this window, in order (the project window shows
    /// the last one when the window closes).
    const std::vector<std::string>& added() const noexcept { return added_; }

    // -- trims already added (IND-06, IND-07) ------------------------------
    std::vector<PlacedView> placed() const;
    const std::string& selected() const noexcept { return selected_; }
    /// Selects a floor of this source (its trim is highlighted; scale and
    /// reference apply to it); shows its page.
    Result select_floor(const std::string& floor_id);
    /// The floor whose trim (on the current page) contains the point.
    std::optional<std::string> floor_at(Point2 page_point) const;
    /// Loads a floor's trim as the current trim (IND-07).
    Result edit_floor(const std::string& floor_id);
    const std::optional<std::string>& editing() const noexcept { return import_->editing(); }
    /// Stores the edited trim; walls, antennas, scale points and the
    /// reference point keep their place on the sheet (plan::reanchor_floor).
    Result apply_edit();
    Result cancel_edit();
    Result request_remove(const std::string& floor_id);
    const std::optional<std::string>& pending_remove() const noexcept { return pending_remove_; }
    std::string remove_question() const;
    Result confirm_remove();
    void cancel_remove() noexcept { pending_remove_.reset(); }

    // -- scale and alignment (IND-08, IND-09) ------------------------------
    ScaleForm& scale_form() noexcept { return scale_; }
    /// The drawing scale (1:N) is offered: the page has a physical size.
    bool drawing_scale_available() const;
    /// Validates the form and stores the scale of the selected floor.
    Result apply_scale();
    /// "Not to scale" (allowed, marked).
    Result clear_scale();
    /// The other floors of the selected floor's sheet (same source and page).
    std::vector<ShareChoice> share_choices() const;
    /// share: the floor follows the selected floor's scale; !share: it
    /// keeps the value as its own scale.
    Result share_scale(const std::string& floor_id, bool share);
    Result set_reference(Point2 page_point);
    Result clear_reference();

    /// The project changed elsewhere: drop a selection whose floor is gone.
    void refresh();

private:
    PlanImportWindow(project::Project& project, std::unique_ptr<plan::PlanImport> import,
                     std::optional<RestrictedView> restricted);
    void load_scale_form();
    void trim_changed() noexcept { ++trim_gen_; }
    Result fail(std::string message) const { return Result{false, std::move(message)}; }
    std::optional<std::size_t> handle_at(Point2 p) const;
    bool has_handles() const;
    std::vector<building::Floor> floors_here() const;

    project::Project& project_;
    std::unique_ptr<plan::PlanImport> import_;
    std::optional<RestrictedView> restricted_;
    ImportTool tool_ = ImportTool::Pan;
    double pick_ = 10.0;
    unsigned trim_gen_ = 0;
    // Pointer state.
    bool down_ = false;
    std::optional<Point2> start_;
    std::optional<Point2> current_;
    std::optional<std::size_t> drag_handle_;
    std::optional<Point2> rect_anchor_;
    std::vector<Point2> polygon_;
    std::vector<Point2> drag_outline_;
    // Forms and selection.
    std::optional<ImportFloorForm> add_form_;
    std::vector<std::string> added_;
    std::string selected_;
    std::optional<std::string> pending_remove_;
    ScaleForm scale_;
};

}  // namespace yantra::app
