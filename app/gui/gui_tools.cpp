// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project window's tools (step 9.4): wall and antenna editing
// (IND-11, IND-12), the walk test and the mouse survey (IND-20, GEO-02,
// GEO-05, DEMO-14), the interpolation commands and the propagation
// settings (IND-41..45). Drawing and input only; the logic is in
// FloorEditor, LiveSurvey and InterpolationRunner (app/model).
#include <imgui.h>

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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) {
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

// -- editing (IND-11, IND-12) ----------------------------------------------------------------

void Gui::draw_edit_section() {
    FloorEditor* ed = state_.floor_editor();
    if (ed == nullptr) return;
    ImGui::SeparatorText(tr("Edit"));
    help_marker(tr("Draw and change the walls and antennas of this floor. Every change is stored at once; Undo takes it "
                "back [Ctrl+Z], Redo puts it back [Ctrl+Y]. The interpolation uses the walls' classes and the "
                "antennas' properties."));
    const bool walking = state_.live() != nullptr && state_.live()->running();
    ImGui::BeginDisabled(walking);
    for (const EditToolView& t : ed->tools()) {
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(t.label.c_str()).x + 30.0F) ImGui::NewLine();
        if (tool_button(t.label + "###" + t.id, t.enabled, t.tooltip, t.reason, ed->tool() == t.tool))
            (void)state_.show(ed->set_tool(t.tool));
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::EndDisabled();
    if (walking) wrapped(tr("Editing is off while a walk test runs: clicks on the plan go to the walk."));

    // The class of new walls (IND-11).
    const auto mats = ed->materials();
    std::string current = ed->draw_class();
    for (const MaterialRow& m : mats)
        if (m.id == ed->draw_class()) current = m.name;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0F);
    if (ImGui::BeginCombo(tr("New walls###edit.draw_class"), current.c_str())) {
        for (const MaterialRow& m : mats)
            if (ImGui::Selectable((m.name + "###edit.draw_class." + m.id).c_str(), m.id == ed->draw_class()))
                (void)state_.show(ed->set_draw_class(m.id));
        ImGui::EndCombo();
    }
    text_tooltip(tr("The wall class (material) new walls get; its attenuation is in the wall-class table."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Classes...###edit.materials"))) (void)state_.edit_materials();
    text_tooltip(tr("The wall-class table: attenuation per material and frequency band; add or change classes."));

    if (tool_button(tr("Undo###edit.undo"), ed->can_undo(), ed->can_undo() ? ed->undo_label() + " [Ctrl+Z]" : tr("Undo the last edit [Ctrl+Z]"),
                    tr("Nothing to undo.")))
        (void)state_.show(ed->undo());
    ImGui::SameLine();
    if (tool_button(tr("Redo###edit.redo"), ed->can_redo(), ed->can_redo() ? ed->redo_label() + " [Ctrl+Y]" : tr("Redo the edit undone last [Ctrl+Y]"),
                    tr("Nothing to redo.")))
        (void)state_.show(ed->redo());
    wrapped(ed->undo_note(), true);
    if (!ed->draft().empty())
        wrapped(tr("Drawing a wall: ") + std::to_string(ed->draft().size()) +
                tr(" corners. Double click or Enter ends it, Backspace removes the last corner, Esc drops it."));

    const Selection& sel = ed->selection();
    if (sel.kind == Selection::Kind::None) return;
    wrapped(ed->selection_text());
    if (sel.kind == Selection::Kind::Wall) {
        WallForm& f = ed->wall_form();
        std::string cname = f.wall_class;
        for (const MaterialRow& m : mats)
            if (m.id == f.wall_class) cname = m.name;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0F);
        if (ImGui::BeginCombo(tr("Class###edit.wall_class"), cname.c_str())) {
            for (const MaterialRow& m : mats)
                if (ImGui::Selectable((m.name + "###edit.wall_class." + m.id).c_str(), m.id == f.wall_class)) {
                    f.wall_class = m.id;
                    (void)state_.show(ed->apply_wall_form());
                }
            ImGui::EndCombo();
        }
        text_tooltip(tr("The wall's class: its attenuation comes from the wall-class table at the measured frequency."));
        if (ImGui::Checkbox(tr("Own attenuation###edit.own_value"), &f.own_value)) (void)state_.show(ed->apply_wall_form());
        text_tooltip(tr("Give this wall its own attenuation in dB instead of the class value (e.g. a measured wall)."));
        if (f.own_value) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0F);
            if (ImGui::InputDouble("dB###edit.attenuation", &f.attenuation_db, 0.5, 5.0, "%.1f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                (void)state_.show(ed->apply_wall_form());
            text_tooltip(tr("The wall's own attenuation (0 to 100 dB); Enter stores it."));
        }
        const std::string why = ed->split_reason();
        if (tool_button(tr("Split###edit.split"), why.empty(), tr("Split the wall at the selected corner into two walls."), why))
            (void)state_.show(ed->split_selected());
        ImGui::SameLine();
    } else {
        if (ImGui::Button(tr("Properties...###edit.antenna_props"))) (void)state_.edit_antenna();
        text_tooltip(tr("Type, gain, pattern, height, EIRP and the cell the antenna serves (IND-12)."));
        ImGui::SameLine();
    }
    if (ImGui::Button(tr("Delete###edit.delete"))) (void)state_.show(ed->delete_selected());
    text_tooltip(sel.kind == Selection::Kind::Wall && sel.vertex ? tr("Remove the selected corner (a wall of two corners is removed) [Delete]")
                                                                 : tr("Remove the selection [Delete]"));
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
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0F);
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
        if (tool_button(tr("Start###walk.start"), why.empty(), tr("Start measuring on this floor."), why)) {
            (void)state_.show(live->start(c->floor_id));
            if (state_.floor_editor()) (void)state_.floor_editor()->set_tool(EditTool::Pan);
        }
        return;
    }
    const WalkView v = live->view();
    ImGui::Text("%zu points stored", v.points);
    wrapped(v.radio);
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
    help_marker(tr("The ray-tracing interpolation: the model of the walls and antennas fitted to the measured points of "
                "this floor, then the remaining differences spread over the floor. It runs in the background and is "
                "stored in the project with its settings. The map is always labelled \"interpolated\"."));
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
            ImGui::SameLine();
            const bool stale = c->interpolation->stale;
            if (tool_button(stale ? tr("Run again (stale)###interp.rerun") : tr("Run again###interp.rerun"), why.empty(),
                            tr("Compute the shown map again with the same metric and the current walls, antennas, points "
                            "and settings."),
                            why, stale))
                (void)state_.run_interpolation(true);
        }
    }
    if (c->interpolation && c->interpolation->stale) {
        std::string reasons;
        for (const std::string& r : c->interpolation->stale_reasons) reasons += (reasons.empty() ? "" : ", ") + r;
        wrapped(tr("Stale: ") + reasons + ". Run again to update it.", true);
    }
    if (c->interpolation && c->interpolation->estimated_sources)
        wrapped(tr("No placed antenna reaches this floor: the map uses estimated sources (dashed markers)."), true);
    if (ImGui::Button(tr("Settings...###interp.settings"))) (void)state_.edit_propagation();
    text_tooltip(tr("Grid size, reflections, path-loss exponent and the other ray-tracing settings of the project."));
}

// -- the plan: overlay and input -------------------------------------------------------------

void Gui::draw_plan_overlay(ImDrawList* dl) {
    overlay_ = ToolOverlay{overlay_.draft_corners, false, {}, false, overlay_.progress};
    overlay_.draft_corners = 0;
    const PlanCanvas& cv = *canvas_;
    const ImU32 edit_col = IM_COL32(255, 200, 0, 255);
    if (FloorEditor* ed = state_.floor_editor()) {
        // The wall being drawn, with the rubber band to the mouse.
        const auto& d = ed->draft();
        for (std::size_t i = 0; i + 1 < d.size(); ++i) dl->AddLine(cv.screen_of(d[i]), cv.screen_of(d[i + 1]), edit_col, 3.0F);
        if (!d.empty() && ed->rubber_band() && ed->tool() == EditTool::Wall)
            dl->AddLine(cv.screen_of(d.back()), cv.screen_of(*ed->rubber_band()), IM_COL32(255, 200, 0, 140), 2.0F);
        for (const Point2& p : d) {
            const ImVec2 s = cv.screen_of(p);
            dl->AddRectFilled(ImVec2(s.x - 4, s.y - 4), ImVec2(s.x + 4, s.y + 4), edit_col);
        }
        overlay_.draft_corners = d.size();
        // The selection (or what a drag would make of it).
        const Selection& sel = ed->selection();
        if (sel.kind == Selection::Kind::Wall) {
            const auto w = ed->drag_wall() ? ed->drag_wall() : ed->selected_wall();
            if (w) {
                for (std::size_t i = 0; i + 1 < w->points.size(); ++i)
                    dl->AddLine(cv.screen_of(w->points[i]), cv.screen_of(w->points[i + 1]), edit_col, 5.0F);
                for (std::size_t i = 0; i < w->points.size(); ++i) {
                    const ImVec2 s = cv.screen_of(w->points[i]);
                    const bool on = sel.vertex && *sel.vertex == i;
                    dl->AddRectFilled(ImVec2(s.x - 5, s.y - 5), ImVec2(s.x + 5, s.y + 5), on ? IM_COL32(255, 60, 0, 255) : IM_COL32(255, 255, 255, 255));
                    dl->AddRect(ImVec2(s.x - 5, s.y - 5), ImVec2(s.x + 5, s.y + 5), IM_COL32(0, 0, 0, 255));
                }
                overlay_.selection = true;
            }
        } else if (sel.kind == Selection::Kind::Antenna) {
            const auto a = ed->selected_antenna();
            if (a) {
                const ImVec2 s = cv.screen_of(ed->drag_antenna() ? *ed->drag_antenna() : a->position);
                dl->AddCircle(s, 15.0F, edit_col, 0, 3.0F);
                overlay_.selection = true;
            }
        }
    }
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
    FloorEditor* ed = state_.floor_editor();
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
    if (ed == nullptr) return;
    if (ed->wants_left_button()) {
        if (in.mouse) {
            if (in.double_clicked) ed->double_click(*in.mouse, in.tolerance);
            else if (in.pressed) ed->pointer_down(*in.mouse, in.tolerance);
            else if (in.dragging) ed->pointer_move(*in.mouse, in.tolerance);
            if (in.released) ed->pointer_up(*in.mouse, in.tolerance);
            if (!in.dragging && !in.released) ed->hover(in.hovered ? in.mouse : std::nullopt);
        } else {
            ed->hover(std::nullopt);
        }
    }
    if (!keys) return;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) (void)state_.show(ed->undo());
    else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        (void)state_.show(ed->redo());
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ed->cancel();
    else if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ed->draft().empty()) (void)state_.show(ed->finish_wall());
    else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) (void)ed->remove_last_corner();
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete) && ed->selection().kind != Selection::Kind::None)
        (void)state_.show(ed->delete_selected());
    else if (!ImGui::GetIO().KeyCtrl) {
        const struct {
            ImGuiKey key;
            EditTool tool;
        } tools[] = {{ImGuiKey_P, EditTool::Pan}, {ImGuiKey_S, EditTool::Select}, {ImGuiKey_W, EditTool::Wall}, {ImGuiKey_A, EditTool::Antenna}};
        for (const auto& t : tools)
            if (ImGui::IsKeyPressed(t.key, false)) (void)state_.show(ed->set_tool(t.tool));
    }
}

// -- dialogs ------------------------------------------------------------------------------------

void Gui::draw_antenna_dialog() {
    FloorEditor* ed = state_.floor_editor();
    if (ed == nullptr || !ed->selected_antenna()) {
        state_.cancel_dialog();
        return;
    }
    AntennaForm& f = ed->antenna_form();
    input_text(tr("Name###name"), f.name, 128);
    text_tooltip(tr("For example \"DAS 1, corridor\"."));
    input_text(tr("Model###model"), f.model, 128);
    text_tooltip(tr("The antenna type or product, free text (e.g. \"ceiling omni\")."));
    ImGui::Checkbox(tr("Outdoor donor / macro direction###outdoor"), &f.outdoor);
    text_tooltip(tr("An outdoor site seen from the building (its direction on the plan) instead of an indoor DAS antenna."));
    (void)input_double("Height (m)###height", f.height_m, 0.1, "%.2f", "Above the floor, 0 to 100 m.");
    (void)input_double("Gain (dBi)###gain", f.gain_dbi, 0.5, "%.1f", "The antenna gain in the main direction, -20 to 30 dBi.");
    ImGui::Checkbox(tr("Directional###directional"), &f.directional);
    text_tooltip(tr("A sector pattern with azimuth and beamwidth; else omnidirectional."));
    if (f.directional) {
        (void)input_double("Azimuth (deg)###azimuth", f.azimuth_deg, 5.0, "%.0f",
                           tr("The main direction in degrees clockwise from the top of the plan (the Aim tool sets it by a click)."));
        (void)input_double("Beamwidth (deg)###beamwidth", f.beamwidth_deg, 5.0, "%.0f", "Half-power beamwidth, up to 360 degrees.");
        (void)input_double("Front-to-back (dB)###front_to_back", f.front_to_back_db, 1.0, "%.0f", "The most the pattern takes off behind the antenna.");
    }
    if (f.outdoor) {
        bool has = f.direction_deg.has_value();
        if (ImGui::Checkbox(tr("Direction known###has_direction"), &has)) f.direction_deg = has ? std::optional<double>(0.0) : std::nullopt;
        text_tooltip(tr("The direction of the donor site from the building (the Aim tool sets it by a click)."));
        if (f.direction_deg) {
            double d = *f.direction_deg;
            if (input_double("Direction (deg)###direction", d, 5.0, "%.0f", "Degrees clockwise from the top of the plan."))
                f.direction_deg = d;
        }
    }
    ImGui::Checkbox(tr("EIRP known###eirp_known"), &f.eirp_known);
    text_tooltip(tr("When the EIRP is not known, the interpolation estimates it from the measured points (IND-42)."));
    if (f.eirp_known) (void)input_double("EIRP (dBm)###eirp", f.eirp_dbm, 0.5, "%.1f", "-30 to 70 dBm.");
    (void)input_double("Cable loss (dB)###cable_loss", f.cable_loss_db, 0.5, "%.1f", "Feeder loss, 0 to 60 dB.");
    const auto cells = ed->cells();
    std::string cell = f.cell.empty() ? std::string(tr("(not set)")) : f.cell;
    for (const CellChoice& c : cells)
        if (c.id == f.cell) cell = c.label;
    if (ImGui::BeginCombo(tr("Cell###cell"), cell.c_str())) {
        if (ImGui::Selectable(tr("(not set)###cell.none"), f.cell.empty())) f.cell.clear();
        for (const CellChoice& c : cells)
            if (ImGui::Selectable((c.label + "###cell." + c.id).c_str(), c.id == f.cell)) f.cell = c.id;
        ImGui::EndCombo();
    }
    text_tooltip(tr("The cell (LA and carrier) the antenna serves; the interpolation compares it with the measured carrier."));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0F);
    input_text("###new_cell_name", new_cell_name_, 128);
    text_tooltip(tr("The name of a new cell."));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0F);
    ImGui::InputDouble("MHz###new_cell_mhz", &new_cell_mhz_, 0.0, 0.0, "%.4f");
    text_tooltip(tr("Its main carrier in MHz (0: not known)."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Add cell###add_cell"))) {
        if (state_.show(ed->add_cell(new_cell_name_, new_cell_mhz_)).ok) new_cell_name_.clear();
    }
    text_tooltip(tr("Add a cell to the project and let this antenna serve it."));
    ImGui::Spacing();
    if (dialog_button(tr("Apply###dlg.ok"), tr("Store the antenna's properties"))) (void)state_.confirm_antenna();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close without storing [Esc]")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
    error_line(state_);
}

void Gui::draw_materials_dialog() {
    FloorEditor* ed = state_.floor_editor();
    if (ed == nullptr) {
        state_.cancel_dialog();
        return;
    }
    if (ImGui::BeginTable("###mat.table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Class"));
        ImGui::TableSetupColumn(tr("Attenuation"));
        ImGui::TableSetupColumn(tr("Walls"));
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (const MaterialRow& m : ed->materials()) {
            ImGui::PushID(m.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.bands.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu", m.walls);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton((tr("Edit###mat.edit.") + m.id).c_str())) material_editing_ = state_.show(ed->edit_material(m.id)).ok;
            text_tooltip(tr("Change the class's attenuation per frequency band."));
            ImGui::SameLine();
            const std::string why = m.walls > 0 ? std::to_string(m.walls) + tr(" walls use this class.") : std::string();
            if (tool_button(tr("Remove###mat.remove.") + m.id, why.empty(), tr("Remove the class from the table."), why))
                (void)state_.show(ed->remove_material(m.id));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(tr("New class###mat.new"))) material_editing_ = state_.show(ed->edit_material("")).ok;
    text_tooltip(tr("Add a wall class (material) with its own attenuation."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset defaults###mat.reset"))) (void)state_.show(ed->reset_materials());
    text_tooltip(tr("Put the default classes back to their default values; your own classes stay."));
    if (material_editing_) {
        MaterialForm& f = ed->material_form();
        ImGui::SeparatorText(f.is_new ? tr("New class") : tr("Edit class"));
        ImGui::BeginDisabled(!f.is_new);
        input_text(tr("Id###mat.id"), f.id, 64);
        ImGui::EndDisabled();
        text_tooltip(tr("The key walls use (letters, digits, \"_\"); it cannot change once the class exists."));
        input_text(tr("Name###mat.name"), f.name, 128);
        text_tooltip(tr("The name shown in the lists."));
        for (std::size_t i = 0; i < f.bands.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const std::string n = std::to_string(i);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0F);
            ImGui::InputDouble(("MHz###mat.band." + n + ".mhz").c_str(), &f.bands[i].frequency_mhz, 0.0, 0.0, "%.0f");
            text_tooltip(tr("The band's frequency; between bands the attenuation is interpolated."));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0F);
            ImGui::InputDouble(("dB###mat.band." + n + ".db").c_str(), &f.bands[i].attenuation_db, 0.5, 5.0, "%.1f");
            text_tooltip(tr("The attenuation of one wall of this class at that frequency (0 to 100 dB)."));
            ImGui::SameLine();
            ImGui::BeginDisabled(f.bands.size() <= 1);
            if (ImGui::SmallButton((tr("Remove###mat.band.") + n + ".remove").c_str())) {
                f.bands.erase(f.bands.begin() + static_cast<std::ptrdiff_t>(i));
                ImGui::EndDisabled();
                ImGui::PopID();
                break;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (ImGui::SmallButton(tr("Add band###mat.band_add")))
            f.bands.push_back({f.bands.empty() ? 400.0 : f.bands.back().frequency_mhz * 2.0, f.bands.empty() ? 10.0 : f.bands.back().attenuation_db});
        text_tooltip(tr("Add a frequency band."));
        if (dialog_button(tr("Apply###mat.apply"), tr("Store the class; walls of this class use it at once"))) {
            if (state_.show(ed->apply_material()).ok) material_editing_ = false;
        }
    }
    ImGui::Spacing();
    if (dialog_button(tr("Close###dlg.cancel"), tr("Close the table [Esc]")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        material_editing_ = false;
        state_.cancel_dialog();
    }
    error_line(state_);
}

void Gui::draw_propagation_dialog() {
    InterpolationRunner* run = state_.interpolation();
    if (run == nullptr) {
        state_.cancel_dialog();
        return;
    }
    PropagationForm& f = run->settings_form();
    project::PropagationSettings& s = f.settings;
    (void)input_double("Grid (m)###grid", s.grid_m, 0.05, "%.2f", "The size of a map cell, 0.05 to 10 m (default 0.25 m).");
    ImGui::InputInt(tr("Reflections###reflections"), &s.max_reflections);
    text_tooltip(tr("Specular reflections per ray, 0 to 3 (default 2); more is slower."));
    (void)input_double("Path-loss exponent###exponent", s.path_loss_exponent, 0.1, "%.2f",
                       tr("The starting value of the fit, 1 to 8 (2 = free space)."));
    (void)input_double("Reference distance (m)###ref_distance", s.reference_distance_m, 0.1, "%.2f",
                       tr("Where the exponent starts to apply, 0.01 to 100 m."));
    (void)input_double("Reflection loss (dB)###reflection_loss", s.reflection_loss_db, 0.5, "%.1f",
                       tr("Loss at a reflection when the wall class has none, 0 to 60 dB."));
    (void)input_double("Receiver height (m)###rx_height", s.rx_height_m, 0.1, "%.2f", "The height of the measured radio, 0 to 10 m.");
    (void)input_double("Default EIRP (dBm)###default_eirp", s.default_eirp_dbm, 0.5, "%.1f",
                       tr("The starting EIRP of an antenna without one, -50 to 80 dBm."));
    ImGui::Checkbox(tr("Antennas on other floors###other_floors"), &s.other_floors);
    text_tooltip(tr("Let antennas on other floors contribute through the floors' slabs (IND-44)."));
    (void)input_double("Attenuation per floor (dB)###floor_attenuation", f.floor_attenuation_db, 1.0, "%.1f",
                       tr("The slab attenuation between two floors, 0 to 100 dB (a floor may have its own)."));
    ImGui::Spacing();
    if (dialog_button(tr("Apply###dlg.ok"), tr("Store the settings; maps made with others become stale"))) (void)state_.confirm_propagation();
    ImGui::SameLine();
    if (dialog_button(tr("Defaults###prop.defaults"), tr("Put the default values in the form (stored with Apply)"))) run->default_settings();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close without storing [Esc]")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
    error_line(state_);
}

}  // namespace yantra::app
