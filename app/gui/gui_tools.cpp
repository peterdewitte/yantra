// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project window's tabs (UI-18, UI-19): Walk with the walk test and
// the mouse survey (IND-20, GEO-02, GEO-05, DEMO-14), the live values and
// the recording; Map with the Show row, the interpolation commands and
// the Advanced dropdown (IND-41, IND-43, IND-45, PP-02..05, EXP-06).
// Drawing and input only; the logic is in LiveSurvey, ProjectWindow and
// InterpolationRunner (app/model).
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "gui.h"
#include "yantra/app/i18n.h"
#include "yantra/core/project/project.h"
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

ImVec2 plan_badge(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 bg, ImU32 fg = IM_COL32(255, 255, 255, 255)) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 end(pos.x + ts.x + 16.0F, pos.y + ts.y + 10.0F);
    dl->AddRectFilled(pos, end, bg, 4.0F);
    dl->AddText(ImVec2(pos.x + 8.0F, pos.y + 5.0F), fg, text);
    return ImVec2(end.x - pos.x, end.y - pos.y);
}

/// The explanation of a walk test kind for its "(?)" (UI-21).
const char* walk_kind_help(WalkKind k) {
    switch (k) {
    case WalkKind::PointAndClick:
        return tr("Click where you are at the start, walk at a steady pace in a straight line and click where you "
                  "are at the end: the measurements are spread along the line.");
    case WalkKind::Continuous:
        return tr("As point and click, but every click ends a segment and starts the next, so you walk a route "
                  "without stopping.");
    case WalkKind::Manual:
        return tr("Click a point on the plan and stay there: it is measured for the set number of seconds and the "
                  "cycles are averaged.");
    case WalkKind::MouseSurvey:
        return tr("With the simulator: the mouse stands in for the surveyor and the simulator measures where the "
                  "mouse is on the plan.");
    }
    return "";
}

}  // namespace

// -- the Walk tab (UI-18) ------------------------------------------------------------------------

void Gui::draw_walk_page(const FloorContent* c) {
    LiveSurvey* live = state_.live();
    if (live == nullptr || state_.monitor() == nullptr) return;
    draw_walk_section(c);
    draw_live_values(state_.monitor()->frame());
    draw_recording_controls(*live);
    // REC-07: replaying a recording into the live values and the windows.
    const bool replaying = state_.replay() != nullptr && state_.replay()->is_open();
    if (replaying) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    const bool open = ImGui::CollapsingHeader(tr("Replay###walk.replay"));
    text_tooltip(tr("Replay a recording of the project into the live values and the survey windows (Window menu)."));
    if (open && state_.replay() != nullptr) draw_replay_controls();
    ImGui::TextDisabled("%s", tr("Neighbours, graphs, scanner, events and statistics: Window menu."));
}

// -- the Map tab (UI-19) -------------------------------------------------------------------------

void Gui::draw_map_page(ProjectWindow& w, const FloorContent* c) {
    // The Show row: plan, measured points and the map with its opacity.
    ImGui::SeparatorText(tr("Show"));
    help_marker(tr("Switch the plan, the measured points and the interpolated map on or off. These are your viewing "
                   "settings: they do not change the project or its report."));
    const std::vector<LayerRow> rows = w.layer_rows(state_.settings().layers);
    auto row_of = [&](Layer l) -> const LayerRow* {
        for (const LayerRow& r : rows)
            if (r.layer == l) return &r;
        return nullptr;
    };
    bool first = true;
    const std::pair<Layer, const char*> shown[] = {{Layer::Plan, tr("Plan###layer.plan")},
                                                   {Layer::Points, tr("Points###layer.points")},
                                                   {Layer::Interpolation, tr("Map###layer.interpolation")}};
    for (const auto& [layer, label] : shown) {
        const LayerRow* r = row_of(layer);
        if (r == nullptr) continue;
        if (!first) same_line_if(switch_w(label));
        first = false;
        bool on = r->visible;
        if (ImGui::Checkbox(label, &on)) state_.set_layer_visible(layer, on);
        text_tooltip(r->tooltip.c_str());
    }
    {
        float pct = static_cast<float>(state_.settings().layers.map_opacity * 100.0);
        ImGui::BeginDisabled(!state_.settings().layers.visible(Layer::Interpolation));
        const float slider_w = std::min(em(140.0F), ImGui::GetContentRegionAvail().x * 0.6F);
        const std::string opacity_label = label_above_if_narrow(tr("Map opacity###map.opacity"), slider_w);
        ImGui::SetNextItemWidth(slider_w);
        if (ImGui::SliderFloat(opacity_label.c_str(), &pct, static_cast<float>(kMinMapOpacity * 100.0), 100.0F,
                               "%.0f %%", ImGuiSliderFlags_AlwaysClamp))
            state_.set_map_opacity(static_cast<double>(pct) / 100.0);
        ImGui::EndDisabled();
        text_tooltip(tr("How much of the plan shows through the map (10 to 100 %)."));
    }
    for (Layer l : {Layer::Points, Layer::Interpolation})
        if (const LayerRow* r = row_of(l); r != nullptr && !r->note.empty())
            wrapped(r->note, l == Layer::Interpolation && c != nullptr && c->interpolation && c->interpolation->stale);
    if (c == nullptr) return;
    ImGui::TextDisabled(tr("%zu measured points"), c->points.count());
    if (c->metres_per_unit)
        ImGui::TextDisabled(tr("Scale: %.4g m per plan unit"), *c->metres_per_unit);
    else
        ImGui::TextColored(kBannerColour, "%s", tr("Not to scale"));
    if (!canvas_->tile_error().empty() && c->has_plan) wrapped(canvas_->tile_error(), true);

    // Compute the map (IND-45), and choose one of several stored maps.
    draw_interpolation_section(c);
    if (c->interpolation) {
        ImGui::TextDisabled(tr("IDW of %zu measured points"), c->interpolation->points);
        if (c->interpolations.size() > 1) {
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::BeginCombo("###layer.interpolation.choose", c->interpolation->key.c_str())) {
                for (const auto& [iid, key] : c->interpolations)
                    if (ImGui::Selectable((key + "###layer.map." + std::to_string(iid)).c_str(), iid == c->interpolation->id))
                        (void)w.show_interpolation(iid);
                ImGui::EndCombo();
            }
            text_tooltip(tr("The floor has several stored maps (one per metric and cell): choose the one shown."));
        }
    }

    // UI-19: the post-processing settings, closed by default.
    ImGui::Spacing();
    const bool advanced = ImGui::CollapsingHeader(tr("Advanced###map.advanced"));
    text_tooltip(tr("Post-processing: the value and view of the points, failed cells, level offset, binning, the "
                    "interpolation settings and the acceptance profile."));
    if (!advanced) return;
    survey_.advanced_open = true;
    help_button("11.1");
    draw_interpolation_settings();
    draw_post_section();
}

// -- walk test (IND-20, GEO-02, GEO-05, DEMO-14) -----------------------------------------------

void Gui::draw_walk_section(const FloorContent* c) {
    LiveSurvey* live = state_.live();
    if (live == nullptr) return;
    ImGui::SeparatorText(tr("Walk test"));
    help_marker(tr("Measure this floor with a radio: click where you are on the plan as you walk (point and click, "
                "continuous), measure at chosen points (manual), or, with the simulator, let the mouse stand in "
                "for the surveyor (mouse survey). The points are stored in the project as a walk test of the floor."));
    const bool running = live->running();
    // UI-21: the radio in use, chosen in Radio > Radios... (Select...).
    const RadioChoice* ch = live->chosen();
    ImGui::PushTextWrapPos(0.0F);
    if (ch != nullptr)
        ImGui::Text(tr("Radio: %s"), ch->label.c_str());
    else
        ImGui::TextDisabled("%s", tr("No radio"));
    ImGui::PopTextWrapPos();
    text_tooltip(tr("The radio the walk test measures with."));
    same_line_if(label_w(tr("Select...###walk.radio_select")));
    if (ImGui::Button(tr("Select...###walk.radio_select"))) {
        if (RadioWindows* rw = state_.radio_windows()) {
            (void)rw->open(RadioWindow::List);
            // The first time the radios are searched at once.
            if (!live->radios_searched()) (void)state_.show(rw->find_radios());
        }
    }
    text_tooltip(tr("Open Radio > Radios...: find the radios and choose the one the walk test measures with."));
    wrapped(live->radio_note(), live->radios().empty() && live->radios_searched());
    if (live->radios_searched()) draw_radio_controls(*live);

    WalkForm& f = live->form();
    ImGui::BeginDisabled(running);
    // UI-21: the kind as options, each with its "(?)" (as the plan import's trim tools).
    ImGui::TextUnformatted(tr("Kind of walk test"));
    for (WalkKind k : {WalkKind::PointAndClick, WalkKind::Continuous, WalkKind::Manual, WalkKind::MouseSurvey})
        if (option_radio((std::string(walk_kind_label(k)) + "###walk.kind." + walk_kind_key(k)).c_str(), f.kind == k,
                         walk_kind_help(k)))
            f.kind = k;
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
        if (tool_button(tr("Compute map###interp.run"), why.empty(),
                        tr("Compute a new map of this floor from its measured points (serving carrier level)."), why))
            (void)state_.run_interpolation(false);
        if (c->interpolation) {
            const bool stale = c->interpolation->stale;
            const char* rerun = stale ? tr("Compute again (stale)###interp.rerun") : tr("Compute again###interp.rerun");
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
        wrapped(trf("Stale: {}. Compute again to update it.", {reasons}), true);
    }
}

void Gui::draw_interpolation_settings() {
    const project::Project* p = state_.project();
    if (p == nullptr) return;
    ImGui::SeparatorText(tr("Interpolation"));
    const project::InterpolationSettings s = p->interpolation_settings();
    ImGui::TextDisabled(tr("Grid %.2f m, power %.1f, radius %.1f m"), s.grid_m, s.power, s.radius_m);
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
    // DEMO-15: the hint is a demo explanation: the green box of the theme.
    const DemoColours dc = demo_colours(layout().theme);
    ImVec2 sz;
    if (v.kind == WalkKind::MouseSurvey) {
        ImGui::PushStyleColor(ImGuiCol_Text, dc.text);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, dc.muted);
        note_demo_box("mouse survey hint", dc);
        ImGui::PopStyleColor(2);
        sz = plan_badge(dl, at, v.hint.c_str(), ImGui::GetColorU32(dc.bg), ImGui::GetColorU32(dc.text));
    } else {
        sz = plan_badge(dl, at, v.hint.c_str(), IM_COL32(20, 90, 70, 220));
    }
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
