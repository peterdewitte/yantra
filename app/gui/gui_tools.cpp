// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project window's tools (step 9.4): the walk test and the mouse
// survey (IND-20, GEO-02, GEO-05, DEMO-14), the interpolation commands and
// the interpolation settings (IND-41, IND-43, IND-45). Drawing and input
// only; the logic is in LiveSurvey and InterpolationRunner (app/model).
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "gui.h"
#include "yantra/app/i18n.h"
#include "gui_widgets.h"

namespace yantra::app {

using namespace gui_detail;

namespace {

/// A double input with a tooltip; true when changed.
bool input_double(const char* label, double& v, double step, const char* format, const char* tip) {
    const bool changed = ImGui::InputDouble(label, &v, step, step * 10.0, format);
    text_tooltip(tip);
    return changed;
}

/// A button that stays visible when disabled, with the reason on hover (DOC-07 c).
bool tool_button(const std::string& label, bool enabled, const std::string& tip, const std::string& reason,
                 bool active = false) {
    ImGui::BeginDisabled(!enabled);
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    const bool pressed = ImGui::Button(label.c_str());
    if (active) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    const bool shown = note_help(!enabled && !reason.empty(), tip.c_str(), enabled ? nullptr : reason.c_str());
    if (shown || (!help_shown_on_purpose() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
        ImGui::TextUnformatted(tip.c_str());
        if (!enabled && !reason.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kBannerColour, "%s", reason.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    return pressed && enabled;
}

void wrapped(const std::string& text, bool warn = false) {
    if (text.empty()) return;
    ImGui::PushTextWrapPos(0.0F);
    if (warn) ImGui::TextColored(kBannerColour, "%s", text.c_str());
    else ImGui::TextDisabled("%s", text.c_str());
    ImGui::PopTextWrapPos();
}

ImVec2 plan_badge(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 bg) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 end(pos.x + ts.x + 16.0F, pos.y + ts.y + 10.0F);
    dl->AddRectFilled(pos, end, bg, 4.0F);
    dl->AddText(ImVec2(pos.x + 8.0F, pos.y + 5.0F), IM_COL32(255, 255, 255, 255), text);
    return ImVec2(end.x - pos.x, end.y - pos.y);
}

}  // namespace

// -- walk test (IND-20, GEO-02, GEO-05, DEMO-14) -----------------------------------------------

void Gui::draw_walk_section(const FloorContent* c) {
    LiveSurvey* live = state_.live();
    if (live == nullptr) return;
    ImGui::SeparatorText(tr("Walk test"));
    help_marker(tr("Measure this floor with a radio: click where you are on the plan as you walk (point and click, "
                "continuous), measure at chosen points (manual), or, with the simulator, let the mouse stand in "
                "for the surveyor (mouse survey). The points are stored in the project as a walk test of the floor."));
    const bool running = live->running();
    ImGui::BeginDisabled(running);
    if (ImGui::Button(live->radios_searched() ? tr("Search again###walk.find_radios") : tr("Find radios###walk.find_radios")))
        (void)state_.show(live->find_radios());
    ImGui::EndDisabled();
    text_tooltip(tr("Load the radio plugins and list their devices (a second or two; the simulator needs no hardware)."));
    if (live->radios_searched() && !live->radios().empty()) {
        const RadioChoice* ch = live->chosen();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::BeginDisabled(running);
        if (ImGui::BeginCombo("###walk.radio", ch ? ch->label.c_str() : tr("Choose a radio"))) {
            for (std::size_t i = 0; i < live->radios().size(); ++i) {
                const RadioChoice& r = live->radios()[i];
                ImGui::BeginDisabled(!r.usable);
                if (ImGui::Selectable((r.label + "###walk.radio." + std::to_string(i)).c_str(), ch == &r))
                    (void)state_.show(live->choose_radio(i));
                ImGui::EndDisabled();
                if (!r.usable) text_tooltip(r.reason.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        text_tooltip(tr("The radio the walk test measures with."));
    }
    wrapped(live->radio_note(), live->radios().empty() && live->radios_searched());

    WalkForm& f = live->form();
    ImGui::BeginDisabled(running);
    ImGui::SetNextItemWidth(combo_w({walk_kind_label(WalkKind::PointAndClick), walk_kind_label(WalkKind::Continuous),
                                     walk_kind_label(WalkKind::Manual), walk_kind_label(WalkKind::MouseSurvey)}));
    if (ImGui::BeginCombo(tr("Kind###walk.kind"), walk_kind_label(f.kind))) {
        for (WalkKind k : {WalkKind::PointAndClick, WalkKind::Continuous, WalkKind::Manual, WalkKind::MouseSurvey})
            if (ImGui::Selectable((std::string(walk_kind_label(k)) + "###walk.kind." + walk_kind_key(k)).c_str(), f.kind == k))
                f.kind = k;
        ImGui::EndCombo();
    }
    text_tooltip(tr("Point and click: click the start, walk, click the end. Continuous: every click ends a segment and "
                 "starts the next. Manual: click a point, it is measured for the set time. Mouse survey: the "
                 "simulator measures where the mouse is."));
    if (f.kind == WalkKind::Manual) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
        (void)input_double("Seconds per point###walk.seconds", f.manual_seconds, 1.0, "%.0f",
                           tr("How long each manual point is measured; the cycles are averaged (GEO-05)."));
    }
    if (f.kind == WalkKind::MouseSurvey) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
        (void)input_double("Point spacing (m)###walk.spacing", f.spacing_m, 0.05, "%.2f",
                           tr("At most one point per this distance (default 0.25 m)."));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
        (void)input_double("Speed warning (m/s)###walk.max_speed", f.max_speed_mps, 0.5, "%.1f",
                           "\"Move slower\" appears when the mouse moves faster than this over the plan (default 2 m/s).");
    }
    input_text(tr("Name###walk.name"), f.name, 128);
    text_tooltip(tr("The walk test's name (default: the kind and the date)."));
    ImGui::EndDisabled();

    if (!running) {
        const std::string why = live->start_reason(c->floor_id);
        if (tool_button(tr("Start###walk.start"), why.empty(), tr("Start measuring on this floor."), why))
            (void)state_.show(live->start(c->floor_id));
        return;
    }
    const WalkView v = live->view();
    ImGui::Text(tr("%zu points stored"), v.points);
    wrapped(v.radio);
    if (!v.calibration_note.empty()) wrapped(v.calibration_note);
    if (v.kind == WalkKind::PointAndClick || v.kind == WalkKind::Continuous) {
        if (ImGui::Button(tr("Pause###walk.pause"))) (void)state_.show(live->pause());
        text_tooltip(tr("Pause: the open segment is dropped (its end is not known); the next click starts again."));
        ImGui::SameLine();
    }
    if (v.kind != WalkKind::MouseSurvey) {
        if (ImGui::Button(tr("Delete last###walk.delete_last"))) (void)state_.show(live->delete_last());
        text_tooltip(v.kind == WalkKind::Manual ? tr("Remove the last point.") : tr("Remove the last segment and its points."));
        ImGui::SameLine();
    }
    if (ImGui::Button(tr("Stop###walk.stop"))) (void)state_.show(live->stop());
    text_tooltip(v.kind == WalkKind::MouseSurvey ? tr("End the mouse survey; its points stay [Esc]") : tr("End the walk test; its points stay."));
}

// -- interpolation (IND-41..45) ----------------------------------------------------------------

void Gui::draw_interpolation_section(const FloorContent* c) {
    InterpolationRunner* run = state_.interpolation();
    if (run == nullptr) return;
    ImGui::SeparatorText(tr("Interpolation"));
    help_marker(tr("The point interpolation (IDW): the measured levels of this floor spread over the floor, each point "
                "weighted by its distance; only the measured points count. It runs in the background and is stored in "
                "the project with its settings. The map is always labelled \"interpolated\"."));
    const InterpolationProgress p = run->progress();
    if (p.running) {
        overlay_.progress = true;
        ImGui::ProgressBar(static_cast<float>(p.progress), ImVec2(-1.0F, 0.0F), p.stage.c_str());
        wrapped(p.text);
        if (ImGui::Button(tr("Cancel###interp.cancel"))) run->cancel();
        text_tooltip(tr("Stop the interpolation; nothing is stored."));
    } else {
        const std::string why = run->run_reason(c->floor_id);
        if (tool_button(tr("Run###interp.run"), why.empty(),
                        tr("Compute a new map of this floor from its measured points (serving carrier level)."), why))
            (void)state_.run_interpolation(false);
        if (c->interpolation) {
            const bool stale = c->interpolation->stale;
            const char* rerun = stale ? tr("Run again (stale)###interp.rerun") : tr("Run again###interp.rerun");
            // Beside Run when it fits, else on a line of its own (TST-52).
            same_line_if(label_w(rerun));
            if (tool_button(rerun, why.empty(),
                            tr("Compute the shown map again with the same metric and the current points and settings."),
                            why, stale))
                (void)state_.run_interpolation(true);
        }
    }
    if (c->interpolation && c->interpolation->stale) {
        std::string reasons;
        for (const std::string& r : c->interpolation->stale_reasons) reasons += (reasons.empty() ? "" : ", ") + r;
        wrapped(tr("Stale: ") + reasons + ". Run again to update it.", true);
    }
    if (ImGui::Button(tr("Settings...###interp.settings"))) (void)state_.edit_interpolation_settings();
    text_tooltip(tr("Grid size, power, search radius and confidence distance of the interpolation (stored in the project)."));
}

// -- the plan: overlay and input -------------------------------------------------------------

void Gui::draw_plan_overlay(ImDrawList* dl) {
    overlay_ = ToolOverlay{{}, false, overlay_.progress};
    const PlanCanvas& cv = *canvas_;
    LiveSurvey* live = state_.live();
    if (live == nullptr || !live->running()) return;
    const WalkView v = live->view();
    const auto& in = cv.input();
    const ImU32 walk_col = IM_COL32(0, 170, 120, 255);
    if (v.open_start) {
        const ImVec2 s = cv.screen_of(*v.open_start);
        dl->AddCircleFilled(s, 6.0F, walk_col);
        if (in.mouse && in.hovered) dl->AddLine(s, cv.screen_of(*in.mouse), IM_COL32(0, 170, 120, 160), 2.0F);
    }
    if (v.manual_point) dl->AddCircle(cv.screen_of(*v.manual_point), 12.0F, walk_col, 0, 3.0F);
    // The hint (DEMO-14) and the "move slower" warning, bottom centre.
    overlay_.hint = v.hint;
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float w = ImGui::CalcTextSize(v.hint.c_str()).x + 16.0F;
    ImVec2 at((min.x + max.x - w) * 0.5F, max.y - 80.0F);
    const ImVec2 sz = plan_badge(dl, at, v.hint.c_str(), IM_COL32(20, 90, 70, 220));
    if (v.too_fast) {
        char buf[96];
        std::snprintf(buf, sizeof buf, tr("Move slower (%.1f m/s): the points cannot follow"), v.speed_mps);
        at.y += sz.y + 4.0F;
        plan_badge(dl, at, buf, IM_COL32(200, 40, 30, 235));
        overlay_.move_slower = true;
    }
}

void Gui::handle_canvas_input() {
    const CanvasInput& in = canvas_->input();
    LiveSurvey* live = state_.live();
    if (live != nullptr) live->cursor(in.hovered ? in.mouse : std::nullopt, ImGui::GetTime());
    const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (live != nullptr && live->running()) {
        const WalkView v = live->view();
        if (v.kind == WalkKind::MouseSurvey) {
            // DEMO-14: a click interpolates the floor's points; Esc stops.
            if (in.pressed) (void)state_.run_interpolation(false);
            if (keys && ImGui::IsKeyPressed(ImGuiKey_Escape)) (void)state_.show(live->stop());
        } else if (in.pressed && in.mouse) {
            (void)state_.show(live->click(*in.mouse));
        }
        return;
    }
}

// -- dialogs ------------------------------------------------------------------------------------

void Gui::draw_interpolation_dialog() {
    InterpolationRunner* run = state_.interpolation();
    if (run == nullptr) {
        state_.cancel_dialog();
        return;
    }
    InterpolationForm& f = run->settings_form();
    project::InterpolationSettings& s = f.settings;
    (void)input_double(tr("Grid (m)###grid"), s.grid_m, 0.05, "%.2f", tr("The size of a map cell, 0.05 to 10 m (default 0.5 m)."));
    (void)input_double(tr("Power###power"), s.power, 0.5, "%.1f",
                       tr("The inverse-distance weight is 1 / distance ^ power, 0.5 to 6 (default 2)."));
    (void)input_double(tr("Search radius (m)###radius"), s.radius_m, 1.0, "%.1f",
                       tr("Measured points farther than this do not count; no value beyond it, 0.5 to 1000 m (default 10 m)."));
    (void)input_double(tr("Full confidence (m)###full_confidence"), s.full_confidence_m, 0.5, "%.1f",
                       tr("Up to this distance from a measured point the map is not faded; it fades out at the search radius (default 2 m)."));
    ImGui::Spacing();
    if (dialog_button(tr("Apply###dlg.ok"), tr("Store the settings; maps made with others become stale"))) (void)state_.confirm_interpolation_settings();
    ImGui::SameLine();
    if (dialog_button(tr("Defaults###prop.defaults"), tr("Put the default values in the form (stored with Apply)"))) run->default_settings();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close without storing [Esc]")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
    error_line(state_);
}

}  // namespace yantra::app
