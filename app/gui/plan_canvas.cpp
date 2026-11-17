// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plan view of the project window, see plan_canvas.h.
// Partly copied from yantra-v2 68ea20a: app/src/plan_canvas.cpp, adapted.
#include "plan_canvas.h"
#include "gui_widgets.h"
#include "yantra/app/i18n.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace yantra::app {

namespace {

constexpr std::size_t kTileBudget = 192U << 20U;  // bytes of cached tile pixels

ImU32 rgb_colour(std::uint32_t rgb, int alpha = 255) {
    return IM_COL32((rgb >> 16U) & 0xffU, (rgb >> 8U) & 0xffU, rgb & 0xffU, alpha);
}

/// A small filled label box ("badge") at `pos`; returns its size.
ImVec2 badge(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 bg, ImU32 fg) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad(6.0F, 3.0F);
    const ImVec2 end(pos.x + ts.x + 2 * pad.x, pos.y + ts.y + 2 * pad.y);
    dl->AddRectFilled(pos, end, bg, 3.0F);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), fg, text);
    return ImVec2(end.x - pos.x, end.y - pos.y);
}

bool mouse_in(ImVec2 a, ImVec2 b) {
    const ImVec2 m = ImGui::GetIO().MousePos;
    return m.x >= a.x && m.y >= a.y && m.x < b.x && m.y < b.y;
}

}  // namespace

ImVec2 PlanCanvas::to_screen(ImVec2 origin, Point2 plan) const {
    const Point2 s = view_.page_to_screen(plan);
    return ImVec2(origin.x + static_cast<float>(s.x), origin.y + static_cast<float>(s.y));
}

void PlanCanvas::close() {
    textures_.clear();
    used_.clear();
    interpolation_.reset();
    interpolation_generation_ = 0;
    interpolation_id_ = 0;
    tiles_ = PlanTiles{};
    floor_id_.clear();
    fitted_ = false;
}

void PlanCanvas::open(ProjectWindow& window, const FloorContent& content) {
    close();
    floor_id_ = content.floor_id;
    plan_w_ = content.width;
    plan_h_ = content.height;
    plan_has_picture_ = content.has_plan;
    if (content.has_plan) tiles_ = window.plan_tiles(content.floor_id, kTileBudget);
}

void PlanCanvas::zoom(double factor) {
    view_.zoom_at(factor, Point2{view_.viewport_width() * 0.5, view_.viewport_height() * 0.5});
}

void PlanCanvas::draw_tiles(ImDrawList* dl, ImVec2 origin) {
    if (!tiles_.renderer) return;
    plan::TileRenderer& r = *tiles_.renderer;
    const plan::TileLayout& layout = r.layout();
    const auto [lo, hi] = view_.visible_page_bounds();
    const int lod = layout.lod_for(view_.zoom());
    // The coarsest level (the whole floor in one tile) always lies below,
    // so zooming and panning never show holes while finer tiles render.
    std::vector<plan::TileKey> keys =
        layout.tiles_for(0, layout.max_lod(), 0.0, 0.0, layout.extent().width, layout.extent().height);
    if (lod != layout.max_lod()) {
        const auto fine = layout.tiles_for(0, lod, lo.x, lo.y, hi.x, hi.y);
        keys.insert(keys.end(), fine.begin(), fine.end());
    }
    r.request(keys);
    if (wait_for_tiles_) r.wait_idle();

    // Textures drawn in the previous frame only: the others are retired
    // before anything of this frame uses them.
    std::unordered_set<plan::TileKey, plan::TileKeyHash> wanted(keys.begin(), keys.end());
    for (auto it = textures_.begin(); it != textures_.end();)
        it = wanted.count(it->first) != 0 ? std::next(it) : textures_.erase(it);
    for (const plan::TileKey& k : keys) {
        auto tex = textures_.find(k);
        if (tex == textures_.end()) {
            const std::shared_ptr<const plan::Raster> raster = r.find(k);
            if (!raster || raster->width <= 0 || raster->height <= 0) continue;
            UserTexture t;
            t.set(raster->width, raster->height, raster->rgba.data());
            if (!t.valid()) continue;
            tex = textures_.emplace(k, std::move(t)).first;
        }
        const plan::PixelRect px = layout.tile_rect(k);
        const double s = layout.scale(k.lod);
        const Point2 a{static_cast<double>(px.x) / s, static_cast<double>(px.y) / s};
        const Point2 b{static_cast<double>(px.x + px.width) / s, static_cast<double>(px.y + px.height) / s};
        dl->AddImage(tex->second.ref(), to_screen(origin, a), to_screen(origin, b));
        ++stats_.tiles;
    }
}

void PlanCanvas::draw_interpolation(ImDrawList* dl, ImVec2 origin, const FloorContent& c, double opacity) {
    if (!c.interpolation || c.interpolation->cols <= 0 || c.interpolation->rows <= 0) return;
    const InterpolationLayer& in = *c.interpolation;
    // IND-43: cells coloured with the points' legend, alpha = confidence.
    if (interpolation_generation_ != c.generation || interpolation_id_ != in.id || !interpolation_.valid()) {
        interpolation_.set(in.cols, in.rows, in.rgba.data());
        interpolation_generation_ = c.generation;
        interpolation_id_ = in.id;
    }
    if (!interpolation_.valid()) return;
    dl->AddImage(interpolation_.ref(), to_screen(origin, in.min), to_screen(origin, in.max), ImVec2(0, 0),
                 ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(std::lround(std::clamp(opacity, 0.0, 1.0) * 255.0))));
    stats_.interpolation = true;
}

void PlanCanvas::draw_binned(ImDrawList* dl, ImVec2 origin, const FloorContent& c) {
    if (!c.binned) return;
    for (const PlanBin& b : c.binned->bins) {
        const ImVec2 a = to_screen(origin, b.min);
        const ImVec2 e = to_screen(origin, b.max);
        dl->AddRectFilled(a, e, b.has_value ? rgb_colour(b.rgb, 170) : IM_COL32(158, 158, 158, 110));
        dl->AddRect(a, e, IM_COL32(40, 40, 40, 160));
        ++stats_.bins;
    }
}

void PlanCanvas::draw_points(ImDrawList* dl, ImVec2 origin, ImVec2 size, const FloorContent& c) {
    const std::size_t n = c.points.count();
    if (n == 0) return;
    // IND-30: every point is drawn; dense points are drawn smaller, never
    // left out. The radius follows the screen area per point.
    const double per_point = static_cast<double>(size.x) * static_cast<double>(size.y) / static_cast<double>(n);
    const float radius = std::clamp(static_cast<float>(0.3 * std::sqrt(per_point)), 1.5F, 6.0F);
    stats_.point_radius_px = radius;
    for (std::size_t i = 0; i < n; ++i) {
        const survey::SurveyPoint& p = c.points.points[i];
        const ImVec2 s = to_screen(origin, p.plan);
        dl->AddCircleFilled(s, radius, rgb_colour(p.rgb));
        if (radius >= 3.0F) dl->AddCircle(s, radius, IM_COL32(30, 30, 30, 200), 0, 1.0F);
        ++stats_.points;
    }
    // PP-02: the serving-cell changes, a square around the point.
    for (const survey::ServingChange& ch : c.serving_changes) {
        if (ch.index >= n) continue;
        const ImVec2 s = to_screen(origin, c.points.points[ch.index].plan);
        const float r = radius + 3.0F;
        dl->AddRect(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), IM_COL32(255, 235, 59, 255), 0.0F, 2.0F);
        ++stats_.serving_changes;
    }
    if (hover_.point && *hover_.point < n) {
        const ImVec2 s = to_screen(origin, c.points.points[*hover_.point].plan);
        dl->AddCircle(s, radius + 3.0F, IM_COL32(255, 255, 255, 255), 0, 2.0F);
    }
}

void PlanCanvas::draw_overlay(ImDrawList* dl, ImVec2 origin, ImVec2 size, const FloorContent& c,
                              const LayerSettings& layers) {
    // Badges, top left: interpolated / stale (IND-43, IND-45), the binned
    // view's label (PP-05), "not to scale" (IND-08).
    ImVec2 at(origin.x + 8.0F, origin.y + 8.0F);
    auto next = [&](ImVec2 s) { at.y += s.y + 4.0F; };
    if (layers.visible(Layer::Interpolation) && c.interpolation) {
        const std::string t = tr("Map: ") + c.interpolation->label;
        next(badge(dl, at, t.c_str(), IM_COL32(30, 30, 30, 200), IM_COL32(255, 255, 255, 255)));
        if (c.interpolation->stale) {
            const ImVec2 s = badge(dl, at, tr("STALE: inputs changed since it was computed"), IM_COL32(190, 60, 30, 230),
                                   IM_COL32(255, 255, 255, 255));
            stats_.stale_badge = true;
            if (!gui_detail::help_shown_on_purpose() && mouse_in(at, ImVec2(at.x + s.x, at.y + s.y))) {
                std::string why;
                for (const std::string& r : c.interpolation->stale_reasons) why += (why.empty() ? "" : "\n") + r;
                ImGui::SetTooltip(tr("The map is shown as it was computed. Changed since:\n%s"), why.c_str());
            }
            next(s);
        }
    }
    // PP-02/PP-04: a classified or simulated view says so.
    if (layers.visible(Layer::Points) && !c.view_label.empty()) {
        next(badge(dl, at, c.view_label.c_str(), IM_COL32(120, 60, 0, 220), IM_COL32(255, 255, 255, 255)));
        stats_.view_badge = true;
    }
    if (layers.visible(Layer::Binned) && c.binned)
        next(badge(dl, at, c.binned->label.c_str(), IM_COL32(30, 30, 30, 200), IM_COL32(255, 230, 150, 255)));

    // Scale bar, bottom left (IND-08).
    stats_.scale = scale_bar(c.metres_per_unit, view_.zoom());
    const float th = ImGui::GetTextLineHeight();
    const ImVec2 base(origin.x + 12.0F, origin.y + size.y - 16.0F);
    if (stats_.scale.pixels > 0.0) {
        const float px = static_cast<float>(stats_.scale.pixels);
        const float tw = ImGui::CalcTextSize(stats_.scale.label.c_str()).x;
        dl->AddRectFilled(ImVec2(base.x - 6.0F, base.y - th - 9.0F), ImVec2(base.x + std::max(px, tw) + 6.0F, base.y + 8.0F),
                          IM_COL32(255, 255, 255, 200), 3.0F);
        dl->AddLine(base, ImVec2(base.x + px, base.y), IM_COL32(0, 0, 0, 255), 3.0F);
        dl->AddLine(ImVec2(base.x, base.y - 6.0F), ImVec2(base.x, base.y + 4.0F), IM_COL32(0, 0, 0, 255), 2.0F);
        dl->AddLine(ImVec2(base.x + px, base.y - 6.0F), ImVec2(base.x + px, base.y + 4.0F), IM_COL32(0, 0, 0, 255),
                    2.0F);
        dl->AddText(ImVec2(base.x, base.y - th - 7.0F), IM_COL32(0, 0, 0, 255), stats_.scale.label.c_str());
    } else {
        badge(dl, ImVec2(base.x - 4.0F, base.y - th - 9.0F), tr("Not to scale: set a scale for distances and areas"),
              IM_COL32(120, 90, 20, 230), IM_COL32(255, 255, 255, 255));
    }

    // Legend of the point colours, top right (the interpolation and the
    // bins use the same one, IND-43).
    const bool coloured = (layers.visible(Layer::Points) && c.points.count() > 0) ||
                          (layers.visible(Layer::Interpolation) && c.interpolation) ||
                          (layers.visible(Layer::Binned) && c.binned);
    if (!coloured || c.legend.bands.empty()) return;
    const float row = ImGui::GetTextLineHeight() + 2.0F;
    float width = 0.0F;
    for (const survey::ColourBand& b : c.legend.bands) width = std::max(width, ImGui::CalcTextSize(b.label.c_str()).x);
    const std::string unit = c.legend.unit.empty() ? std::string("value") : c.legend.unit;
    width = std::max(width + 22.0F, ImGui::CalcTextSize(unit.c_str()).x) + 12.0F;
    const ImVec2 l0(origin.x + size.x - width - 8.0F, origin.y + 8.0F);
    const float h = row * static_cast<float>(c.legend.bands.size() + 1) + 8.0F;
    dl->AddRectFilled(l0, ImVec2(l0.x + width, l0.y + h), IM_COL32(255, 255, 255, 210), 3.0F);
    dl->AddText(ImVec2(l0.x + 6.0F, l0.y + 4.0F), IM_COL32(0, 0, 0, 255), unit.c_str());
    float y = l0.y + 4.0F + row;
    for (auto it = c.legend.bands.rbegin(); it != c.legend.bands.rend(); ++it) {
        dl->AddRectFilled(ImVec2(l0.x + 6.0F, y + 2.0F), ImVec2(l0.x + 18.0F, y + row - 2.0F), rgb_colour(it->rgb));
        const std::string label = it->label.empty() ? std::to_string(static_cast<int>(it->from)) : it->label;
        dl->AddText(ImVec2(l0.x + 24.0F, y), IM_COL32(0, 0, 0, 255), label.c_str());
        y += row;
    }
}

void PlanCanvas::draw(ProjectWindow& window, const FloorContent& c, const LayerSettings& layers, ImVec2 avail) {
    if (c.floor_id != floor_id_ || c.width != plan_w_ || c.height != plan_h_ || c.has_plan != plan_has_picture_)
        open(window, c);
    stats_ = CanvasStats{};
    ImGuiIO& io = ImGui::GetIO();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(avail.x, 64.0F), std::max(avail.y, 64.0F));
    view_.set_viewport(size.x, size.y);
    const double w = c.width > 0.0 ? c.width : 1.0;
    const double h = c.height > 0.0 ? c.height : 1.0;
    if (!fitted_) {
        view_.fit(plan::PageSize{w, h}, 0, 0.0);
        fitted_ = true;
    }

    ImGui::InvisibleButton("###plan.canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    origin_ = origin;
    input_ = CanvasInput{};
    if (ImGui::IsItemActive() && ((left_pans_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)) ||
                                  ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0F) ||
                                  ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0F)))
        view_.pan_by(Point2{io.MouseDelta.x, io.MouseDelta.y});
    const Point2 mouse{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
    if (hovered && io.MouseWheel != 0.0F) view_.zoom_at(std::pow(1.2, static_cast<double>(io.MouseWheel)), mouse);
    if (hovered && left_pans_ && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) fit();
    if (view_.zoom() > 0.0) {
        input_.hovered = hovered;
        input_.tolerance = 8.0 / view_.zoom();
        if (hovered || ImGui::IsItemActive()) input_.mouse = view_.screen_to_page(mouse);
        const bool dbl = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        input_.double_clicked = dbl;
        input_.pressed = hovered && !dbl && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        input_.dragging = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        input_.released = ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    }

    // Hover readout (IND-43; a point's short readout, PP-01 in 9.8).
    hover_ = HoverReadout{};
    if (hovered && view_.zoom() > 0.0) {
        const Point2 at = view_.screen_to_page(mouse);
        hover_ = window.hover(at, 8.0 / view_.zoom(), layers);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, end, true);
    dl->AddRectFilled(origin, end, IM_COL32(48, 50, 54, 255));
    // The floor area: paper under the plan (outside a polygon trim the
    // tiles are transparent).
    const ImVec2 f0 = to_screen(origin, Point2{0.0, 0.0});
    const ImVec2 f1 = to_screen(origin, Point2{w, h});
    dl->AddRectFilled(f0, f1, c.has_plan ? IM_COL32(255, 255, 255, 255) : IM_COL32(236, 236, 230, 255));
    for (Layer l : layers.order) {
        if (!layers.visible(l)) continue;
        const std::size_t before = stats_.tiles + stats_.points + stats_.bins +
                                   (stats_.interpolation ? 1U : 0U);
        switch (l) {
        case Layer::Plan: draw_tiles(dl, origin); break;
        case Layer::Interpolation: draw_interpolation(dl, origin, c, layers.map_opacity); break;
        case Layer::Binned: draw_binned(dl, origin, c); break;
        case Layer::Points: draw_points(dl, origin, size, c); break;
        }
        const std::size_t after = stats_.tiles + stats_.points + stats_.bins +
                                  (stats_.interpolation ? 1U : 0U);
        if (after > before) stats_.drawn.push_back(l);
    }
    dl->AddRect(f0, f1, IM_COL32(120, 120, 120, 255));
    if (overlay_) overlay_(dl);
    draw_overlay(dl, origin, size, c, layers);
    dl->PopClipRect();

    if (hover_.any && !gui_detail::help_shown_on_purpose()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(hover_.text.c_str());
        ImGui::EndTooltip();
    }
}

}  // namespace yantra::app
