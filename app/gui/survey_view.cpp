// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The survey panels, recording and replay controls and the status bar's
// survey items (step 9.5; docs/APP.md "Survey panels, recording and
// replay"): UI-01 large values, UI-02 serving cell, UI-03 neighbours,
// UI-04 graphs with a cursor and threshold lines, the scanner view, the
// event list, UI-11 statistics, the NFR-07 warning, UI-08 status items;
// REC-01..03, REC-05..09, REC-11 controls. Drawing only: the models are
// SurveyMonitor, LiveSurvey and ReplaySession (app/model). The graphs
// are Yantra's own (ImGui draw lists, UI-13), technology-neutral: the
// field labels come from the technology schemas.
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

const ImVec4 kWarnColour{1.0F, 0.75F, 0.25F, 1.0F};
const ImVec4 kRecordColour{1.0F, 0.30F, 0.30F, 1.0F};
const ImVec4 kListedOnlyColour{0.60F, 0.60F, 0.65F, 1.0F};
const ImVec4 kMeasuredOnlyColour{0.55F, 0.80F, 1.0F, 1.0F};

std::string level_text(double level, bool calibrated) {
    if (std::isnan(level)) return "-";
    char b[32];
    std::snprintf(b, sizeof b, "%.1f %s", level, calibrated ? "dBm" : "dBFS");
    return b;
}

std::string mhz(std::uint64_t hz) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4f MHz", static_cast<double>(hz) / 1e6);
    return b;
}

std::string clock_text(std::int64_t ns) {
    const std::int64_t s = std::max<std::int64_t>(0, ns / 1'000'000'000);
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", static_cast<long long>(s / 3600),
                                 static_cast<long long>(s / 60 % 60), static_cast<long long>(s % 60));
    else std::snprintf(b, sizeof b, "%lld:%02lld", static_cast<long long>(s / 60), static_cast<long long>(s % 60));
    return b;
}

std::string cell_label(const CellRow& r) {
    std::string s = r.name.empty() ? (r.identity.empty() ? mhz(r.frequency_hz) : r.identity) : r.name;
    if (!r.channel.empty()) s += "  ch " + r.channel;
    return s;
}

ImU32 im_colour(std::uint32_t argb) {
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

bool light_theme() {
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    return bg.x + bg.y + bg.z > 1.5F;
}

/// A disabled-aware button with a tooltip that says why it is disabled.
bool tool_button(const char* label, const std::string& why_not, const char* tooltip) {
    ImGui::BeginDisabled(!why_not.empty());
    const bool pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    text_tooltip(why_not.empty() ? tooltip : (std::string(tooltip) + " " + why_not).c_str());
    return pressed;
}

}  // namespace

// -- the side panel's Survey tab ----------------------------------------------------------

void Gui::draw_survey_section() {
    LiveSurvey* live = state_.live();
    ReplaySession* replay = state_.replay();
    if (live == nullptr || replay == nullptr) return;
    const bool replaying = replay->is_open();

    ImGui::SeparatorText(tr("Radio"));
    const RadioState rs = live->radio_state();
    if (rs.lost) ImGui::TextColored(kWarnColour, "%s", rs.text.c_str());
    else ImGui::TextUnformatted(rs.text.c_str());
    if (!live->radios().empty() || live->radio_running()) {
        if (live->radio_running()) {
            const std::string why = live->running() ? tr("Stop the walk test first.")
                                    : live->recording() ? tr("Stop the recording first.") : "";
            if (tool_button(tr("Stop radio###survey.radio_stop"), why, tr("Stop measuring.")))
                (void)state_.show(live->stop_radio());
        } else {
            const std::string why = replaying ? tr("Close the replay first.") : live->chosen() ? "" : tr("Choose a radio (Walk tab).");
            if (tool_button(tr("Start radio###survey.radio_start"), why,
                            tr("Measure with the chosen radio without a walk test (monitoring): the survey panels fill; "
                            "a recording can start.")))
                (void)state_.show(live->start_radio());
        }
    } else {
        const std::string why = replaying ? tr("Close the replay first.") : "";
        if (tool_button(tr("Find radios###survey.find_radios"), why, tr("Look for the radios (the Walk tab chooses one).")))
            (void)state_.show(live->find_radios());
    }

    draw_recording_controls(*live);
    draw_replay_controls();

    ImGui::SeparatorText(tr("Coverage"));
    double th = state_.monitor()->stats().threshold_dbm;
    ImGui::SetNextItemWidth(std::min(em(110.0F), ImGui::GetContentRegionAvail().x * 0.55F));
    if (ImGui::InputDouble(tr("dBm###survey.threshold"), &th, 1.0, 5.0, "%.0f") && th >= kMinThresholdDbm && th <= kMaxThresholdDbm)
        (void)state_.show(state_.set_coverage_threshold(th));
    text_tooltip(tr("The serving level that counts as covered in the statistics (UI-11); kept in the settings."));
}

void Gui::draw_recording_controls(LiveSurvey& live) {
    ImGui::SeparatorText(tr("Recording"));
    RecordingForm& f = live.recording_form();
    const recording::RecorderStatus st = live.recording_status();
    if (live.recording()) {
        const bool paused = st.state == recording::RecorderState::Paused;
        ImGui::TextColored(paused ? kWarnColour : kRecordColour, "%s %s, file %u", paused ? tr("Paused:") : tr("Recording:"),
                           st.name.c_str(), st.file_index);
        ImGui::TextDisabled(tr("%llu messages in the file, %u markers, %u voice notes"),
                            static_cast<unsigned long long>(st.messages_in_file), st.next_marker - 1, st.voice_notes);
        if (!st.error.empty()) ImGui::TextColored(kErrorColour, "%s", st.error.c_str());
        const std::string why_stop = live.running() ? tr("The walk test records into it: stop the walk test.") : "";
        if (tool_button(tr("Stop recording###rec.stop"), why_stop, tr("Close the recording; its files are listed in the project.")))
            (void)state_.show(live.stop_recording());
        ImGui::SetNextItemWidth(-1.0F);
        input_text_hint("###rec.marker_label", tr("Marker label (optional)"), marker_label_, 128);
        text_tooltip(tr("The marker's label (optional)."));
        const std::string why_mark = paused ? tr("The recording is paused.") : "";
        if (tool_button(tr("Marker###rec.marker"), why_mark, tr("A numbered marker at this instant (REC-05)."))) {
            (void)state_.show(live.marker(marker_label_));
            marker_label_.clear();
        }
        ImGui::SameLine();
        if (tool_button(tr("New file###rec.new_file"), why_mark, tr("Marker and continue in a new file.")))
            (void)state_.show(live.marker(marker_label_, recording::MarkerAction::NewFile));
        ImGui::SameLine();
        if (tool_button(tr("New file + pause###rec.new_file_pause"), why_mark,
                        tr("Marker, close the file and pause; Resume continues in a new file.")))
            (void)state_.show(live.marker(marker_label_, recording::MarkerAction::NewFilePause));
        if (paused && ImGui::Button(tr("Resume###rec.resume"))) (void)state_.show(live.resume_recording());
        // Voice notes (REC-06).
        if (live.voice_recording()) {
            char b[64];
            std::snprintf(b, sizeof b, tr("Stop voice note (%.0f s)###rec.voice_stop"), live.voice_seconds());
            if (ImGui::Button(b)) (void)state_.show(live.voice_stop(marker_label_));
            text_tooltip(tr("Store the voice note with the label above (optional)."));
        } else if (tool_button(tr("Voice note###rec.voice"), live.voice_reason(),
                               tr("Speak a note into the recording at this instant (REC-06)."))) {
            (void)state_.show(live.voice_start());
        }
    } else {
        ImGui::SetNextItemWidth(-1.0F);
        input_text_hint("###rec.name", tr("Recording name (optional)"), f.name, 128);
        text_tooltip(tr("The recording's name (empty: a quick-record name with date and time)."));
        ImGui::SetNextItemWidth(-1.0F);
        input_text_hint("###rec.notes", tr("Notes (optional)"), f.notes, 512);
        text_tooltip(tr("Notes for the recording's header (no personal data)."));
        ImGui::SetNextItemWidth(std::min(em(110.0F), ImGui::GetContentRegionAvail().x * 0.55F));
        ImGui::InputDouble(tr("min split###rec.split"), &f.split_minutes, 1.0, 5.0, "%.0f");
        text_tooltip(tr("A new file every so many minutes (0 = one file)."));
        same_line_if(switch_w(tr("zstd###rec.compress")));
        ImGui::Checkbox(tr("zstd###rec.compress"), &f.compress);
        text_tooltip(tr("Compress the files (zstd)."));
        const std::string why = live.record_reason();
        if (tool_button(tr("Record###rec.record"), why, tr("Record the measurements into the project's recordings folder.")))
            (void)state_.show(live.record(false));
        ImGui::SameLine();
        if (tool_button(tr("Quick record###rec.quick"), why, tr("Record at once under a name with the date and time.")))
            (void)state_.show(live.record(true));
    }
    // The ring buffer (REC-09).
    bool ring = f.ring;
    if (ImGui::Checkbox(tr("Ring buffer###rec.ring"), &ring)) {
        f.ring = ring;
        (void)state_.show(live.apply_ring());
    }
    text_tooltip(tr("Keep the last minutes while the radio runs, without a recording; save them when something "
                 "happened (REC-09)."));
    {
        // The minutes field after the switch when it fits there, else below it.
        const float label = ImGui::CalcTextSize(tr("min###rec.ring_minutes"), nullptr, true).x + ImGui::GetStyle().ItemInnerSpacing.x;
        same_line_if(em(90.0F) + label);
        ImGui::SetNextItemWidth(std::min(em(90.0F), ImGui::GetContentRegionAvail().x - label));
    }
    ImGui::BeginDisabled(f.ring);
    ImGui::InputDouble(tr("min###rec.ring_minutes"), &f.ring_minutes, 1.0, 5.0, "%.0f");
    ImGui::EndDisabled();
    text_tooltip(f.ring ? tr("The minutes the ring buffer keeps. Switch the ring buffer off to change them.")
                        : tr("The minutes the ring buffer keeps."));
    const auto files = live.ring_files();
    if (!files.empty()) {
        ImGui::SetNextItemWidth(-1.0F);
        input_text_hint("###rec.ring_name", tr("Name of the saved recording"), ring_name_, 128);
        text_tooltip(tr("The name of the saved recording."));
        const std::string why = ring_name_.empty() ? tr("Type a name first.") : "";
        if (tool_button(tr("Save ring buffer###rec.ring_save"), why, tr("Save the kept minutes as a recording of the project."))) {
            (void)state_.show(live.save_ring(ring_name_));
            ring_name_.clear();
        }
    }
}

void Gui::draw_replay_controls() {
    ReplaySession* replay = state_.replay();
    LiveSurvey* live = state_.live();
    ImGui::SeparatorText(tr("Replay"));
    if (!replay->is_open()) {
        const auto list = replay->recordings();
        if (list.empty()) {
            ImGui::TextDisabled(tr("No recordings in this project's folder yet."));
            return;
        }
        if (replay_pick_ >= static_cast<int>(list.size())) replay_pick_ = -1;
        ImGui::BeginChild("###replay.list", ImVec2(0.0F, std::min(6.0F, static_cast<float>(list.size())) *
                                                             ImGui::GetTextLineHeightWithSpacing() + 8.0F),
                          ImGuiChildFlags_Borders);
        for (std::size_t i = 0; i < list.size(); ++i) {
            char label[256];
            std::snprintf(label, sizeof label, "%s (%zu)###replay.item.%zu", list[i].name.c_str(), list[i].files.size(), i);
            if (ImGui::Selectable(label, replay_pick_ == static_cast<int>(i), 0, ImVec2(0.0F, row_h())))
                replay_pick_ = static_cast<int>(i);
            text_tooltip(tr("Choose this recording (its number of files in brackets)."));
        }
        ImGui::EndChild();
        const std::string why = replay_pick_ < 0 ? tr("Choose a recording first.")
                                : live->running() ? tr("Stop the walk test first.")
                                : live->recording() ? tr("Stop the recording first.") : "";
        if (tool_button(tr("Open###replay.open"), why, tr("Replay the recording into the survey panels (the radio stops)."))) {
            const Result r = state_.open_replay(list[static_cast<std::size_t>(replay_pick_)]);
            if (r.ok) panels_ = true;
        }
        return;
    }
    const ReplayView v = replay->view();
    ImGui::Text("%s  %s / %s", v.name.c_str(), clock_text(v.position_ns).c_str(), clock_text(v.duration_ns).c_str());
    using St = recording::ReplayPlayer::State;
    if (v.state == St::Playing) {
        if (ImGui::Button(tr("Pause###replay.pause"))) (void)state_.show(replay->pause());
        text_tooltip(tr("Pause the replay at the cursor."));
    } else {
        if (ImGui::Button(tr("Play###replay.play"))) (void)state_.show(replay->play(replay_speed_));
        text_tooltip(tr("Play the recording from the cursor at the speed set."));
    }
    same_line_if(label_w(tr("Stop###replay.stop")));
    if (ImGui::Button(tr("Stop###replay.stop"))) (void)state_.show(replay->stop());
    text_tooltip(tr("Stop the replay and go back to the start."));
    same_line_if(label_w(tr("Again###replay.again")));
    if (ImGui::Button(tr("Again###replay.again"))) (void)state_.show(replay->play_again());
    text_tooltip(tr("Play again from the start."));
    same_line_if(em(80.0F) + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("x").x);
    ImGui::SetNextItemWidth(em(80.0F));
    ImGui::InputDouble("x###replay.speed", &replay_speed_, 0.0, 0.0, "%.1f");
    text_tooltip(tr("The speed: 1 = real time, up to 100; 0 = as fast as possible."));
    std::int64_t pos = v.position_ns;
    const std::int64_t lo = 0, hi = std::max<std::int64_t>(1, v.duration_ns);
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::SliderScalar("###replay.position", ImGuiDataType_S64, &pos, &lo, &hi, ""))
        (void)state_.show(replay->seek(pos));
    for (const auto& [t, label] : v.markers) {
        char b[160];
        std::snprintf(b, sizeof b, "%s  %s###replay.marker.%lld", clock_text(t).c_str(), label.c_str(), static_cast<long long>(t));
        if (ImGui::SmallButton(b)) (void)state_.show(replay->seek(t));
    }
    // REC-08: cut at the cursor.
    ImGui::SetNextItemWidth(-1.0F);
    input_text("###replay.cut_name", cut_name_, 128);
    text_tooltip(tr("The name of the cut recording."));
    const std::string why = cut_name_.empty() ? tr("Type a name first.") : "";
    if (tool_button(tr("Cut: start to cursor###replay.cut_head"), why, tr("Save the part before the cursor as a new recording (REC-08).")))
        (void)state_.show(replay->cut(true, cut_name_));
    same_line_if(label_w(tr("Cut: cursor to end###replay.cut_tail")));
    if (tool_button(tr("Cut: cursor to end###replay.cut_tail"), why, tr("Save the part after the cursor as a new recording (REC-08).")))
        (void)state_.show(replay->cut(false, cut_name_));
    if (ImGui::Button(tr("Close replay###replay.close"))) (void)state_.close_replay();
    text_tooltip(tr("Close the recording and go back to the live view."));
}

// -- the panels under the plan -------------------------------------------------------------

void Gui::draw_survey_panels(float height) {
    survey_.panels = true;
    const auto& monitor = state_.monitor();
    ImGui::BeginChild("###survey", ImVec2(0.0F, height), ImGuiChildFlags_Borders);
    wrap_texts();
    // NFR-07: the self-interference warning with its advice.
    const measure::SelfInterferenceWarning w = monitor->interference();
    if (w.active) {
        survey_.interference_banner = true;
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(kWarnColour, tr("Self-interference: %s"), w.text.c_str());
        ImGui::PopTextWrapPos();
    }
    const SurveyFrame f = monitor->frame();
    if (ImGui::BeginTabBar("###survey.tabs")) {
        if (tab_item(tr("Cells###survey.cells"), tr("The serving cell and its neighbours, with the large values."))) {
            survey_.tab = "cells";
            draw_cells_panel(f);
            ImGui::EndTabItem();
        }
        if (tab_item(tr("Graphs###survey.graphs"), tr("Level, C1 and quality over time."))) {
            survey_.tab = "graphs";
            draw_graphs_panel();
            ImGui::EndTabItem();
        }
        if (tab_item(tr("Scanner###survey.scanner"), tr("Every carrier the radio sees."))) {
            survey_.tab = "scanner";
            draw_scanner_panel(f);
            ImGui::EndTabItem();
        }
        if (tab_item(tr("Events###survey.events"), tr("The events of the walk: cell changes, alerts and markers."))) {
            survey_.tab = "events";
            draw_events_panel();
            ImGui::EndTabItem();
        }
        if (tab_item(tr("Statistics###survey.stats"), tr("The statistics of the points measured so far."))) {
            survey_.tab = "stats";
            draw_stats_panel();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

void Gui::draw_cells_panel(const SurveyFrame& f) {
    if (!f.valid) {
        ImGui::TextDisabled(tr("No measurements yet: start the radio, a walk test or a replay (Survey tab)."));
        return;
    }
    if (f.at_cursor) {
        ImGui::TextColored(kWarnColour, tr("At the graph cursor"));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Live###cells.live"))) state_.monitor()->set_cursor(std::nullopt);
    }
    // UI-01: the large values.
    const ImVec2 start = ImGui::GetCursorPos();
    const float row_w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0F);
    if (f.serving) {
        const std::string lv = level_text(f.serving->level, f.serving->calibrated);
        ImGui::TextUnformatted(lv.c_str());
        survey_.large.push_back(tr("Level ") + lv);
    } else {
        ImGui::TextDisabled(tr("No serving cell"));
    }
    ImGui::PopFont();
    if (f.serving) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3F);
        std::string line;
        if (f.serving->c1) {
            char b[32];
            std::snprintf(b, sizeof b, tr("C1 %.1f dB%s"), *f.serving->c1, f.serving->c1_estimated ? tr(" (est.)") : "");
            line += b;
        }
        if (f.serving->quality) {
            char b[64];
            std::snprintf(b, sizeof b, "%s%s %.1f %s", line.empty() ? "" : "   ", f.serving->quality->field.c_str(),
                          f.serving->quality->value, f.serving->quality->unit.c_str());
            line += b;
        }
        if (!line.empty()) {
            ImGui::TextUnformatted(line.c_str());
            survey_.large.push_back(line);
        }
        for (const FieldText& t : f.serving->large) {
            ImGui::Text("%s %s", t.label.c_str(), t.value.c_str());
            ImGui::SameLine();
            survey_.large.push_back(t.label + " " + t.value);
        }
        ImGui::NewLine();
        ImGui::PopFont();
    }
    ImGui::TextDisabled(tr("%llu samples"), static_cast<unsigned long long>(f.samples));
    ImGui::EndGroup();
    // Right of the large values, however wide they are at the font size and
    // scale (UI-13): the serving cell never runs over them. When the two
    // lists have no room there (a narrow window, a large font), they go
    // under the values, and under each other when the row is narrow too;
    // the panel scrolls (TST-52).
    const float min_list = em(220.0F);
    const float beside_x = start.x + std::max(gui_detail::px(260.0F), ImGui::GetItemRectSize().x + gui_detail::px(20.0F));
    const bool beside = row_w - (beside_x - start.x) >= 2.0F * min_list + ImGui::GetStyle().ItemSpacing.x;
    const bool lists_side_by_side = beside || row_w >= 2.0F * min_list + ImGui::GetStyle().ItemSpacing.x;
    if (beside) ImGui::SameLine(beside_x);
    const ImGuiChildFlags list_flags = beside ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY;

    // UI-02: the serving cell and its broadcast parameters.
    ImGui::BeginChild("###cells.serving",
                      ImVec2(lists_side_by_side ? std::max(min_list, ImGui::GetContentRegionAvail().x * 0.4F) : 0.0F, 0.0F),
                      list_flags);
    wrap_texts();
    if (f.serving) {
        survey_.serving = cell_label(*f.serving) + "  " + mhz(f.serving->frequency_hz);
        ImGui::SeparatorText(tr("Serving cell"));
        ImGui::TextUnformatted(survey_.serving.c_str());
        if (!f.serving->identity.empty()) ImGui::TextDisabled("%s", f.serving->identity.c_str());
        if (!f.serving->flags_text.empty()) ImGui::TextColored(kWarnColour, "%s", f.serving->flags_text.c_str());
        if (ImGui::BeginTable("###cells.params", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            for (const FieldText& p : f.serving->parameters) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p.label.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p.value.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    if (lists_side_by_side) ImGui::SameLine();
    // UI-03: neighbours; listed and received, received only, listed only.
    ImGui::BeginChild("###cells.neighbours", ImVec2(0.0F, 0.0F), list_flags);
    wrap_texts();
    ImGui::SeparatorText(tr("Neighbours"));
    ImGui::TextColored(kMeasuredOnlyColour, tr("received, not listed"));
    same_line_if(ImGui::CalcTextSize(tr("  listed, not received")).x);
    ImGui::TextColored(kListedOnlyColour, tr("  listed, not received"));
    if (ImGui::BeginTable("###cells.neighbour_table", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Cell"));
        ImGui::TableSetupColumn(tr("Frequency"));
        ImGui::TableSetupColumn(tr("Level"));
        ImGui::TableSetupColumn("C1");
        headers_row();
        for (const CellRow& r : f.neighbours) {
            ++survey_.neighbour_rows;
            if (r.list == ListState::ListedOnly) ++survey_.listed_only_rows;
            const bool tinted = r.list != ListState::Both;
            if (tinted) ImGui::PushStyleColor(ImGuiCol_Text, r.list == ListState::ListedOnly ? kListedOnlyColour : kMeasuredOnlyColour);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(cell_label(r).c_str());
            if (!help_shown_on_purpose() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list_state_label(r.list));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(mhz(r.frequency_hz).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(level_text(r.level, r.calibrated).c_str());
            ImGui::TableNextColumn();
            if (r.c1) ImGui::Text("%.1f%s", *r.c1, r.c1_estimated ? " est." : "");
            else ImGui::TextDisabled("-");
            if (tinted) ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void Gui::draw_graphs_panel() {
    const auto& monitor = state_.monitor();
    if (monitor->version() != plot_version_) {
        plot_ = monitor->plot();
        plot_version_ = plot_.version;
    }
    ReplaySession* replay = state_.replay();
    const bool replaying = replay != nullptr && replay->is_open();
    std::optional<std::int64_t> cursor = monitor->cursor();
    if (replaying) cursor = replay->view().cursor_mono;
    if (!replaying && cursor) {
        if (ImGui::SmallButton(tr("Live###graph.live"))) monitor->set_cursor(std::nullopt);
        text_tooltip(tr("Follow the newest measurement again."));
        ImGui::SameLine();
    }
    ImGui::Checkbox(tr("Level###graph.level"), &graph_level_);
    text_tooltip(tr("Show the graph of the levels."));
    ImGui::SameLine();
    ImGui::Checkbox("C1###graph.c1", &graph_c1_);
    text_tooltip(tr("Show the graph of C1, the path loss criterion."));
    ImGui::SameLine();
    ImGui::Checkbox(tr("Quality###graph.quality"), &graph_quality_);
    text_tooltip(tr("Show the graph of the quality."));
    // The threshold lines (UI-04), wrapped onto more rows when needed.
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (const ThresholdLine& t : plot_.thresholds) {
        const std::string label = t.label + "###graph.th." + t.name;
        const float w = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                        ImGui::CalcTextSize(t.label.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine();
        if (ImGui::GetCursorScreenPos().x + w > right) ImGui::NewLine();
        bool on = std::find(threshold_on_.begin(), threshold_on_.end(), t.name) != threshold_on_.end();
        if (ImGui::Checkbox(label.c_str(), &on)) {
            if (on) threshold_on_.push_back(t.name);
            else threshold_on_.erase(std::remove(threshold_on_.begin(), threshold_on_.end(), t.name), threshold_on_.end());
        }
        char tip[160];
        std::snprintf(tip, sizeof tip, tr("Draw the serving cell's %s (%.1f %s) as a line."), t.label.c_str(), t.value, t.unit.c_str());
        text_tooltip(tip);
    }
    if (plot_.samples.empty()) {
        ImGui::TextDisabled(tr("No measurements yet."));
        return;
    }
    // The time axis: the graph span of the settings live, the whole recording in a replay.
    const std::int64_t t1 = plot_.samples.back().t_ns;
    const auto span_max = static_cast<std::int64_t>(state_.settings().survey.graph_span_min * 60e9);
    const std::int64_t span = replaying ? std::max<std::int64_t>(1, t1 - plot_.samples.front().t_ns)
                                        : std::max<std::int64_t>(std::min<std::int64_t>(60'000'000'000LL, span_max),
                                                                 std::min<std::int64_t>(span_max, t1 - plot_.samples.front().t_ns));
    const std::int64_t t0 = t1 - span;
    const int graphs = (graph_level_ ? 1 : 0) + (graph_c1_ ? 1 : 0) + (graph_quality_ ? 1 : 0);
    if (graphs == 0) return;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // At least room for the name and two value labels apart (TST-52); the
    // panel scrolls when the graphs do not fit.
    const float gh = std::max(ImGui::GetTextLineHeight() * 6.0F, avail.y / static_cast<float>(graphs) - 4.0F);
    const bool light = light_theme();
    survey_.graph_samples = 0;
    for (const auto& s : plot_.samples) survey_.graph_samples += s.t_ns >= t0 ? 1 : 0;
    survey_.graph_traces = plot_.cells.size();
    survey_.cursor = cursor.has_value();

    // kind: 0 level, 1 C1, 2 quality.
    auto graph = [&](int kind, const char* id, const char* unit) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        // Margins and label places by the font (TST-52: at any UI scale the
        // axis labels stay apart and out of the plot).
        const float line_h = ImGui::GetTextLineHeight();
        const float left = ImGui::CalcTextSize("-100").x + px(8.0F), bottom = line_h + px(2.0F);
        const ImVec2 size(std::max(100.0F, avail.x), gh);
        ImGui::InvisibleButton(id, size);
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) || (ImGui::IsItemActive() && ImGui::IsMouseDragging(0));
        const ImVec2 a(p0.x + left, p0.y + 2.0F), b(p0.x + size.x - 4.0F, p0.y + size.y - bottom);
        dl->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_Border));
        double lo = 1e9, hi = -1e9;
        auto value = [&](const HistorySample& s, const TracePoint& p) -> double {
            return kind == 0 ? p.level : kind == 1 ? p.c1 : (&p == &s.points.front() ? s.quality : std::nan(""));
        };
        for (const auto& s : plot_.samples) {
            if (s.t_ns < t0) continue;
            for (const auto& p : s.points) {
                const double v = value(s, p);
                if (std::isfinite(v)) lo = std::min(lo, v), hi = std::max(hi, v);
            }
        }
        for (const ThresholdLine& t : plot_.thresholds)
            if (std::find(threshold_on_.begin(), threshold_on_.end(), t.name) != threshold_on_.end() &&
                t.level_graph == (kind == 0) && kind != 2)
                lo = std::min(lo, t.value), hi = std::max(hi, t.value);
        const Range y = autoscale(lo <= hi ? lo : std::nan(""), lo <= hi ? hi : std::nan(""), kind == 2 ? 5.0 : 20.0,
                                  kind == 0 ? Range{-120, -40} : kind == 1 ? Range{-10, 40} : Range{0, 40});
        auto sx = [&](std::int64_t t) { return a.x + (b.x - a.x) * static_cast<float>(static_cast<double>(t - t0) / static_cast<double>(span)); };
        auto sy = [&](double v) { return b.y - (b.y - a.y) * static_cast<float>((v - y.lo) / y.span()); };
        const ImU32 grid = ImGui::GetColorU32(ImGuiCol_Border, 0.5F);
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        // The graph's name at the top left inside the plot; no value label
        // beside it (the labels are left of the plot, two lines apart).
        const std::string name = std::string(kind == 0 ? tr("Level") : kind == 1 ? "C1" : tr("Quality")) + " (" + unit + ")";
        float last_label_y = 1e9F;  // a value's label only a line clear of the one below it
        for (double v : nice_ticks(y.lo, y.hi, std::max(2, static_cast<int>((b.y - a.y) / (line_h * 2.0F))))) {
            dl->AddLine(ImVec2(a.x, sy(v)), ImVec2(b.x, sy(v)), grid);
            char l[24];
            std::snprintf(l, sizeof l, "%.0f", v);
            const float ly = std::clamp(sy(v) - line_h * 0.5F, p0.y, b.y - line_h);
            if (ly + line_h > last_label_y) continue;
            last_label_y = ly;
            dl->AddText(ImVec2(a.x - px(4.0F) - ImGui::CalcTextSize(l).x, ly), text, l);
        }
        const float time_w = ImGui::CalcTextSize("00:00:00").x + px(16.0F);
        const std::int64_t step = time_step_ns(span, std::max(1, std::min(8, static_cast<int>((b.x - a.x) / time_w))));
        for (std::int64_t t = (t0 / step + 1) * step; t <= t1; t += step) {
            dl->AddLine(ImVec2(sx(t), a.y), ImVec2(sx(t), b.y), grid);
            const std::string ct = clock_text(t - plot_.samples.front().t_ns);
            const float tw = ImGui::CalcTextSize(ct.c_str()).x;
            dl->AddText(ImVec2(std::clamp(sx(t) - tw * 0.5F, a.x, b.x - tw), b.y + 1.0F), text, ct.c_str());
        }
        dl->AddText(ImVec2(a.x + px(4.0F), a.y + px(2.0F)), text, name.c_str());
        dl->PushClipRect(a, b, true);
        // Per trace cell: a polyline over the samples where it has a value.
        const std::size_t traces = kind == 2 ? 1 : plot_.cells.size();
        for (std::size_t ci = 0; ci < traces; ++ci) {
            const ImU32 col = kind == 2 ? ImGui::GetColorU32(ImGuiCol_PlotLines) : im_colour(trace_colour(plot_.cells[ci], light));
            ImVec2 prev;
            bool have = false;
            for (const auto& s : plot_.samples) {
                if (s.t_ns < t0 || s.points.empty()) {
                    have = false;
                    continue;
                }
                double v = std::nan("");
                bool serving = false;
                if (kind == 2) {
                    v = s.quality;
                } else {
                    for (const auto& p : s.points)
                        if (p.cell == ci) {
                            v = value(s, p);
                            serving = static_cast<int>(ci) == s.serving;
                        }
                }
                if (!std::isfinite(v)) {
                    have = false;
                    continue;
                }
                const ImVec2 q(sx(s.t_ns), sy(v));
                if (have) dl->AddLine(prev, q, col, serving || kind == 2 ? 2.0F : 1.0F);
                prev = q;
                have = true;
            }
        }
        for (const ThresholdLine& t : plot_.thresholds)
            if (kind != 2 && t.level_graph == (kind == 0) &&
                std::find(threshold_on_.begin(), threshold_on_.end(), t.name) != threshold_on_.end()) {
                dl->AddLine(ImVec2(a.x, sy(t.value)), ImVec2(b.x, sy(t.value)), IM_COL32(255, 190, 60, 220), 1.5F);
                dl->AddText(ImVec2(b.x - ImGui::CalcTextSize(t.label.c_str()).x - px(4.0F), sy(t.value) - line_h),
                            IM_COL32(255, 190, 60, 255), t.label.c_str());
            }
        if (cursor) dl->AddLine(ImVec2(sx(*cursor), a.y), ImVec2(sx(*cursor), b.y), ImGui::GetColorU32(ImGuiCol_Text), 1.0F);
        dl->PopClipRect();
        // UI-04: a click moves the cursor; every panel follows.
        if (clicked) {
            const float mx = std::clamp(ImGui::GetIO().MousePos.x, a.x, b.x);
            const std::int64_t t = t0 + static_cast<std::int64_t>(static_cast<double>(span) * (mx - a.x) / std::max(1.0F, b.x - a.x));
            if (replaying) (void)replay->seek_mono(t);
            else monitor->set_cursor(t);
        }
    };
    if (graph_level_) graph(0, "###graph.level_plot", "dBm");
    if (graph_c1_) graph(1, "###graph.c1_plot", "dB");
    if (graph_quality_) graph(2, "###graph.quality_plot", tr("the technology's unit"));
    for (const ThresholdLine& t : plot_.thresholds)
        survey_.threshold_lines += std::find(threshold_on_.begin(), threshold_on_.end(), t.name) != threshold_on_.end() ? 1 : 0;
}

void Gui::draw_scanner_panel(const SurveyFrame& f) {
    ImGui::TextDisabled(tr("Every carrier the radios report besides the serving cell and its neighbours."));
    if (f.scan.empty()) {
        ImGui::TextDisabled(tr("No other carriers."));
        return;
    }
    if (ImGui::BeginTable("###scanner.table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(tr("Frequency"));
        ImGui::TableSetupColumn(tr("Cell"));
        ImGui::TableSetupColumn(tr("Level"));
        ImGui::TableSetupColumn(tr("State"));
        headers_row();
        for (const CellRow& r : f.scan) {
            ++survey_.scan_rows;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(mhz(r.frequency_hz).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.name.empty() ? r.identity.c_str() : r.name.c_str());
            ImGui::TableNextColumn();
            // A level bar with the value.
            const float frac = std::isnan(r.level) ? 0.0F : static_cast<float>(std::clamp((r.level + 130.0) / 90.0, 0.0, 1.0));
            ImGui::ProgressBar(frac, ImVec2(-1.0F, 0.0F), level_text(r.level, r.calibrated).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.flags_text.c_str());
        }
        ImGui::EndTable();
    }
}

void Gui::draw_events_panel() {
    const auto events = state_.monitor()->events();
    if (events.empty()) {
        ImGui::TextDisabled(tr("No events yet."));
        return;
    }
    const std::int64_t t0 = events.front().t_ns;
    if (ImGui::BeginTable("###events.table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Time"), ImGuiTableColumnFlags_WidthFixed, em(70.0F));
        ImGui::TableSetupColumn(tr("Event"), ImGuiTableColumnFlags_WidthFixed, em(130.0F));
        ImGui::TableSetupColumn(tr("Text"));
        headers_row();
        // Newest first.
        for (auto it = events.rbegin(); it != events.rend(); ++it) {
            ++survey_.event_rows;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(clock_text(it->t_ns - t0).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(event_kind_label(it->kind));
            ImGui::TableNextColumn();
            if (it->warning) ImGui::TextColored(kWarnColour, "%s", it->text.c_str());
            else ImGui::TextUnformatted(it->text.c_str());
        }
        ImGui::EndTable();
    }
}

void Gui::draw_stats_panel() {
    const SurveyStats s = state_.monitor()->stats();
    char line[160];
    std::snprintf(line, sizeof line, tr("%.1f %% of the measurements at or above %.0f dBm"), s.percent_above, s.threshold_dbm);
    survey_.stats = line;
    ImGui::TextUnformatted(line);
    if (ImGui::BeginTable("###stats.table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        auto row = [](const char* k, const std::string& v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(k);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(v.c_str());
        };
        row(tr("Measurement cycles"), std::to_string(s.cycles));
        row(tr("Serving-cell changes"), std::to_string(s.serving_changes));
        row(tr("Sync losses"), std::to_string(s.sync_losses));
        row(tr("Time in coverage"), clock_text(static_cast<std::int64_t>(s.seconds_in_coverage * 1e9)));
        row(tr("Time out of coverage"), clock_text(static_cast<std::int64_t>(s.seconds_out_of_coverage * 1e9)));
        row(tr("Records dropped by the radios"), std::to_string(s.records_dropped));
        row(tr("Cycles with data loss"), std::to_string(s.data_loss_cycles));
        ImGui::EndTable();
    }
}

// -- the status bar's survey items (UI-08) ----------------------------------------------------


std::vector<StatusPiece> Gui::status_pieces() {
    std::vector<StatusPiece> out;
    const std::string& s = state_.status();
    out.push_back({s.empty() ? std::string(tr("Ready")) : s, state_.status_is_error() ? ImGui::GetColorU32(kErrorColour) : 0U});
    const AppState::StatusBar b = state_.status_bar(ImGui::GetTime());
    survey_.status = b;
    auto item = [&](const std::string& text, bool warn, const ImVec4* colour = nullptr) {
        if (text.empty()) return;
        out.push_back({text, colour != nullptr ? ImGui::GetColorU32(*colour) : warn ? ImGui::GetColorU32(kWarnColour) : 0U});
    };
    item(b.radio, b.radio_warning);
    item(b.recording, false, b.recording_active ? &kRecordColour : nullptr);
    if (b.dropped_warning) item(b.dropped, true);
    item(b.interference, b.interference_warning);
    item(b.disk, b.disk_warning);
    item(b.power, b.power_warning);
    return out;
}

}  // namespace yantra::app
