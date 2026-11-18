// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plan view of the selected floor in the project window (IND-04,
// IND-07, IND-08, IND-30, IND-43, PP-05): background tiles of the trimmed
// plan (plan::TileRenderer, zoom-dependent levels) as ImGui user textures,
// zoom and pan (plan::PlanView over plan units), the scale bar, and the
// layers of the project window model in the user's order. Drawing only:
// what is shown comes from ProjectWindow; no OpenGL here (user_texture.h).
//
// Ids: the canvas "plan.canvas", buttons "plan.fit", "plan.zoom_in",
// "plan.zoom_out" (docs/APP.md).
// Partly copied from yantra-v2 68ea20a: app/src/plan_canvas.{h,cpp},
// adapted (headless textures, layer order, binned layer, scale bar,
// badges).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "user_texture.h"
#include "yantra/app/plan_notes.h"
#include "yantra/app/project_window.h"
#include "yantra/core/plan/import.h"

namespace yantra::app {

/// What the last frame drew (tests and the screenshot check).
struct CanvasStats {
    std::size_t tiles = 0;        ///< plan tiles drawn (all levels)
    std::size_t points = 0;       ///< markers drawn: always every point of the list when shown
    float point_radius_px = 0.0F;
    std::size_t bins = 0;
    bool interpolation = false;
    bool stale_badge = false;
    bool view_badge = false;              ///< the post-processing view label (PP-02, PP-04)
    std::size_t serving_changes = 0;      ///< serving-change markers drawn (PP-02)
    std::vector<Layer> drawn;  ///< the layers drawn, bottom first
    ScaleBar scale;
};

/// The mouse over the canvas in the last frame, for the editing tools and
/// the walk test (plan units of the floor).
struct CanvasInput {
    bool hovered = false;
    std::optional<Point2> mouse;  ///< over the canvas (also outside the plan rectangle)
    double tolerance = 0.0;       ///< 8 screen pixels in plan units (hit tests)
    bool pressed = false;         ///< left button went down on the canvas (not a double click)
    bool dragging = false;        ///< left button held since a press on the canvas
    bool released = false;        ///< left button went up after a press on the canvas
    bool double_clicked = false;
    /// IND-48 (plan_notes.h LongPress): a short click, released without
    /// moving before the long-press time (a tool's click acts on this, so
    /// a long press is never also a click); at `press`.
    bool clicked = false;
    /// The button held the long-press time without moving (once per press).
    bool long_pressed = false;
    /// The press moved beyond the slop (once per press).
    bool drag_started = false;
    /// The press is held and has moved: `mouse` is where it is now.
    bool press_dragging = false;
    /// Where the current or last press began.
    std::optional<Point2> press;
    /// The press began on something the grab test took (a note pin): the
    /// left drag does not pan.
    bool grabbed = false;
    bool touch = false;  ///< the press comes from a touch screen
};

class PlanCanvas {
public:
    /// Whether a left drag pans (else the left button goes to a tool;
    /// middle and right drag always pan, a double click does not fit).
    void set_left_pans(bool on) { left_pans_ = on; }
    /// Drawn over the layers, inside the canvas clip (editing and walk overlays).
    void set_overlay(std::function<void(ImDrawList*)> overlay) { overlay_ = std::move(overlay); }
    /// Asked when a left press begins at a plan point (tolerance in plan
    /// units): true when it takes the press (no panning; IND-48 pins).
    void set_grab_test(std::function<bool(Point2, double)> grab) { grab_ = std::move(grab); }
    /// The long-press rules (tests may shorten them).
    void set_long_press(LongPressConfig c) { press_ = LongPress(c); }
    /// The screen position of a plan point in the last frame.
    ImVec2 screen_of(Point2 plan) const { return to_screen(origin_, plan); }
    const CanvasInput& input() const { return input_; }
    /// Screenshots and tests: wait for every requested tile before drawing.
    void set_wait_for_tiles(bool on) { wait_for_tiles_ = on; }
    /// Draws the floor into a region of `size` at the cursor.
    void draw(ProjectWindow& window, const FloorContent& content, const LayerSettings& layers, ImVec2 size);
    /// Fits the floor into the view on the next frame.
    void fit() { fitted_ = false; }
    /// Zooms about the view's centre.
    void zoom(double factor);
    /// Drops the plan tiles and textures (project closed).
    void close();

    const plan::PlanView& view() const { return view_; }
    const CanvasStats& stats() const { return stats_; }
    const HoverReadout& hover() const { return hover_; }
    /// Why there is no plan picture ("" when tiles are shown).
    const std::string& tile_error() const { return tiles_.error; }

private:
    void open(ProjectWindow& window, const FloorContent& content);
    void draw_tiles(ImDrawList* dl, ImVec2 origin);
    void draw_interpolation(ImDrawList* dl, ImVec2 origin, const FloorContent& c, double opacity);
    void draw_binned(ImDrawList* dl, ImVec2 origin, const FloorContent& c);
    void draw_points(ImDrawList* dl, ImVec2 origin, ImVec2 size, const FloorContent& c);
    void draw_overlay(ImDrawList* dl, ImVec2 origin, ImVec2 size, const FloorContent& c, const LayerSettings& layers);
    ImVec2 to_screen(ImVec2 origin, Point2 plan) const;

    std::string floor_id_;
    double plan_w_ = 0.0, plan_h_ = 0.0;
    bool plan_has_picture_ = false;
    PlanTiles tiles_;
    std::unordered_map<plan::TileKey, UserTexture, plan::TileKeyHash> textures_;
    std::unordered_set<plan::TileKey, plan::TileKeyHash> used_;
    plan::PlanView view_;
    bool fitted_ = false;
    bool wait_for_tiles_ = false;
    bool left_pans_ = true;
    std::function<void(ImDrawList*)> overlay_;
    std::function<bool(Point2, double)> grab_;
    LongPress press_;
    std::optional<Point2> press_at_;
    bool grabbed_ = false;
    bool touch_ = false;
    ImVec2 origin_{0.0F, 0.0F};
    CanvasInput input_;
    UserTexture interpolation_;
    std::uint64_t interpolation_generation_ = 0;
    std::int64_t interpolation_id_ = 0;
    CanvasStats stats_;
    HoverReadout hover_;
};

}  // namespace yantra::app
