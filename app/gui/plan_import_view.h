// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing of the plan import window (IND-02, IND-03, IND-05..IND-10): the
// page list with thumbnails, the trim tools, rotation and deskew, the
// live preview of the trimmed floor, Add as floor, the floors already
// trimmed, scale and reference point in a side panel; the source page as
// tiles (zoom-dependent, turned by the trim's rotation) with the trims
// outlined and labelled; the credit and licence of a licence-restricted
// plan behind Info (UI-20). Everything it shows and does goes through PlanImportWindow
// (app/model). No OpenGL here (user_texture.h).
//
// Ids (docs/APP.md): the dialog "dlg.plan_import"; panel items "pi.*"
// (pi.page.<n>, pi.tool.<tool>, pi.rot_left, pi.rot_right, pi.reset,
// pi.deskew, pi.add, pi.add_name, pi.add_level, pi.add_height, pi.add_ok,
// pi.add_cancel, pi.apply_edit, pi.cancel_edit, pi.finish, pi.undo,
// pi.cancel_poly, per floor pi.floor.<id>, pi.edit.<id>, pi.remove.<id>,
// pi.confirm_remove, pi.keep; scale and alignment pi.tool.scale_line,
// pi.tool.scale_bar, pi.tool.reference, pi.distance, pi.scale_apply,
// pi.ratio, pi.ratio_apply, pi.scale_clear, pi.share.<id>, pi.ref_clear);
// the canvas "pi.canvas"; Close "dlg.cancel".
// Copied from yantra-v2 68ea20a: app/src/plan_import_view.{h,cpp}, adapted
// (headless textures, live preview, drawing scale, restricted credits,
// test stats).
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "user_texture.h"
#include "yantra/app/app_state.h"
#include "yantra/core/plan/tiles.h"

namespace yantra::app {

/// What the last frame drew (tests).
struct ImportViewStats {
    std::size_t tiles = 0;       ///< page tiles drawn
    std::size_t thumbnails = 0;  ///< page thumbnails drawn
    std::size_t placed = 0;      ///< outlines of floors already added, on this page
    bool preview = false;        ///< the live preview of the trim was drawn
    int preview_width = 0, preview_height = 0;
    bool credit = false;         ///< the restricted source's credit was shown
    ImVec2 canvas_origin{0.0F, 0.0F};  ///< screen position of the canvas (page_to_screen + this)
    ImVec2 canvas_size{0.0F, 0.0F};
};

class PlanImportView {
public:
    /// Screenshots and tests: wait for every requested tile before drawing.
    void set_wait_for_tiles(bool on) { wait_for_tiles_ = on; }
    /// Draws the window's content (inside the dialog); returns false when
    /// the user closes it.
    bool draw(AppState& state, PlanImportWindow& w);
    /// Drops every texture (the window closed).
    void close();
    const ImportViewStats& stats() const { return stats_; }
    /// The message of the last command ("" none).
    const std::string& message() const { return message_; }
    bool message_is_error() const { return message_error_; }

private:
    void reset(PlanImportWindow& w);
    void draw_panel(PlanImportWindow& w);
    void draw_pages(PlanImportWindow& w);
    void draw_preview(PlanImportWindow& w);
    void draw_scale(PlanImportWindow& w);
    void draw_add_popup(PlanImportWindow& w);
    void draw_canvas(PlanImportWindow& w);
    void draw_tiles(PlanImportWindow& w, ImDrawList* dl, ImVec2 origin);
    void report(const Result& r);

    PlanImportWindow* shown_ = nullptr;
    int page_ = -1;
    std::unique_ptr<plan::TileRenderer> tiles_;
    std::unordered_map<plan::TileKey, UserTexture, plan::TileKeyHash> textures_;
    std::map<int, UserTexture> thumbnails_;
    std::map<int, bool> thumbnail_failed_;
    UserTexture preview_;
    unsigned preview_gen_ = ~0U;
    std::string preview_error_;
    bool fitted_ = false;
    bool capturing_ = false;  // the pointer press went to the model (not a pan)
    bool wait_for_tiles_ = false;
    std::string message_;
    bool message_error_ = false;
    bool info_open_ = false;   // UI-20: the file's details behind Info
    std::string add_error_;    // the Add as floor popup's refusal
    ImportViewStats stats_;
};

}  // namespace yantra::app
