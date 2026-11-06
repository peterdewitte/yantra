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
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "user_texture.h"
#include "yantra/app/project_window.h"
#include "yantra/core/plan/import.h"

namespace yantra::app {

/// What the last frame drew (tests and the screenshot check).
struct CanvasStats {
    std::size_t tiles = 0;        ///< plan tiles drawn (all levels)
    std::size_t walls = 0;
    std::size_t antennas = 0;
    std::size_t points = 0;       ///< markers drawn: always every point of the list when shown
    float point_radius_px = 0.0F;
    std::size_t bins = 0;
    bool interpolation = false;
    bool stale_badge = false;
    std::vector<Layer> drawn;  ///< the layers drawn, bottom first
    ScaleBar scale;
};

class PlanCanvas {
public:
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
    void draw_interpolation(ImDrawList* dl, ImVec2 origin, const FloorContent& c);
    void draw_binned(ImDrawList* dl, ImVec2 origin, const FloorContent& c);
    void draw_walls(ImDrawList* dl, ImVec2 origin, const FloorContent& c);
    void draw_antennas(ImDrawList* dl, ImVec2 origin, const FloorContent& c);
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
    UserTexture interpolation_;
    std::uint64_t interpolation_generation_ = 0;
    std::int64_t interpolation_id_ = 0;
    CanvasStats stats_;
    HoverReadout hover_;
};

}  // namespace yantra::app
