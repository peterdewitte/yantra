// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project window's model (IND-04, IND-08, IND-10, IND-30, IND-43,
// PP-05), without drawing: floor tabs ordered by level, the floor commands
// (rename, level and height, reorder, remove with confirmation) and what
// each layer of the selected floor shows. The ImGui layer (app/gui) draws
// it; tests use it directly (TST-50). The model calls the core only.
//
// - Measured points and their counts come only from survey::point_list()
//   and survey::point_counts() (IND-30, docs/SURVEY.md), so the window and
//   the report show the same points: never thinned, dense markers are
//   drawn smaller.
// - The binned layer is survey::bin_points() of that list, an extra view
//   that always shows its label (PP-05, IND-30).
// - The interpolation layer is a stored result of the floor (the newest
//   by default), coloured by survey::cell_colours() with the points'
//   legend (alpha = confidence) and always labelled "interpolated"
//   (IND-43). A stale result is shown, marked stale with its reasons.
// - Positions are plan units of the floor (the trimmed plan, IND-07);
//   squares and grid cells in plan-frame metres are mapped through the
//   floor's scale (building::Building::from_plan_metres).
//
// Threading: the UI thread. Project changes from elsewhere (a background
// job's listener) only mark the cache dirty; it is rebuilt on the next
// call.
// Partly copied from yantra-v2 68ea20a: app/model/include/yantra/app/project_window.h,
// adapted (string floor ids, plan and binned layers, layer order, scale bar).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/app/result.h"
#include "yantra/app/settings.h"
#include "yantra/core/building/model.h"
#include "yantra/core/plan/source.h"
#include "yantra/core/plan/tiles.h"
#include "yantra/core/survey/bins.h"
#include "yantra/core/survey/interpolation.h"
#include "yantra/core/survey/points.h"

namespace yantra::project {
class Project;
}

namespace yantra::app {

using building::Point2;

/// One floor tab, in level order, lowest first (IND-04).
struct FloorTab {
    std::string id;
    std::string name;
    int level = 0;
    double height_m = 3.0;
    bool to_scale = false;
    bool has_plan = false;   ///< a plan source with a trim (else a blank floor)
    std::size_t points = 0;  ///< survey::point_counts() (IND-30)
    std::string label;       ///< tab text: "Ground floor (0)"
    std::string tooltip;     ///< level, height, scale, plan, points
};

/// A layer's row in the layer list.
struct LayerRow {
    Layer layer = Layer::Plan;
    std::string id;       ///< "layer.<layer_key>"
    std::string label;    ///< "Walls (34)"
    std::string tooltip;  ///< what it shows (DOC-07 b)
    bool visible = true;
    bool available = true;  ///< the floor has something for this layer
    std::string note;       ///< why it is empty, or e.g. "Stale: measured points changed. ..."
    bool can_move_up = false;
    bool can_move_down = false;
};

struct PlanWall {
    std::vector<Point2> points;  ///< plan units
    std::uint32_t rgb = 0;       ///< by attenuation: heavier walls darker
    std::string label;           ///< class and attenuation
};

struct PlanAntenna {
    Point2 position;  ///< plan units
    std::string label;
    bool outdoor = false;                 ///< donor/macro direction marker
    std::optional<double> direction_deg;  ///< outdoor donor: bearing (building/model.h convention)
};

/// One square of the binned layer, in plan units.
struct PlanBin {
    Point2 min, max;
    std::uint32_t rgb = 0;
    bool has_value = false;
};

/// The binned layer (PP-05): an extra view, labelled.
struct BinnedLayer {
    std::string label;  ///< BinnedView::label, always shown with the layer
    std::vector<PlanBin> bins;
    std::size_t left_out = 0;  ///< points without metres
    survey::BinnedView view;
};

/// The interpolation layer of a floor (IND-43).
struct InterpolationLayer {
    std::int64_t id = 0;  ///< the stored result
    std::string label;    ///< "interpolated" (+ ", estimated sources")
    std::string key;      ///< what it is for (metric and source group)
    Point2 min, max;      ///< grid rectangle in plan units
    int cols = 0;
    int rows = 0;
    /// Row-major RGBA bytes (cols * rows * 4), alpha = confidence; row 0
    /// is the grid's first row (lowest y, the top of the plan).
    std::vector<std::uint8_t> rgba;
    bool stale = false;
    std::vector<std::string> stale_reasons;
    double rms_db = 0.0;
    std::size_t fit_points = 0;
    survey::Interpolation data;  ///< for the hover readout
};

/// What the selected floor shows.
struct FloorContent {
    std::string floor_id;
    std::string name;
    /// Changes whenever the content is rebuilt (the UI re-uploads textures).
    std::uint64_t generation = 0;
    double width = 0.0;  ///< plan units (trimmed plan size; else the drawing's extent)
    double height = 0.0;
    std::optional<double> metres_per_unit;  ///< nullopt: "not to scale" (IND-08)
    bool has_plan = false;
    std::string plan_note;  ///< why there is no plan picture, when there is none
    std::vector<PlanWall> walls;
    std::vector<PlanAntenna> antennas;
    survey::PointList points;  ///< survey::point_list() (IND-30)
    std::optional<BinnedLayer> binned;
    std::string binned_note;  ///< why there is no binned layer
    std::optional<InterpolationLayer> interpolation;
    std::string interpolation_note;  ///< why there is none, or that it cannot be read
    /// Every stored interpolation of the floor (id, key), newest first.
    std::vector<std::pair<std::int64_t, std::string>> interpolations;
    survey::ColourScale legend;  ///< the points' scale, also used by the interpolation and the bins
};

/// The plan picture of a floor: its source and background tiles of the
/// trimmed floor (IND-02 tiled rendering, IND-07 trim only).
struct PlanTiles {
    std::shared_ptr<const plan::PlanSource> source;
    std::unique_ptr<plan::TileRenderer> renderer;  ///< nullptr on error or without plan
    std::string error;                              ///< why there is no picture
};

/// The question asked before a floor is removed (IND-04).
struct RemoveQuestion {
    std::string floor_id;
    std::string title;     ///< "Remove floor?"
    std::string question;  ///< names the floor and what goes with it
};

/// Hover readout over the plan (IND-43, PP-01 in short).
struct HoverReadout {
    bool any = false;
    std::optional<std::size_t> point;  ///< index in FloorContent::points
    std::string text;                  ///< several lines
};

/// A scale bar for the plan view (IND-08): a round length (1, 2 or 5 x
/// 10^n m) about `target_px` screen pixels long.
struct ScaleBar {
    double length_m = 0.0;
    double pixels = 0.0;
    std::string label;  ///< "5 m", "500 mm"... ; "not to scale" without a scale
};
ScaleBar scale_bar(std::optional<double> metres_per_unit, double screen_px_per_unit, double target_px = 120.0);

class ProjectWindow {
public:
    explicit ProjectWindow(project::Project& project);
    ~ProjectWindow();
    ProjectWindow(const ProjectWindow&) = delete;
    ProjectWindow& operator=(const ProjectWindow&) = delete;

    /// The floor tabs, ordered by level.
    const std::vector<FloorTab>& tabs();
    /// The selected floor id ("" when the project has no floor).
    const std::string& selected();
    bool select(const std::string& floor_id);
    /// The selected floor's layers (nullptr without floors). The binned
    /// layer uses `bin_size_m`.
    const FloorContent* content(double bin_size_m = 2.0);
    /// Shows another stored interpolation of the selected floor (IND-43).
    bool show_interpolation(std::int64_t id);
    /// The layer list for the selected floor, top layer first (the
    /// reverse of the drawing order), with counts and notes.
    std::vector<LayerRow> layer_rows(const LayerSettings& layers);

    /// Opens the floor's plan source and makes its tile renderer (tiles
    /// in plan units at plan::default_base_scale, `budget_bytes` cache).
    PlanTiles plan_tiles(const std::string& floor_id, std::size_t budget_bytes,
                         plan::TileRenderer::ReadyFn ready = {}) const;

    /// Hover readout at a plan point of the selected floor: the measured
    /// point within `radius` plan units (points layer shown), else the
    /// interpolation's readout (layer shown, floor to scale).
    HoverReadout hover(Point2 plan, double radius, const LayerSettings& layers);

    // -- floor commands (IND-04) ----------------------------------------
    Result rename_floor(const std::string& id, const std::string& name);
    /// Level must be free; height 0 < h <= 50 m.
    Result set_level(const std::string& id, int level, double height_m);
    /// Moves a floor one place down (-1) or up (+1) in the level order;
    /// the floors keep their set of levels (Project::move_floor).
    Result move_floor(const std::string& id, int steps);
    /// Asks before removing: the question is then pending.
    Result request_remove(const std::string& id);
    const std::optional<RemoveQuestion>& pending_remove() const noexcept { return pending_remove_; }
    /// Removes the floor of the pending question with its walk tests and
    /// points (one transaction) and selects a neighbour.
    Result confirm_remove();
    void cancel_remove();

    /// Marks the cache dirty (also done by project change notifications).
    void invalidate() noexcept;

private:
    void refresh();
    FloorContent build_content(const std::string& floor_id, double bin_size_m) const;
    void build_binned(FloorContent& c, const building::Building& b, const building::Floor& f, double size_m) const;
    void build_interpolation(FloorContent& c, const building::Building& b, const building::Floor& f) const;

    project::Project& project_;
    int token_ = 0;
    std::shared_ptr<std::atomic<bool>> dirty_;
    std::vector<FloorTab> tabs_;
    std::string selected_;
    std::optional<FloorContent> content_;
    double content_bin_size_ = 0.0;
    std::optional<std::int64_t> chosen_interpolation_;
    std::uint64_t generation_ = 0;
    std::optional<RemoveQuestion> pending_remove_;
};

}  // namespace yantra::app
