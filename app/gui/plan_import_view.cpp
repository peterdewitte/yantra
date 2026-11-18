// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing of the plan import window, see plan_import_view.h.
// Copied from yantra-v2 68ea20a: app/src/plan_import_view.cpp, adapted.
#include "plan_import_view.h"
#include "yantra/app/i18n.h"

#include "gui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <vector>

namespace yantra::app {

namespace {

constexpr std::size_t kTileBudget = 256U * 1024U * 1024U;  // decoded page tiles
constexpr int kThumbPx = 112;
constexpr int kPreviewPx = 240;
constexpr float kPanelWidth = 360.0F;
const ImVec4 kErrorColour{1.0F, 0.45F, 0.40F, 1.0F};
const ImVec4 kWarnColour{0.95F, 0.62F, 0.07F, 1.0F};
constexpr ImU32 kTrimColour = IM_COL32(255, 140, 0, 255);       // the current trim
constexpr ImU32 kPlacedColour = IM_COL32(20, 150, 70, 255);     // floors already added
constexpr ImU32 kSelectedColour = IM_COL32(30, 110, 230, 255);  // the selected floor
constexpr ImU32 kScaleColour = IM_COL32(200, 30, 160, 255);

void tooltip(const char* text) {
    // A disabled control's text says why (DOC-07 c).
    const bool shown = gui_detail::note_help((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0, text);
    if (!shown && (gui_detail::help_shown_on_purpose() ||
                   !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)))
        return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
void tooltip(const std::string& text) { tooltip(text.c_str()); }

/// The shared "(?)" marker (DOC-07 a, UI-20).
void help(const char* text) { gui_detail::help_marker(text); }

bool input_text(const char* label, std::string& s, std::size_t capacity = 256) {
    std::string buf = s;
    buf.resize(std::max(capacity, s.size() + 1), '\0');
    const bool changed = ImGui::InputText(label, buf.data(), buf.size());
    if (changed) s = std::string(buf.c_str());
    return changed;
}

ImVec2 screen(const plan::PlanView& v, ImVec2 origin, Point2 page) {
    const Point2 s = v.page_to_screen(page);
    return ImVec2(origin.x + static_cast<float>(s.x), origin.y + static_cast<float>(s.y));
}

void outline(ImDrawList* dl, const plan::PlanView& v, ImVec2 origin, const std::vector<Point2>& pts, ImU32 col,
             float thick, bool closed = true) {
    if (pts.size() < 2) return;
    std::vector<ImVec2> s;
    s.reserve(pts.size());
    for (const Point2& p : pts) s.push_back(screen(v, origin, p));
    dl->AddPolyline(s.data(), static_cast<int>(s.size()), col, thick, closed ? ImDrawFlags_Closed : ImDrawFlags_None);
}

void label(ImDrawList* dl, ImVec2 at, const std::string& text, ImU32 bg) {
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const ImVec2 p(at.x - ts.x * 0.5F, at.y - ts.y * 0.5F);
    dl->AddRectFilled(ImVec2(p.x - 6.0F, p.y - 3.0F), ImVec2(p.x + ts.x + 6.0F, p.y + ts.y + 3.0F), bg, 4.0F);
    dl->AddText(p, IM_COL32(255, 255, 255, 255), text.c_str());
}

void scale_line(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, const std::string& text) {
    dl->AddLine(a, b, col, 3.0F);
    // End ticks across the line.
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::max(std::sqrt(dx * dx + dy * dy), 1.0F);
    const ImVec2 n(-dy / len * 7.0F, dx / len * 7.0F);
    for (const ImVec2 e : {a, b}) dl->AddLine(ImVec2(e.x - n.x, e.y - n.y), ImVec2(e.x + n.x, e.y + n.y), col, 3.0F);
    if (!text.empty()) label(dl, ImVec2((a.x + b.x) * 0.5F + n.x * 3.0F, (a.y + b.y) * 0.5F + n.y * 3.0F), text, col);
}

void cross(ImDrawList* dl, ImVec2 p, ImU32 col) {
    dl->AddCircle(p, 9.0F, col, 0, 2.5F);
    dl->AddLine(ImVec2(p.x - 14.0F, p.y), ImVec2(p.x + 14.0F, p.y), col, 2.0F);
    dl->AddLine(ImVec2(p.x, p.y - 14.0F), ImVec2(p.x, p.y + 14.0F), col, 2.0F);
}

/// A tool as an option with its "(?)" help (UI-20).
bool tool_radio(PlanImportWindow& w, const ToolView& t, Result& r) {
    const bool pressed = gui_detail::option_radio((t.label + "###pi." + t.id).c_str(), w.tool() == t.tool, t.tooltip.c_str(),
                                                  t.enabled ? std::string() : t.disabled_reason);
    if (pressed) r = w.set_tool(t.tool);
    return pressed;
}

}  // namespace

void PlanImportView::report(const Result& r) {
    if (r.message.empty()) return;
    message_ = r.message;
    message_error_ = !r.ok;
}

void PlanImportView::close() {
    shown_ = nullptr;
    page_ = -1;
    tiles_.reset();
    textures_.clear();
    thumbnails_.clear();
    thumbnail_failed_.clear();
    preview_.reset();
    preview_gen_ = ~0U;
    preview_error_.clear();
}

void PlanImportView::reset(PlanImportWindow& w) {
    close();
    shown_ = &w;
    fitted_ = false;
    capturing_ = false;
    message_.clear();
    message_error_ = false;
}

bool PlanImportView::draw(AppState& state, PlanImportWindow& w) {
    if (shown_ != &w) reset(w);
    stats_ = ImportViewStats{};
    if (page_ != w.page()) {
        // A new page: its own tiles, fitted.
        textures_.clear();
        try {
            tiles_ = w.import().page_tiles(kTileBudget);
        } catch (const std::exception& e) {
            tiles_.reset();
            report(Result{false, std::string(tr("Cannot show the page: ")) + e.what()});
        }
        page_ = w.page();
        fitted_ = false;
    }
    w.refresh();
    bool open = true;
    const float bottom = ImGui::GetFrameHeightWithSpacing() * 1.8F;
    ImGui::BeginChild("###pi.panel", ImVec2(std::min(gui_detail::em(kPanelWidth), ImGui::GetContentRegionAvail().x * 0.45F), -bottom), ImGuiChildFlags_None);
    gui_detail::wrap_texts();
    draw_panel(state, w);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("###pi.canvas_area", ImVec2(0.0F, -bottom), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    draw_canvas(w);
    ImGui::EndChild();
    draw_add_popup(w);

    // Bottom line: the last message (or the tool's hint), Close.
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (!message_.empty()) {
        if (message_error_)
            ImGui::TextColored(kErrorColour, "%s", message_.c_str());
        else
            ImGui::TextUnformatted(message_.c_str());
    } else {
        ImGui::TextDisabled("%s", w.hint().c_str());
    }
    {
        // Close at the right end of the line, or on a line of its own.
        const float cw = gui_detail::button_w(tr("Close###dlg.cancel"), 140.0F);
        if (ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x <= right - cw)
            ImGui::SameLine(std::max(right - cw, 0.0F));
        if (ImGui::Button(tr("Close###dlg.cancel"), ImVec2(cw, gui_detail::target_h()))) open = false;
    }
    tooltip(tr("Close the plan import window. The floors added are kept; the project window shows the last one."));
    if (!open) (void)state.show(Result{true, tr("Plan import closed.")});
    return open;
}

void PlanImportView::draw_pages(PlanImportWindow& w) {
    ImGui::SeparatorText(tr("Pages"));
    help(tr("A PDF can hold several sheets: pick the page with the floor(s). Several floors can be trimmed from one "
         "page."));
    const float h = static_cast<float>(kThumbPx) + ImGui::GetTextLineHeightWithSpacing() + 8.0F;
    ImGui::BeginChild("###pi.pages", ImVec2(0.0F, h + ImGui::GetStyle().ScrollbarSize), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const float tp = static_cast<float>(kThumbPx);
    for (int i = 0; i < w.page_count(); ++i) {
        if (i > 0) ImGui::SameLine();
        ImGui::BeginGroup();
        // Thumbnails are rendered when they first come into view.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::IsRectVisible(ImVec2(tp, tp)) && thumbnails_.count(i) == 0 && thumbnail_failed_.count(i) == 0) {
            try {
                const plan::Raster r = w.import().thumbnail(i, kThumbPx);
                UserTexture t;
                t.set(r.width, r.height, r.rgba.data());
                thumbnails_.emplace(i, std::move(t));
            } catch (const std::exception& e) {
                thumbnail_failed_[i] = true;
                report(Result{false, tr("Page ") + std::to_string(i + 1) + ": " + e.what()});
            }
        }
        const std::string id = "###pi.page." + std::to_string(i);
        if (ImGui::InvisibleButton(id.c_str(), ImVec2(tp, tp))) report(w.set_page(i));
        tooltip(tr("Show this page."));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + tp, p.y + tp), IM_COL32(60, 62, 66, 255));
        if (auto t = thumbnails_.find(i); t != thumbnails_.end() && t->second.valid()) {
            const float tw = static_cast<float>(t->second.width()), th = static_cast<float>(t->second.height());
            const ImVec2 o(p.x + (tp - tw) * 0.5F, p.y + (tp - th) * 0.5F);
            dl->AddImage(t->second.ref(), o, ImVec2(o.x + tw, o.y + th));
            ++stats_.thumbnails;
        }
        if (i == w.page()) dl->AddRect(p, ImVec2(p.x + tp, p.y + tp), kTrimColour, 0.0F, 3.0F);
        ImGui::TextUnformatted(w.page_label(i).c_str());
        ImGui::EndGroup();
    }
    ImGui::EndChild();
}

void PlanImportView::draw_preview(PlanImportWindow& w) {
    // IND-05: the current trim as the floor it gives, re-rendered when it
    // changes (the sheet itself is never changed, IND-07).
    if (preview_gen_ != w.trim_generation()) {
        preview_gen_ = w.trim_generation();
        preview_error_.clear();
        try {
            const plan::Raster r = w.import().preview(kPreviewPx);
            preview_.set(r.width, r.height, r.rgba.data());
        } catch (const std::exception& e) {
            preview_.reset();
            preview_error_ = e.what();
        }
    }
    ImGui::SeparatorText(tr("Preview"));
    help(tr("The floor as the current trim gives it: turned, deskewed and cut out. It is rendered from the sheet each "
         "time; no new image is stored."));
    if (!preview_.valid()) {
        ImGui::TextDisabled("%s", preview_error_.empty() ? "No preview." : preview_error_.c_str());
        return;
    }
    const float pw = static_cast<float>(preview_.width()), ph = static_cast<float>(preview_.height());
    const float avail = std::max(ImGui::GetContentRegionAvail().x, 32.0F);
    const float k = std::min(1.0F, avail / pw);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(pw * k, ph * k));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // A checker shows what lies outside the outline (transparent).
    dl->AddRectFilled(p, ImVec2(p.x + pw * k, p.y + ph * k), IM_COL32(90, 92, 96, 255));
    dl->AddImage(preview_.ref(), p, ImVec2(p.x + pw * k, p.y + ph * k));
    stats_.preview = true;
    stats_.preview_width = preview_.width();
    stats_.preview_height = preview_.height();
}

void PlanImportView::draw_scale(PlanImportWindow& w) {
    // UI-20: Scale (IND-08) and Alignment (IND-09), each closed by default;
    // the arrow of its header opens it.
    const std::vector<PlacedView> placed = w.placed();
    const bool have_floor = !w.selected().empty();
    auto floor_line = [&] {
        if (!have_floor) {
            ImGui::TextDisabled(tr("Select or add a floor first."));
            return;
        }
        for (const PlacedView& p : placed)
            if (p.selected) ImGui::Text(tr("Floor: %s"), p.label.c_str());
    };
    auto tools = [&](std::initializer_list<ImportTool> which) {
        Result r;
        for (const ToolView& t : w.tools())
            if (std::find(which.begin(), which.end(), t.tool) != which.end() && tool_radio(w, t, r)) report(r);
    };

    if (gui_detail::section_header(tr("Scale###pi.scale"),
                                   tr("The selected floor's scale: two points with their distance, the scale bar or the "
                                      "drawing scale (IND-08)."))) {
        floor_line();
        help(tr("Drag between two points of known distance, or along the plan's scale bar, and type the real distance in "
                "metres; or type the drawing scale (1:100) when the page has a paper size. Floors of one sheet can share "
                "one scale. A floor without scale is allowed but marked \"not to scale\"."));
        tools({ImportTool::ScaleLine, ImportTool::ScaleBar});
        ImGui::BeginDisabled(!have_floor);
        ScaleForm& sf = w.scale_form();
        ImGui::SetNextItemWidth(gui_detail::px(110.0F));
        input_text("metres###pi.distance", sf.distance_m, 32);
        tooltip(tr("The real distance between the two points, or the scale bar's length, in metres (\"2.5\" or \"2,5\")."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Apply###pi.scale_apply"))) {
            if (sf.kind == ScaleForm::Kind::DrawingScale)
                sf.kind = w.tool() == ImportTool::ScaleBar ? ScaleForm::Kind::ScaleBar : ScaleForm::Kind::TwoPoints;
            report(w.apply_scale());
        }
        tooltip(tr("Set the selected floor's scale from the line drawn on the plan and the distance."));
        const bool ratio_ok = w.drawing_scale_available();
        ImGui::BeginDisabled(!ratio_ok);
        ImGui::SetNextItemWidth(gui_detail::px(110.0F));
        input_text(tr("drawing scale 1:###pi.ratio"), sf.ratio, 32);
        tooltip(ratio_ok ? tr("The scale the plan is drawn at, e.g. 100 for 1:100 (the page's paper size is known).")
                         : tr("Not for this page: it has no paper size (an image without a stored resolution). Use two "
                              "points or the scale bar."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Apply###pi.ratio_apply"))) {
            const ScaleForm::Kind before = sf.kind;
            sf.kind = ScaleForm::Kind::DrawingScale;
            const Result applied = w.apply_scale();
            if (!applied.ok) w.scale_form().kind = before;
            report(applied);
        }
        tooltip(tr("Set the selected floor's scale from the drawing scale."));
        ImGui::EndDisabled();
        if (ImGui::Button(tr("Not to scale###pi.scale_clear"))) report(w.clear_scale());
        tooltip(tr("Remove the scale: the floor is marked \"not to scale\" in the window and in reports."));
        const std::vector<ShareChoice> share = w.share_choices();
        if (!share.empty()) {
            ImGui::TextDisabled(tr("Share this scale with (same sheet):"));
            for (const ShareChoice& c : share) {
                bool on = c.shared;
                if (ImGui::Checkbox((c.label + "###pi.share." + c.floor_id).c_str(), &on))
                    report(w.share_scale(c.floor_id, on));
                tooltip(tr("Floors trimmed from one sheet have the same units, so one scale fits all of them."));
            }
        }
        ImGui::EndDisabled();
    }

    if (gui_detail::section_header(tr("Alignment###pi.align"),
                                   tr("The selected floor's reference point, so the floors line up (IND-09)."))) {
        floor_line();
        help(tr("Click the same spot on every floor, e.g. a staircase corner: the floors line up on it in the "
                "multi-floor view."));
        tools({ImportTool::Reference});
        ImGui::BeginDisabled(!have_floor);
        if (ImGui::Button(tr("Clear reference###pi.ref_clear"))) report(w.clear_reference());
        tooltip(tr("Remove the selected floor's alignment point."));
        ImGui::EndDisabled();
    }
}

void PlanImportView::draw_panel(AppState& state, PlanImportWindow& w) {
    // UI-20: the file name with an Info button; the details behind it.
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(w.title().c_str());
    ImGui::PopTextWrapPos();
    gui_detail::same_line_if(gui_detail::label_w(tr("Info###pi.info")));
    if (gui_detail::small_button(tr("Info###pi.info"))) info_open_ = !info_open_;
    tooltip(tr("Show or hide the information about the plan file: its type, page, size and resolution, and for "
               "an example plan its credit and licence."));
    ImGui::PushTextWrapPos(0.0F);
    if (info_open_) {
        ImGui::TextDisabled("%s", w.description().c_str());
        if (w.large_sheet()) ImGui::TextDisabled(tr("Large sheet: shown in tiles, sharper as you zoom in."));
    }
    // IND-03, DEMO-12, UI-20: a licence-restricted plan's credit, licence,
    // source and notes are behind Info too (closed by default); the credit
    // stays under the floor tabs, in the reports and in About.
    if (const auto& rv = w.restricted(); rv && info_open_) {
        ImGui::Spacing();
        ImGui::TextColored(kWarnColour, "%s", rv->title.c_str());
        ImGui::TextUnformatted(rv->credit.c_str());
        ImGui::Text(tr("Licence: %s  %s"), rv->licence.c_str(), rv->licence_url.c_str());
        if (!rv->item_url.empty()) ImGui::TextDisabled("%s", rv->item_url.c_str());
        if (rv->scale_approximate)
            ImGui::TextColored(kWarnColour, tr("The example floors' scales on this sheet are approximate."));
        ImGui::TextDisabled("%s", rv->note.c_str());
        stats_.credit = true;
    }
    ImGui::PopTextWrapPos();
    draw_properties(state, w);
    if (w.page_count() > 1) draw_pages(w);

    // -- trim: every option with its "(?)" (UI-20) --------------------------------
    ImGui::SeparatorText(tr("Trim"));
    help(tr("Trim the part of the sheet that is one floor: a rectangle, or a polygon for irregular outlines, so "
         "legends, title blocks, sections and other floors stay out. The sheet itself is never changed."));
    Result r;
    for (const ToolView& t : w.tools()) {
        if (t.tool == ImportTool::ScaleLine || t.tool == ImportTool::ScaleBar || t.tool == ImportTool::Reference)
            continue;  // in the Scale and Alignment sections
        if (tool_radio(w, t, r)) report(r);
    }
    if (w.tool() == ImportTool::Polygon && w.drafting()) {
        if (ImGui::Button(tr("Finish###pi.finish"))) report(w.finish_polygon());
        tooltip(tr("Close the outline [Enter]."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Undo point###pi.undo"))) w.undo_point();
        tooltip(tr("Remove the last point [Backspace]."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel###pi.cancel_poly"))) w.cancel_draft();
        tooltip(tr("Drop this outline [Esc]."));
    }
    if (ImGui::Button(tr("Turn left###pi.rot_left"))) report(w.rotate(-1));
    tooltip(tr("Turn the plan 90 degrees anticlockwise."));
    gui_detail::same_line_if(gui_detail::label_w(tr("Turn right###pi.rot_right")));
    if (ImGui::Button(tr("Turn right###pi.rot_right"))) report(w.rotate(+1));
    tooltip(tr("Turn the plan 90 degrees clockwise."));
    gui_detail::same_line_if(gui_detail::label_w(tr("Whole page###pi.reset")));
    if (ImGui::Button(tr("Whole page###pi.reset"))) report(w.reset_trim());
    tooltip(tr("Make the trim the whole page again (the rotation stays)."));
    help(tr("Turn left and Turn right turn the plan by 90 degrees, so the floor stands upright. Whole page makes the "
            "trim the whole page again; the rotation stays."));
    const char* deskew_help = tr("Fine rotation for a scan that is slightly skewed (Ctrl+click to type up to 45 degrees). "
                                 "The \"Deskew line\" tool sets it from a wall or border.");
    float deskew = static_cast<float>(w.deskew());
    ImGui::SetNextItemWidth(-(ImGui::CalcTextSize(tr("Deskew###pi.deskew"), nullptr, true).x +
                              ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemInnerSpacing.x +
                              ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::SliderFloat(tr("Deskew###pi.deskew"), &deskew, -10.0F, 10.0F, "%.2f deg"))
        report(w.set_deskew(static_cast<double>(deskew)));
    tooltip(deskew_help);
    help(deskew_help);
    ImGui::TextDisabled("%s", w.trim_text().c_str());

    // -- add / edit, in the trim section (UI-20) -------------------------------------
    if (w.editing()) {
        ImGui::TextColored(kWarnColour, tr("Editing the trim of a floor"));
        if (ImGui::Button(tr("Apply###pi.apply_edit"))) report(w.apply_edit());
        tooltip(tr("Store the changed trim in the floor. Its scale points, reference point and measured points keep "
                "their place on the sheet."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel###pi.cancel_edit"))) report(w.cancel_edit());
        tooltip(tr("Keep the floor's trim as it was."));
    } else {
        if (ImGui::Button(tr("Add as floor...###pi.add"), ImVec2(-1.0F, gui_detail::target_h()))) w.begin_add_floor();
        tooltip(tr("Turn the current trim into a floor of the project; asks for its name and level."));
    }

    // -- floors in this plan, above the preview (UI-20) --------------------------------
    ImGui::SeparatorText(tr("Floors in this plan"));
    help(tr("Trims already added are outlined and labelled on the sheet. Click one (Pan/select) or its name to select "
         "it; Edit loads its trim to adjust it (IND-07)."));
    const std::vector<PlacedView> placed = w.placed();
    if (placed.empty()) ImGui::TextDisabled(tr("None yet."));
    for (const PlacedView& p : placed) {
        const float row_right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        // The row's name up to its buttons (they do not overlap it).
        // The Edit and Remove buttons at the right, as wide as their labels.
        const float buttons = gui_detail::label_w(tr("Edit###pi.edit.x")) + gui_detail::label_w(tr("Remove###pi.remove.x")) +
                              ImGui::GetStyle().ItemSpacing.x * 2.0F;
        // A name too long for the room beside them takes the whole row, the
        // buttons go under it (TST-52: a name is never cut).
        const float row_x = ImGui::GetCursorPosX();
        const bool beside = ImGui::CalcTextSize(p.label.c_str()).x <= row_right - buttons - row_x;
        const float name_w = beside ? row_right - buttons - row_x : 0.0F;
        if (ImGui::Selectable((p.label + "###pi.floor." + p.floor_id).c_str(), p.selected, 0, ImVec2(name_w, gui_detail::row_h())))
            report(w.select_floor(p.floor_id));
        tooltip(tr("Select this floor for its scale and reference point."));
        if (beside) ImGui::SameLine(std::max(row_right - buttons + ImGui::GetStyle().ItemSpacing.x, 0.0F));
        ImGui::BeginDisabled(w.editing().has_value());
        if (gui_detail::small_button((tr("Edit###pi.edit.") + p.floor_id).c_str())) report(w.edit_floor(p.floor_id));
        tooltip(w.editing() ? tr("Apply or cancel the trim you are editing first.") : tr("Adjust this floor's trim."));
        ImGui::SameLine();
        if (gui_detail::small_button((tr("Remove###pi.remove.") + p.floor_id).c_str())) report(w.request_remove(p.floor_id));
        ImGui::EndDisabled();
        tooltip(tr("Remove this floor (asks first)."));
        const std::string extra = std::string(p.aligned ? tr(", aligned") : "") +
                                  (p.page != w.page() ? tr(", page ") + std::to_string(p.page + 1) : std::string()) +
                                  (p.shared_with.empty() ? std::string() : tr(", shared with ") + p.shared_with);
        ImGui::TextColored(p.to_scale ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : kWarnColour, "  %s%s",
                           p.scale_text.c_str(), extra.c_str());
        if (w.pending_remove() && *w.pending_remove() == p.floor_id) {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextColored(kWarnColour, "%s", w.remove_question().c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button(tr("Remove floor###pi.confirm_remove"))) report(w.confirm_remove());
            tooltip(tr("Remove the floor now."));
            ImGui::SameLine();
            if (ImGui::Button(tr("Keep###pi.keep"))) w.cancel_remove();
            tooltip(tr("Do not remove the floor."));
        }
    }
    draw_preview(w);
    draw_scale(w);
}

// UI-23: the selected floor's name, level and height ("Plan and
// properties..." in the project window opens the window on it).
void PlanImportView::draw_properties(AppState& state, PlanImportWindow& w) {
    if (w.selected().empty()) return;
    FloorForm& f = state.floor_properties_for(w.selected());
    if (f.floor_id != w.selected()) return;
    ImGui::SeparatorText(tr("Floor properties"));
    help(tr("The selected floor's name, its level number (0 = ground floor, 1 = first floor, -1 = basement; the tabs "
            "are ordered by level) and its floor-to-floor height. Apply stores them."));
    const float field = -ImGui::CalcTextSize(tr("Height (m)###pi.prop.height"), nullptr, true).x -
                        ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(field);
    gui_detail::input_text(tr("Name###pi.prop.name"), f.name);
    tooltip(tr("For example \"Ground floor\" or \"Basement\"."));
    ImGui::SetNextItemWidth(field);
    ImGui::InputInt(tr("Level##floor###pi.prop.level"), &f.level);
    tooltip(tr("0 = ground floor, 1 = first floor, -1 = basement. Every floor has its own level; the tabs are "
               "ordered by level."));
    float height = static_cast<float>(f.height_m);
    ImGui::SetNextItemWidth(field);
    if (ImGui::InputFloat(tr("Height (m)###pi.prop.height"), &height, 0.1F, 0.5F, "%.2f"))
        f.height_m = static_cast<double>(height);
    tooltip(tr("Floor-to-floor height in metres (more than 0, at most 50)."));
    if (ImGui::Button(tr("Apply###pi.prop.apply"))) report(state.apply_floor_properties());
    tooltip(tr("Store the floor's name, level and height."));
}

void PlanImportView::draw_add_popup(PlanImportWindow& w) {
    // IND-06, UI-20: "Add as floor" asks for the floor's name, level and
    // height in a popup over the window.
    const char* id = tr("Add as floor###pi.add_popup");
    ImportFloorForm* f = w.add_form();
    if (f != nullptr && !ImGui::IsPopupOpen(id)) {
        ImGui::OpenPopup(id);
        add_error_.clear();
    }
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    if (f == nullptr) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const float field_w = std::min(gui_detail::em(200.0F), ImGui::GetMainViewport()->WorkSize.x * 0.45F);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(field_w);
    input_text(tr("Name###pi.add_name"), f->name);
    tooltip(tr("For example \"Ground floor\" or \"Basement\"."));
    ImGui::SetNextItemWidth(field_w);
    ImGui::InputInt(tr("Level###pi.add_level"), &f->level);
    tooltip(tr("0 = ground floor, 1 = first floor, -1 = basement. Every floor has its own level."));
    float h = static_cast<float>(f->height_m);
    ImGui::SetNextItemWidth(field_w);
    if (ImGui::InputFloat(tr("Height (m)###pi.add_height"), &h, 0.1F, 0.5F, "%.2f")) f->height_m = h;
    tooltip(tr("Floor-to-floor height in metres (more than 0, at most 50)."));
    if (!add_error_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + field_w * 1.5F);
        ImGui::TextColored(kErrorColour, "%s", add_error_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    if (gui_detail::dialog_button(tr("Add###pi.add_ok"), tr("Add the trim as this floor. The window stays open for the next floor."))) {
        const Result added = w.confirm_add_floor();
        report(added);
        if (added.ok) ImGui::CloseCurrentPopup();
        else add_error_ = added.message;
    }
    ImGui::SameLine();
    if (gui_detail::dialog_button(tr("Cancel###pi.add_cancel"), tr("Do not add a floor.")) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        w.cancel_add_floor();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void PlanImportView::draw_tiles(PlanImportWindow& w, ImDrawList* dl, ImVec2 origin) {
    if (!tiles_) return;
    plan::TileRenderer& r = *tiles_;
    const plan::TileLayout& layout = r.layout();
    const plan::PlanView& view = w.view();
    const auto [lo, hi] = view.visible_page_bounds();
    const int lod = layout.lod_for(view.zoom());
    // The coarsest level always lies below, so there are no holes while
    // finer tiles render.
    std::vector<plan::TileKey> keys =
        layout.tiles_for(w.page(), layout.max_lod(), 0.0, 0.0, layout.extent().width, layout.extent().height);
    if (lod != layout.max_lod()) {
        const auto fine = layout.tiles_for(w.page(), lod, lo.x, lo.y, hi.x, hi.y);
        keys.insert(keys.end(), fine.begin(), fine.end());
    }
    r.request(keys);
    if (wait_for_tiles_) r.wait_idle();
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
        const double x0 = static_cast<double>(px.x) / s, y0 = static_cast<double>(px.y) / s;
        const double x1 = static_cast<double>(px.x + px.width) / s, y1 = static_cast<double>(px.y + px.height) / s;
        // The page is turned by the trim's rotation: tiles are quads.
        dl->AddImageQuad(tex->second.ref(), screen(view, origin, {x0, y0}), screen(view, origin, {x1, y0}),
                         screen(view, origin, {x1, y1}), screen(view, origin, {x0, y1}));
        ++stats_.tiles;
    }
}

void PlanImportView::draw_canvas(PlanImportWindow& w) {
    ImGuiIO& io = ImGui::GetIO();
    plan::PlanView& view = w.view();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 64.0F);
    size.y = std::max(size.y, 64.0F);
    stats_.canvas_origin = origin;
    stats_.canvas_size = size;
    const plan::PageSize page = w.import().source().page_size(w.page());
    if (view.viewport_width() != static_cast<double>(size.x) || view.viewport_height() != static_cast<double>(size.y) ||
        !fitted_) {
        view.set_viewport(size.x, size.y);
        if (!fitted_) view.fit(page, w.quarter_turns(), w.deskew());
        fitted_ = true;
    }
    w.set_pick_radius(8.0 / std::max(view.zoom(), 1e-9));

    ImGui::InvisibleButton("###pi.canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    tooltip(w.hint());
    const bool hovered = ImGui::IsItemHovered();
    const Point2 mouse{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
    const Point2 at = view.screen_to_page(mouse);
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) capturing_ = w.pointer_down(at);
    if (ImGui::IsItemActive()) {
        const bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (left && capturing_)
            w.pointer_move(at);
        else if (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F)
            view.pan_by(Point2{io.MouseDelta.x, io.MouseDelta.y});
    }
    if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        report(w.pointer_up(at));
        capturing_ = false;
    }
    if (hovered && io.MouseWheel != 0.0F) view.zoom_at(std::pow(1.2, static_cast<double>(io.MouseWheel)), mouse);
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle)) view.fit(page, w.quarter_turns(), w.deskew());
    if (!io.WantTextInput && (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || hovered)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && w.tool() == ImportTool::Polygon && w.drafting())
            report(w.finish_polygon());
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) w.undo_point();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) w.cancel_draft();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, end, true);
    dl->AddRectFilled(origin, end, IM_COL32(48, 50, 54, 255));
    // White paper under the page (a transparent page shows it).
    dl->AddQuadFilled(screen(view, origin, {0, 0}), screen(view, origin, {page.width, 0}),
                      screen(view, origin, {page.width, page.height}), screen(view, origin, {0, page.height}),
                      IM_COL32(255, 255, 255, 255));
    draw_tiles(w, dl, origin);

    // Floors already trimmed from this page: outline + label (IND-06).
    for (const PlacedView& p : w.placed()) {
        if (!p.on_page) continue;
        const ImU32 col = p.selected ? kSelectedColour : kPlacedColour;
        if (!p.editing) outline(dl, view, origin, p.outline, col, p.selected ? 3.5F : 2.5F);
        label(dl, screen(view, origin, p.label_at), p.label + (p.to_scale ? "" : tr(" - not to scale")), col);
        if (p.scale_line && !p.selected)
            scale_line(dl, screen(view, origin, p.scale_line->first), screen(view, origin, p.scale_line->second),
                       IM_COL32(200, 30, 160, 160), "");
        if (p.reference) cross(dl, screen(view, origin, *p.reference), col);
        ++stats_.placed;
    }
    // The current trim, the shape being drawn and its handles.
    const DraftView d = w.draft();
    outline(dl, view, origin, d.trim, kTrimColour, 2.5F);
    for (const Point2& h : d.handles) {
        const ImVec2 s = screen(view, origin, h);
        dl->AddRectFilled(ImVec2(s.x - 5, s.y - 5), ImVec2(s.x + 5, s.y + 5), IM_COL32(255, 255, 255, 255));
        dl->AddRect(ImVec2(s.x - 5, s.y - 5), ImVec2(s.x + 5, s.y + 5), kTrimColour, 0.0F, 2.0F);
    }
    if (!d.polygon.empty()) {
        std::vector<Point2> pts = d.polygon;
        if (hovered) pts.push_back(at);
        outline(dl, view, origin, pts, IM_COL32(255, 60, 0, 255), 2.0F, false);
        for (const Point2& p : d.polygon) dl->AddCircleFilled(screen(view, origin, p), 4.5F, IM_COL32(255, 60, 0, 255));
    }
    if (d.line)
        dl->AddLine(screen(view, origin, d.line->first), screen(view, origin, d.line->second), kScaleColour, 3.0F);
    // The selected floor's scale line as in the form.
    const ScaleForm& sf = w.scale_form();
    if (sf.a && sf.b && !w.selected().empty() && !d.line && sf.kind != ScaleForm::Kind::DrawingScale)
        scale_line(dl, screen(view, origin, *sf.a), screen(view, origin, *sf.b), kScaleColour,
                   sf.distance_m.empty() ? std::string("? m") : sf.distance_m + " m");
    if (hovered) {
        char pos[64];
        std::snprintf(pos, sizeof pos, "x %.0f  y %.0f", at.x, at.y);
        dl->AddText(ImVec2(origin.x + 8.0F, end.y - ImGui::GetTextLineHeight() - 6.0F), IM_COL32(230, 230, 230, 255),
                    pos);
    }
    dl->PopClipRect();
}

}  // namespace yantra::app
