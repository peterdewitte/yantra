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

    ImGui::SeparatorText("Radio");
    const RadioState rs = live->radio_state();
    if (rs.lost) ImGui::TextColored(kWarnColour, "%s", rs.text.c_str());
    else ImGui::TextUnformatted(rs.text.c_str());
    if (!live->radios().empty() || live->radio_running()) {
        if (live->radio_running()) {
            const std::string why = live->running() ? "Stop the walk test first."
                                    : live->recording() ? "Stop the recording first." : "";
            if (tool_button("Stop radio###survey.radio_stop", why, "Stop measuring."))
                (void)state_.show(live->stop_radio());
        } else {
            const std::string why = replaying ? "Close the replay first." : live->chosen() ? "" : "Choose a radio (Walk tab).";
            if (tool_button("Start radio###survey.radio_start", why,
                            "Measure with the chosen radio without a walk test (monitoring): the survey panels fill; "
                            "a recording can start."))
                (void)state_.show(live->start_radio());
        }
    } else {
        const std::string why = replaying ? "Close the replay first." : "";
        if (tool_button("Find radios###survey.find_radios", why, "Look for the radios (the Walk tab chooses one)."))
            (void)state_.show(live->find_radios());
    }

    draw_recording_controls(*live);
    draw_replay_controls();

    ImGui::SeparatorText("Coverage");
    double th = state_.monitor()->stats().threshold_dbm;
    ImGui::SetNextItemWidth(110.0F);
    if (ImGui::InputDouble("dBm###survey.threshold", &th, 1.0, 5.0, "%.0f") && th > -200.0 && th < 0.0)
        state_.monitor()->set_threshold(th);
    text_tooltip("The serving level that counts as covered in the statistics (UI-11).");
}

void Gui::draw_recording_controls(LiveSurvey& live) {
    ImGui::SeparatorText("Recording");
    RecordingForm& f = live.recording_form();
    const recording::RecorderStatus st = live.recording_status();
    if (live.recording()) {
        const bool paused = st.state == recording::RecorderState::Paused;
        ImGui::TextColored(paused ? kWarnColour : kRecordColour, "%s %s, file %u", paused ? "Paused:" : "Recording:",
                           st.name.c_str(), st.file_index);
        ImGui::TextDisabled("%llu messages in the file, %u markers, %u voice notes",
                            static_cast<unsigned long long>(st.messages_in_file), st.next_marker - 1, st.voice_notes);
        if (!st.error.empty()) ImGui::TextColored(kErrorColour, "%s", st.error.c_str());
        const std::string why_stop = live.running() ? "The walk test records into it: stop the walk test." : "";
        if (tool_button("Stop recording###rec.stop", why_stop, "Close the recording; its files are listed in the project."))
            (void)state_.show(live.stop_recording());
        ImGui::SetNextItemWidth(-1.0F);
        input_text("###rec.marker_label", marker_label_, 128);
        text_tooltip("The marker's label (optional).");
        const std::string why_mark = paused ? "The recording is paused." : "";
        if (tool_button("Marker###rec.marker", why_mark, "A numbered marker at this instant (REC-05).")) {
            (void)state_.show(live.marker(marker_label_));
            marker_label_.clear();
        }
        ImGui::SameLine();
        if (tool_button("New file###rec.new_file", why_mark, "Marker and continue in a new file."))
            (void)state_.show(live.marker(marker_label_, recording::MarkerAction::NewFile));
        ImGui::SameLine();
        if (tool_button("New file + pause###rec.new_file_pause", why_mark,
                        "Marker, close the file and pause; Resume continues in a new file."))
            (void)state_.show(live.marker(marker_label_, recording::MarkerAction::NewFilePause));
        if (paused && ImGui::Button("Resume###rec.resume")) (void)state_.show(live.resume_recording());
        // Voice notes (REC-06).
        if (live.voice_recording()) {
            char b[64];
            std::snprintf(b, sizeof b, "Stop voice note (%.0f s)###rec.voice_stop", live.voice_seconds());
            if (ImGui::Button(b)) (void)state_.show(live.voice_stop(marker_label_));
            text_tooltip("Store the voice note with the label above (optional).");
        } else if (tool_button("Voice note###rec.voice", live.voice_reason(),
                               "Speak a note into the recording at this instant (REC-06).")) {
            (void)state_.show(live.voice_start());
        }
    } else {
        ImGui::SetNextItemWidth(-1.0F);
        input_text("###rec.name", f.name, 128);
        text_tooltip("The recording's name (empty: a quick-record name with date and time).");
        ImGui::SetNextItemWidth(-1.0F);
        input_text("###rec.notes", f.notes, 512);
        text_tooltip("Notes for the recording's header (no personal data).");
        ImGui::SetNextItemWidth(110.0F);
        ImGui::InputDouble("min split###rec.split", &f.split_minutes, 1.0, 5.0, "%.0f");
        text_tooltip("A new file every so many minutes (0 = one file).");
        ImGui::SameLine();
        ImGui::Checkbox("zstd###rec.compress", &f.compress);
        text_tooltip("Compress the files (zstd).");
        const std::string why = live.record_reason();
        if (tool_button("Record###rec.record", why, "Record the measurements into the project's recordings folder."))
            (void)state_.show(live.record(false));
        ImGui::SameLine();
        if (tool_button("Quick record###rec.quick", why, "Record at once under a name with the date and time."))
            (void)state_.show(live.record(true));
    }
    // The ring buffer (REC-09).
    bool ring = f.ring;
    if (ImGui::Checkbox("Ring buffer###rec.ring", &ring)) {
        f.ring = ring;
        (void)state_.show(live.apply_ring());
    }
    text_tooltip("Keep the last minutes while the radio runs, without a recording; save them when something "
                 "happened (REC-09).");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0F);
    ImGui::BeginDisabled(f.ring);
    ImGui::InputDouble("min###rec.ring_minutes", &f.ring_minutes, 1.0, 5.0, "%.0f");
    ImGui::EndDisabled();
    const auto files = live.ring_files();
    if (!files.empty()) {
        ImGui::SetNextItemWidth(-1.0F);
        input_text("###rec.ring_name", ring_name_, 128);
        text_tooltip("The name of the saved recording.");
        const std::string why = ring_name_.empty() ? "Type a name first." : "";
        if (tool_button("Save ring buffer###rec.ring_save", why, "Save the kept minutes as a recording of the project.")) {
            (void)state_.show(live.save_ring(ring_name_));
            ring_name_.clear();
        }
    }
}

void Gui::draw_replay_controls() {
    ReplaySession* replay = state_.replay();
    LiveSurvey* live = state_.live();
    ImGui::SeparatorText("Replay");
    if (!replay->is_open()) {
        const auto list = replay->recordings();
        if (list.empty()) {
            ImGui::TextDisabled("No recordings in this project's folder yet.");
            return;
        }
        if (replay_pick_ >= static_cast<int>(list.size())) replay_pick_ = -1;
        ImGui::BeginChild("###replay.list", ImVec2(0.0F, std::min(6.0F, static_cast<float>(list.size())) *
                                                             ImGui::GetTextLineHeightWithSpacing() + 8.0F),
                          ImGuiChildFlags_Borders);
        for (std::size_t i = 0; i < list.size(); ++i) {
            char label[256];
            std::snprintf(label, sizeof label, "%s (%zu)###replay.item.%zu", list[i].name.c_str(), list[i].files.size(), i);
            if (ImGui::Selectable(label, replay_pick_ == static_cast<int>(i))) replay_pick_ = static_cast<int>(i);
        }
        ImGui::EndChild();
        const std::string why = replay_pick_ < 0 ? "Choose a recording first."
                                : live->running() ? "Stop the walk test first."
                                : live->recording() ? "Stop the recording first." : "";
        if (tool_button("Open###replay.open", why, "Replay the recording into the survey panels (the radio stops).")) {
            const Result r = state_.open_replay(list[static_cast<std::size_t>(replay_pick_)]);
            if (r.ok) panels_ = true;
        }
        return;
    }
    const ReplayView v = replay->view();
    ImGui::Text("%s  %s / %s", v.name.c_str(), clock_text(v.position_ns).c_str(), clock_text(v.duration_ns).c_str());
    using St = recording::ReplayPlayer::State;
    if (v.state == St::Playing) {
        if (ImGui::Button("Pause###replay.pause")) (void)state_.show(replay->pause());
    } else if (ImGui::Button("Play###replay.play")) {
        (void)state_.show(replay->play(replay_speed_));
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop###replay.stop")) (void)state_.show(replay->stop());
    ImGui::SameLine();
    if (ImGui::Button("Again###replay.again")) (void)state_.show(replay->play_again());
    text_tooltip("Play again from the start.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0F);
    ImGui::InputDouble("x###replay.speed", &replay_speed_, 0.0, 0.0, "%.1f");
    text_tooltip("The speed: 1 = real time, up to 100; 0 = as fast as possible.");
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
    text_tooltip("The name of the cut recording.");
    const std::string why = cut_name_.empty() ? "Type a name first." : "";
    if (tool_button("Cut: start to cursor###replay.cut_head", why, "Save the part before the cursor as a new recording (REC-08)."))
        (void)state_.show(replay->cut(true, cut_name_));
    ImGui::SameLine();
    if (tool_button("Cut: cursor to end###replay.cut_tail", why, "Save the part after the cursor as a new recording (REC-08)."))
        (void)state_.show(replay->cut(false, cut_name_));
    if (ImGui::Button("Close replay###replay.close")) (void)state_.close_replay();
}

// -- the panels under the plan -------------------------------------------------------------

void Gui::draw_survey_panels(float height) {
    survey_.panels = true;
    const auto& monitor = state_.monitor();
    ImGui::BeginChild("###survey", ImVec2(0.0F, height), ImGuiChildFlags_Borders);
    // NFR-07: the self-interference warning with its advice.
    const measure::SelfInterferenceWarning w = monitor->interference();
    if (w.active) {
        survey_.interference_banner = true;
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(kWarnColour, "Self-interference: %s", w.text.c_str());
        ImGui::PopTextWrapPos();
    }
    const SurveyFrame f = monitor->frame();
    if (ImGui::BeginTabBar("###survey.tabs")) {
        if (ImGui::BeginTabItem("Cells###survey.cells")) {
            survey_.tab = "cells";
            draw_cells_panel(f);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graphs###survey.graphs")) {
            survey_.tab = "graphs";
            draw_graphs_panel();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scanner###survey.scanner")) {
            survey_.tab = "scanner";
            draw_scanner_panel(f);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Events###survey.events")) {
            survey_.tab = "events";
            draw_events_panel();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Statistics###survey.stats")) {
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
        ImGui::TextDisabled("No measurements yet: start the radio, a walk test or a replay (Survey tab).");
        return;
    }
    if (f.at_cursor) {
        ImGui::TextColored(kWarnColour, "At the graph cursor");
        ImGui::SameLine();
        if (ImGui::SmallButton("Live###cells.live")) state_.monitor()->set_cursor(std::nullopt);
    }
    // UI-01: the large values.
    const ImVec2 start = ImGui::GetCursorPos();
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0F);
    if (f.serving) {
        const std::string lv = level_text(f.serving->level, f.serving->calibrated);
        ImGui::TextUnformatted(lv.c_str());
        survey_.large.push_back("Level " + lv);
    } else {
        ImGui::TextDisabled("No serving cell");
    }
    ImGui::PopFont();
    if (f.serving) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3F);
        std::string line;
        if (f.serving->c1) {
            char b[32];
            std::snprintf(b, sizeof b, "C1 %.1f dB%s", *f.serving->c1, f.serving->c1_estimated ? " (est.)" : "");
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
    ImGui::TextDisabled("%llu samples", static_cast<unsigned long long>(f.samples));
    ImGui::EndGroup();
    ImGui::SameLine(std::max(start.x + 260.0F, ImGui::GetCursorPosX() + 20.0F));

    // UI-02: the serving cell and its broadcast parameters.
    ImGui::BeginChild("###cells.serving", ImVec2(std::max(200.0F, ImGui::GetContentRegionAvail().x * 0.4F), 0.0F));
    if (f.serving) {
        survey_.serving = cell_label(*f.serving) + "  " + mhz(f.serving->frequency_hz);
        ImGui::SeparatorText("Serving cell");
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
    ImGui::SameLine();
    // UI-03: neighbours; listed and received, received only, listed only.
    ImGui::BeginChild("###cells.neighbours", ImVec2(0.0F, 0.0F));
    ImGui::SeparatorText("Neighbours");
    ImGui::TextColored(kMeasuredOnlyColour, "received, not listed");
    ImGui::SameLine();
    ImGui::TextColored(kListedOnlyColour, "  listed, not received");
    if (ImGui::BeginTable("###cells.neighbour_table", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Cell");
        ImGui::TableSetupColumn("Frequency");
        ImGui::TableSetupColumn("Level");
        ImGui::TableSetupColumn("C1");
        ImGui::TableHeadersRow();
        for (const CellRow& r : f.neighbours) {
            ++survey_.neighbour_rows;
            if (r.list == ListState::ListedOnly) ++survey_.listed_only_rows;
            const bool tinted = r.list != ListState::Both;
            if (tinted) ImGui::PushStyleColor(ImGuiCol_Text, r.list == ListState::ListedOnly ? kListedOnlyColour : kMeasuredOnlyColour);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(cell_label(r).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list_state_label(r.list));
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
        if (ImGui::SmallButton("Live###graph.live")) monitor->set_cursor(std::nullopt);
        text_tooltip("Follow the newest measurement again.");
        ImGui::SameLine();
    }
    ImGui::Checkbox("Level###graph.level", &graph_level_);
    ImGui::SameLine();
    ImGui::Checkbox("C1###graph.c1", &graph_c1_);
    ImGui::SameLine();
    ImGui::Checkbox("Quality###graph.quality", &graph_quality_);
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
        std::snprintf(tip, sizeof tip, "Draw the serving cell's %s (%.1f %s) as a line.", t.label.c_str(), t.value, t.unit.c_str());
        text_tooltip(tip);
    }
    if (plot_.samples.empty()) {
        ImGui::TextDisabled("No measurements yet.");
        return;
    }
    // The time axis: the last ten minutes live, the whole recording in a replay.
    const std::int64_t t1 = plot_.samples.back().t_ns;
    const std::int64_t span = replaying ? std::max<std::int64_t>(1, t1 - plot_.samples.front().t_ns)
                                        : std::max<std::int64_t>(60'000'000'000LL, std::min<std::int64_t>(600'000'000'000LL, t1 - plot_.samples.front().t_ns));
    const std::int64_t t0 = t1 - span;
    const int graphs = (graph_level_ ? 1 : 0) + (graph_c1_ ? 1 : 0) + (graph_quality_ ? 1 : 0);
    if (graphs == 0) return;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gh = std::max(60.0F, avail.y / static_cast<float>(graphs) - 4.0F);
    const bool light = light_theme();
    survey_.graph_samples = 0;
    for (const auto& s : plot_.samples) survey_.graph_samples += s.t_ns >= t0 ? 1 : 0;
    survey_.graph_traces = plot_.cells.size();
    survey_.cursor = cursor.has_value();

    // kind: 0 level, 1 C1, 2 quality.
    auto graph = [&](int kind, const char* id, const char* unit) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float left = 48.0F, bottom = 16.0F;
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
        for (double v : nice_ticks(y.lo, y.hi, std::max(2, static_cast<int>(gh / 30.0F)))) {
            dl->AddLine(ImVec2(a.x, sy(v)), ImVec2(b.x, sy(v)), grid);
            char l[24];
            std::snprintf(l, sizeof l, "%.0f", v);
            dl->AddText(ImVec2(p0.x + 2.0F, sy(v) - 7.0F), text, l);
        }
        const std::int64_t step = time_step_ns(span, 8);
        for (std::int64_t t = (t0 / step + 1) * step; t <= t1; t += step) {
            dl->AddLine(ImVec2(sx(t), a.y), ImVec2(sx(t), b.y), grid);
            dl->AddText(ImVec2(sx(t) - 14.0F, b.y + 1.0F), text, clock_text(t - plot_.samples.front().t_ns).c_str());
        }
        dl->AddText(ImVec2(a.x + 4.0F, a.y + 2.0F), text,
                    (std::string(kind == 0 ? "Level" : kind == 1 ? "C1" : "Quality") + " (" + unit + ")").c_str());
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
                dl->AddText(ImVec2(b.x - 90.0F, sy(t.value) - 14.0F), IM_COL32(255, 190, 60, 255), t.label.c_str());
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
    if (graph_quality_) graph(2, "###graph.quality_plot", "the technology's unit");
    for (const ThresholdLine& t : plot_.thresholds)
        survey_.threshold_lines += std::find(threshold_on_.begin(), threshold_on_.end(), t.name) != threshold_on_.end() ? 1 : 0;
}

void Gui::draw_scanner_panel(const SurveyFrame& f) {
    ImGui::TextDisabled("Every carrier the radios report besides the serving cell and its neighbours.");
    if (f.scan.empty()) {
        ImGui::TextDisabled("No other carriers.");
        return;
    }
    if (ImGui::BeginTable("###scanner.table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Frequency");
        ImGui::TableSetupColumn("Cell");
        ImGui::TableSetupColumn("Level");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
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
        ImGui::TextDisabled("No events yet.");
        return;
    }
    const std::int64_t t0 = events.front().t_ns;
    if (ImGui::BeginTable("###events.table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0F);
        ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthFixed, 130.0F);
        ImGui::TableSetupColumn("Text");
        ImGui::TableHeadersRow();
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
    std::snprintf(line, sizeof line, "%.1f %% of the measurements at or above %.0f dBm", s.percent_above, s.threshold_dbm);
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
        row("Measurement cycles", std::to_string(s.cycles));
        row("Serving-cell changes", std::to_string(s.serving_changes));
        row("Sync losses", std::to_string(s.sync_losses));
        row("Time in coverage", clock_text(static_cast<std::int64_t>(s.seconds_in_coverage * 1e9)));
        row("Time out of coverage", clock_text(static_cast<std::int64_t>(s.seconds_out_of_coverage * 1e9)));
        row("Records dropped by the radios", std::to_string(s.records_dropped));
        row("Cycles with data loss", std::to_string(s.data_loss_cycles));
        ImGui::EndTable();
    }
}

// -- the status bar's survey items (UI-08) ----------------------------------------------------

void Gui::draw_status_items() {
    const AppState::StatusBar b = state_.status_bar(ImGui::GetTime());
    survey_.status = b;
    auto item = [](const std::string& text, bool warn, const ImVec4* colour = nullptr) {
        if (text.empty()) return;
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (colour) ImGui::TextColored(*colour, "%s", text.c_str());
        else if (warn) ImGui::TextColored(kWarnColour, "%s", text.c_str());
        else ImGui::TextUnformatted(text.c_str());
    };
    item(b.radio, b.radio_warning);
    item(b.recording, false, b.recording_active ? &kRecordColour : nullptr);
    if (b.dropped_warning) item(b.dropped, true);
    item(b.interference, b.interference_warning);
    item(b.disk, b.disk_warning);
    item(b.power, b.power_warning);
}

}  // namespace yantra::app
