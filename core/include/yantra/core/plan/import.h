// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plan import window's model (IND-05, IND-06, IND-07, IND-10),
// without UI: the window itself is drawn in phase 9 from this state.
//
// - Add floor (IND-10): PlanImport::add_file copies a new plan file into
//   the project (IND-03) and opens it; PlanImport::open uses a source the
//   project already has. A file that cannot be read is refused with the
//   reason and not kept in the project.
// - The window opens on the whole first page, fitted into the view, with
//   zoom and pan (PlanView); a PDF lists its pages with thumbnails.
// - Trimming (IND-05): a rectangle (drawn in the turned view) or a
//   polygon, quarter turns and a fine deskew (an angle or two points on a
//   line). The current trim starts as the whole page.
// - "Add as floor" (IND-06) stores the current trim as a new floor (name,
//   level) and leaves the window open for the next floor of the same
//   sheet. The trims already added are listed (outline + label on the
//   source) and can be edited (IND-07: the floor's trim is loaded,
//   changed, applied) or removed.
//
// Threading: one thread (the UI). The page tiles (page_tiles) render on
// their own worker.
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plan/import.h, adapted (string ids, building::Floor, plan size, source pages).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/plan/source.h"
#include "yantra/core/plan/tiles.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/project/project.h"

namespace yantra::plan {

/// Zoom and pan of the import window over the turned page:
/// screen = zoom * turned(page) + pan, where turned() is the rotation of
/// the current trim (quarter turns + deskew, clockwise) about the page
/// origin. Screen units are the window's pixels.
class PlanView {
public:
    void set_viewport(double width, double height);
    double viewport_width() const noexcept { return vw_; }
    double viewport_height() const noexcept { return vh_; }
    /// Shows the whole turned page centred in the viewport (IND-05).
    void fit(const PageSize& page, int quarter_turns, double deskew_deg);
    /// Changes the rotation, keeping the page point under the viewport
    /// centre where it is.
    void set_rotation(int quarter_turns, double deskew_deg);
    double zoom() const noexcept { return zoom_; }
    Point2 pan() const noexcept { return pan_; }
    /// Zooms by `factor` (clamped to 1e-4..1e4 screen px per unit) keeping
    /// the page point under `anchor` (screen) in place.
    void zoom_at(double factor, Point2 anchor);
    void pan_by(Point2 delta);
    Point2 page_to_screen(Point2 page) const noexcept;
    Point2 screen_to_page(Point2 screen) const noexcept;
    /// Page-unit bounding box of the visible viewport (for tile requests).
    std::pair<Point2, Point2> visible_page_bounds() const noexcept;

private:
    Point2 turned(Point2 p) const noexcept;
    Point2 unturned(Point2 p) const noexcept;
    double vw_ = 800.0, vh_ = 600.0;
    double zoom_ = 1.0;
    Point2 pan_{0.0, 0.0};
    int quarters_ = 0;
    double deskew_ = 0.0;
};

/// A trim already added as a floor, for outlining on the source (IND-06).
struct PlacedTrim {
    std::string floor_id;
    std::string name;
    int level = 0;
    int page = 0;
    Trim trim;
    Point2 label;  ///< where the label goes: the outline's centroid, page units
};

/// Pixels per page unit at level 0 of the page tiles: 1:1 for images,
/// 300 dpi for PDF pages (less when the page would exceed 2^30 px).
double default_base_scale(const PlanSource& source, int page);

class PlanImport {
public:
    /// IND-10 with a new file: copied into the project (or found there by
    /// its hash) and opened. PlanError with the reason when it cannot be
    /// read; a file added by this call is then removed again.
    static std::unique_ptr<PlanImport> add_file(project::Project& project, const std::filesystem::path& file,
                                                const OpenLimits& limits = {});
    /// IND-10 with a source already in the project.
    static std::unique_ptr<PlanImport> open(project::Project& project, const std::string& source_id,
                                            const OpenLimits& limits = {});

    const std::string& source_id() const noexcept { return source_id_; }
    const PlanSource& source() const noexcept { return *source_; }
    std::shared_ptr<const PlanSource> shared_source() const noexcept { return source_; }
    int page_count() const { return source_->page_count(); }
    int page() const noexcept { return page_; }
    /// Shows another page: the trim becomes the whole page (rotation
    /// kept), the view fits it, editing ends.
    void set_page(int page);
    Raster thumbnail(int page, int max_px) const { return render_thumbnail(*source_, page, max_px); }

    PlanView& view() noexcept { return view_; }
    const PlanView& view() const noexcept { return view_; }

    // -- the current trim (IND-05) -------------------------------------
    const Trim& trim() const noexcept { return trim_; }
    /// A rectangle through two page points, its sides along the current
    /// rotation (as dragged in the turned view).
    void set_rectangle(Point2 a, Point2 b);
    /// A polygon outline in page units; PlanError Invalid when it is not
    /// a valid outline (fewer than 3 points, crossing itself, no area).
    void set_polygon(std::vector<Point2> outline);
    /// Turns by `steps` quarter turns (positive: clockwise).
    void rotate_quarter(int steps);
    /// Fine rotation (-45..45 degrees). A rectangle keeps its centre and
    /// size in the new frame.
    void set_deskew(double deg);
    /// Deskew from two points on a line that should be straight.
    void deskew_from_line(Point2 a, Point2 b);
    /// The whole page again (rotation kept).
    void reset_trim();
    /// Size of the floor the current trim gives (floor units).
    PageSize trim_size() const;
    /// The current trim rendered as a floor, fitted into `max_px`.
    Raster preview(int max_px) const;

    // -- floors from this source (IND-06, IND-07) -------------------------
    /// Adds the current trim as a floor (name, level) and returns its id;
    /// the window stays on the source for the next floor. ProjectError
    /// Invalid for an empty name or a used level.
    std::string add_as_floor(const std::string& name, int level, double height_m = 3.0);
    /// The trims of this source already added as floors, by level.
    std::vector<PlacedTrim> placed() const;
    /// Loads a floor's page and trim for editing (IND-07).
    void edit_floor(const std::string& floor_id);
    /// The floor being edited, if any.
    const std::optional<std::string>& editing() const noexcept { return editing_; }
    /// Stores the current trim (and page) in the floor being edited.
    void apply_to_floor();
    /// Stops editing without storing; the trim stays as it is.
    void stop_editing() noexcept { editing_.reset(); }
    /// Removes a floor of this source (the UI confirms first).
    void remove_floor(const std::string& floor_id);

    /// Background tiles of the current page (TileRenderer over
    /// PlanSource::render, base scale default_base_scale).
    std::unique_ptr<TileRenderer> page_tiles(std::size_t budget_bytes, TileRenderer::ReadyFn ready = {}) const;

private:
    PlanImport(project::Project& project, const std::string& source_id, std::shared_ptr<const PlanSource> source);
    void refit();
    project::Project& project_;
    std::string source_id_;
    std::shared_ptr<const PlanSource> source_;
    int page_ = 0;
    Trim trim_;
    PlanView view_;
    std::optional<std::string> editing_;
};

/// Opens a project's plan source (read through the project, so the hash
/// is checked). ProjectError for a missing/damaged source, PlanError when
/// it cannot be read as a plan.
std::shared_ptr<const PlanSource> open_project_source(const project::Project& project, const std::string& source_id,
                                                      const OpenLimits& limits = {});

/// Background tiles of a trimmed floor (the project window, IND-04):
/// render_floor over the floor's source, page and trim. PlanError Invalid
/// for a floor without a trim.
std::unique_ptr<TileRenderer> floor_tiles(std::shared_ptr<const PlanSource> source, const building::Floor& floor,
                                          double base_scale, std::size_t budget_bytes,
                                          TileRenderer::ReadyFn ready = {});

}  // namespace yantra::plan
