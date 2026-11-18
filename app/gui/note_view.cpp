// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-48: the notes on the plan in the project window: the pins (drawn
// over the plan in the Walk and Map tabs while the Show row's "Notes" is
// on), a click on a pin opens it, a drag moves it, a long press elsewhere
// opens a new one; and the Note window (picture, text, audio; Save,
// Delete with a question, Cancel). The model is plan_notes.h.
//
// Ids: the window "dlg.note"; "note.text", "note.picture_open",
// "note.picture_take", "note.picture_remove", "note.audio_record",
// "note.audio_stop", "note.audio_play", "note.audio_stop_play",
// "note.audio_delete", "note.delete", "note.delete_confirm",
// "note.delete_keep", "dlg.ok" (Save), "dlg.cancel" (docs/APP.md).
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "gui.h"
#include "gui_widgets.h"
#include "yantra/app/i18n.h"
#include "yantra/core/about/photo.h"

namespace yantra::app {

using namespace gui_detail;

namespace {

bool note_multiline(const char* label, std::string& s, float height, std::size_t capacity = 16384) {
    std::string buf = s;
    buf.resize(std::max(capacity, s.size() + 1), '\0');
    const bool changed = ImGui::InputTextMultiline(label, buf.data(), buf.size(), ImVec2(-1.0F, height));
    if (changed) s = std::string(buf.c_str());
    return changed;
}

std::string seconds_text(double s) { return std::to_string(static_cast<long long>(std::lround(s))); }

}  // namespace

bool Gui::grab_note(Point2 at, double tolerance) {
    note_press_ = 0;
    note_dragged_ = false;
    PlanNotes* n = state_.notes();
    ProjectWindow* w = state_.project_window();
    // While a walk test runs, a click on the plan is the walk's (pins are
    // shown but not taken).
    const bool walking = state_.live() != nullptr && state_.live()->running();
    if (n == nullptr || w == nullptr || walking || !state_.settings().layers.notes) return false;
    if (const auto id = n->pin_at(w->selected(), at, tolerance)) {
        note_press_ = *id;
        return true;
    }
    return false;
}

void Gui::handle_note_input() {
    const CanvasInput& in = canvas_->input();
    if (in.long_pressed && in.press) {
        // A long press on a pin opens it; elsewhere a new note there.
        if (in.grabbed && note_press_ != 0)
            (void)state_.show(state_.open_note(note_press_));
        else
            (void)state_.show(state_.open_note_at(*in.press));
        note_press_ = 0;
        return;
    }
    if (note_press_ == 0) return;
    if (in.press_dragging && in.mouse) {
        note_dragged_ = true;
        note_drag_pos_ = *in.mouse;
        notes_.dragging = note_press_;
        return;
    }
    if (in.clicked && in.grabbed) {
        (void)state_.show(state_.open_note(note_press_));
        note_press_ = 0;
        return;
    }
    if (note_dragged_ && !in.press_dragging) {
        // Released after a drag: the pin goes there.
        (void)state_.show(state_.move_note(note_press_, note_drag_pos_));
        note_press_ = 0;
        note_dragged_ = false;
    }
}

void Gui::draw_note_pins(ImDrawList* dl) {
    PlanNotes* n = state_.notes();
    ProjectWindow* w = state_.project_window();
    if (n == nullptr || w == nullptr || !state_.settings().layers.notes) return;
    const ImU32 fill = IM_COL32(250, 200, 40, 255);
    const ImU32 edge = IM_COL32(40, 30, 0, 255);
    const float r = std::max(em(10.0F), layout().touch ? em(14.0F) : 0.0F);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const NotePin* hovered = nullptr;
    for (const NotePin& p : n->pins(w->selected())) {
        const bool dragged = note_dragged_ && note_press_ == p.id;
        const ImVec2 c = canvas_->screen_of(dragged ? note_drag_pos_ : p.plan);
        // A pin: a round head over a point, with its number.
        const ImVec2 tip(c.x, c.y);
        const ImVec2 head(c.x, c.y - r * 1.6F);
        dl->AddTriangleFilled(ImVec2(head.x - r * 0.6F, head.y + r * 0.5F), ImVec2(head.x + r * 0.6F, head.y + r * 0.5F), tip,
                              edge);
        dl->AddCircleFilled(head, r, fill);
        dl->AddCircle(head, r, edge, 0, dragged ? 3.0F : 1.5F);
        const std::string num = std::to_string(p.number);
        const ImVec2 ts = ImGui::CalcTextSize(num.c_str());
        dl->AddText(ImVec2(head.x - ts.x * 0.5F, head.y - ts.y * 0.5F), edge, num.c_str());
        if (std::hypot(mouse.x - head.x, mouse.y - head.y) <= r || std::hypot(mouse.x - tip.x, mouse.y - tip.y) <= r)
            hovered = &p;
        ++notes_.pins;
    }
    if (hovered != nullptr && canvas_->input().hovered && !help_shown_on_purpose() && !note_dragged_)
        ImGui::SetTooltip("%s\n%s", hovered->label.c_str(), tr("Click to open, drag to move."));
}

void Gui::draw_note_dialog() {
    PlanNotes* notes = state_.notes();
    NoteForm* f = notes != nullptr ? notes->form() : nullptr;
    if (f == nullptr) {
        state_.cancel_dialog();
        return;
    }
    notes_.window = true;
    notes_.window_pos = ImGui::GetWindowPos();
    note_window_size_ = ImGui::GetWindowSize();
    if (f->id == 0)
        ImGui::TextUnformatted(trf("New note {} on the plan", {std::to_string(f->number)}).c_str());
    else
        ImGui::TextUnformatted(trf("Note {} on the plan", {std::to_string(f->number)}).c_str());
    help_marker(tr("A note on this spot of the plan: a picture, a text and an audio fragment, each optional. It is "
                   "stored in the project and listed in the report. A long click (long press) on the plan adds a note; "
                   "a click on a pin opens it, a drag moves it."));

    // Text.
    ImGui::SeparatorText(tr("Text"));
    const float line = ImGui::GetTextLineHeight();
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    note_multiline("###note.text", f->text, line * 5.0F + ImGui::GetStyle().FramePadding.y * 2.0F);
    text_tooltip(tr("What there is to note at this spot (several lines)."));

    // Picture.
    ImGui::SeparatorText(tr("Picture"));
    if (ImGui::Button(tr("Open picture...###note.picture_open"))) state_.note_picture_file();
    text_tooltip(tr("Add a picture from a file (JPEG, PNG, ...)."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Take picture...###note.picture_take"))) (void)state_.show(state_.note_picture_camera());
    text_tooltip(tr("Take the picture with a camera of this computer (a webcam or a connected camera)."));
    if (!f->picture.bytes.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Remove picture###note.picture_remove"))) notes->remove_picture();
        text_tooltip(tr("Remove the picture from the note."));
    }
    if (note_tex_bytes_ != f->picture.bytes.data() || note_tex_size_ != f->picture.bytes.size()) {
        note_tex_bytes_ = f->picture.bytes.data();
        note_tex_size_ = f->picture.bytes.size();
        note_tex_.reset();
        if (!f->picture.bytes.empty())
            if (const auto r = about::decode_photo(f->picture.bytes, 512)) note_tex_.set(r->width, r->height, r->rgba.data());
    }
    if (!f->picture.bytes.empty() && note_tex_.valid()) {
        const float w = std::min(ImGui::GetContentRegionAvail().x, em(320.0F));
        const float scale = std::min(w / static_cast<float>(note_tex_.width()), em(200.0F) / static_cast<float>(note_tex_.height()));
        ImGui::Image(note_tex_.ref(), ImVec2(static_cast<float>(note_tex_.width()) * scale,
                                             static_cast<float>(note_tex_.height()) * scale));
        text_tooltip(f->picture.source == "camera" ? tr("Taken with a camera.") : f->picture.name.c_str());
        notes_.picture = true;
    } else if (!f->picture.bytes.empty()) {
        ImGui::TextDisabled("%s", tr("The picture cannot be shown."));
    } else {
        ImGui::TextDisabled("%s", tr("No picture."));
    }

    // Audio.
    ImGui::SeparatorText(tr("Audio"));
    const std::string max_s = seconds_text(notes->max_seconds());
    if (notes->recording()) {
        if (ImGui::Button(tr("Stop###note.audio_stop"))) (void)state_.show(notes->stop_recording());
        text_tooltip(tr("Stop recording."));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95F, 0.3F, 0.25F, 1.0F), "%s",
                           trf("Recording: {} s of at most {} s", {seconds_text(notes->recorded_seconds()), max_s}).c_str());
    } else {
        const std::string problem = notes->capture_problem();
        ImGui::BeginDisabled(!problem.empty());
        if (ImGui::Button(tr("Record###note.audio_record"))) (void)state_.show(notes->record());
        ImGui::EndDisabled();
        text_tooltip(problem.empty() ? trf("Record an audio fragment with the microphone (at most {} s; File > Settings > "
                                           "Survey). It replaces the note's audio.",
                                           {max_s})
                                           .c_str()
                                     : problem.c_str());
        if (!f->audio.samples.empty()) {
            ImGui::SameLine();
            if (notes->playing()) {
                if (ImGui::Button(tr("Stop playing###note.audio_stop_play"))) notes->stop_playback();
                text_tooltip(tr("Stop the playback."));
            } else {
                if (ImGui::Button(tr("Play###note.audio_play"))) (void)state_.show(notes->play());
                text_tooltip(tr("Play the audio fragment."));
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Delete audio###note.audio_delete"))) notes->delete_audio();
            text_tooltip(tr("Remove the audio fragment from the note."));
            ImGui::TextDisabled("%s", PlanNotes::audio_label(f->audio.seconds()).c_str());
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr("No audio."));
        }
        if (!problem.empty()) {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextDisabled("%s", problem.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    // Save, Delete (asks), Cancel.
    ImGui::Spacing();
    if (f->confirm_delete) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextUnformatted(f->id == 0 ? tr("Discard this new note?") : tr("Delete this note with its picture and audio?"));
        ImGui::PopTextWrapPos();
        if (dialog_button(tr("Delete###note.delete_confirm"), tr("Delete the note from the project")))
            (void)state_.note_delete();
        ImGui::SameLine();
        if (dialog_button(tr("Keep it###note.delete_keep"), tr("Do not delete the note [Esc]")) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape))
            f->confirm_delete = false;
        error_line(state_);
        return;
    }
    if (dialog_button(tr("Save###dlg.ok"), tr("Store the note in the project"))) (void)state_.note_save();
    ImGui::SameLine();
    if (dialog_button(tr("Delete...###note.delete"), f->id == 0 ? tr("Discard the new note (asks first)")
                                                                  : tr("Delete the note (asks first)")))
        (void)state_.note_delete();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close without storing the changes [Esc]")) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape) && !notes->recording()))
        state_.cancel_dialog();
    error_line(state_);
}

}  // namespace yantra::app
