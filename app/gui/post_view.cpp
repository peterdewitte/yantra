// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Post page of the side panel (step 9.8; docs/APP.md "Cell database,
// network checks and post-processing"): load recordings (PP-01), the
// value shown and its view (PP-02, PP-04), failed cells (PP-03), the
// binned layer's percentile (PP-05), coverage figures and an acceptance
// profile (EXP-06). Drawing only: the model is PostProcessing (app/model).
#include <imgui.h>

#include <cstdio>
#include <string>
#include <vector>

#include "gui.h"
#include "yantra/app/i18n.h"
#include "gui_widgets.h"

namespace yantra::app {

using namespace gui_detail;

namespace {

std::string dbm(const std::optional<double>& v, const std::string& unit) {
    if (!v) return "-";
    char b[48];
    std::snprintf(b, sizeof b, "%.1f %s", *v, unit.c_str());
    return b;
}

}  // namespace

void Gui::draw_post_section() {
    PostProcessing* pp = state_.post_processing();
    if (pp == nullptr) return;
    PostSettings& s = pp->settings();
    const PostSettings before = s;
    using C = survey::Metric::Carrier;
    using V = survey::Metric::Value;
    using M = PostSettings::Mode;

    // PP-01: recordings into the project.
    if (section_header(tr("Load recordings###post.load"), tr("Turn recordings of a walk into points of this floor."))) {
        std::string folder = pp->import_folder().string();
        if (input_text(tr("Folder###post.folder"), folder, 1024)) pp->import_folder() = folder;
        const std::vector<RecordingChoice> recs = pp->recordings();
        if (recs.empty()) ImGui::TextDisabled(tr("No recordings in this folder."));
        for (std::size_t i = 0; i < recs.size(); ++i) {
            const std::string b = tr("Load ") + recs[i].name + "###post.load." + std::to_string(i);
            if (ImGui::Button(b.c_str())) (void)state_.show(pp->import(recs[i]));
            text_tooltip(tr("Replay the recording into the project as walk-test points (the radio is not needed)."));
        }
    }

    // PP-02: the value shown.
    ImGui::SeparatorText(tr("Value"));
    auto carrier = [&](const char* label, C c, const char* tip) {
        if (ImGui::RadioButton(label, s.metric.carrier == c)) s.metric.carrier = c;
        text_tooltip(tip);
    };
    carrier(tr("Serving###post.carrier.serving"), C::Serving, tr("The value of the serving cell at each point."));
    ImGui::SameLine();
    carrier(tr("Strongest###post.carrier.strongest"), C::Strongest, tr("The value of the strongest cell at each point."));
    ImGui::SameLine();
    carrier(tr("Carrier###post.carrier.frequency"), C::Frequency, tr("The value of one carrier, chosen by its frequency."));
    if (s.metric.carrier == C::Frequency) {
        double mhz = static_cast<double>(s.metric.frequency_hz) / 1e6;
        ImGui::PushItemWidth(-1.0F);
        if (ImGui::InputDouble("###post.frequency", &mhz, 0.0125, 0.1, "%.4f MHz") && mhz >= 0.0)
            s.metric.frequency_hz = static_cast<std::uint64_t>(mhz * 1e6 + 0.5);
        ImGui::PopItemWidth();
    }
    auto value = [&](const char* label, V v, const char* tip) {
        if (ImGui::RadioButton(label, s.metric.value == v)) s.metric.value = v;
        text_tooltip(tip);
    };
    value(tr("Level###post.value.level"), V::Level, tr("The received level in dBm."));
    ImGui::SameLine();
    value("C1###post.value.c1", V::C1, tr("The path loss criterion C1 in dB."));
    ImGui::SameLine();
    value(tr("Quality###post.value.quality"), V::Quality, tr("A quality value of the technology plugin (MER, BER, ...)."));
    if (s.metric.value == V::Quality) {
        if (s.metric.quality_field.empty()) s.metric.quality_field = "mer_pct";
        ImGui::PushItemWidth(-1.0F);
        input_text("###post.quality_field", s.metric.quality_field, 64);
        ImGui::PopItemWidth();
        text_tooltip(tr("The quality field of the technology plugin, e.g. mer_pct, ber_pct or snr_db."));
    }

    // PP-02 / PP-04: how the points show it.
    ImGui::SeparatorText(tr("View"));
    if (ImGui::RadioButton(tr("Values###post.mode.values"), s.mode == M::Values)) s.mode = M::Values;
    text_tooltip(tr("Colour the points by their value."));
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Good/bad###post.mode.goodbad"), s.mode == M::GoodBad)) s.mode = M::GoodBad;
    text_tooltip(tr("Two colours: at or above the threshold, or below it."));
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Offset###post.mode.offset"), s.mode == M::Offset)) s.mode = M::Offset;
    text_tooltip(tr("The values with an offset added, to simulate an extra loss."));
    ImGui::PushItemWidth(120.0F);
    if (s.mode == M::GoodBad) {
        ImGui::InputDouble(tr("Threshold###post.threshold"), &s.threshold, 1.0, 5.0, "%.1f");
        text_tooltip(tr("Good: at or above (for error rates: at or below) this value."));
    }
    if (s.mode == M::Offset) {
        ImGui::InputDouble(tr("dB###post.offset"), &s.offset_db, 1.0, 5.0, "%+.1f");
        text_tooltip(tr("Add this to every level (e.g. -10 dB building penetration or body loss): a simulation, "
                     "labelled so."));
    }
    ImGui::InputDouble(tr("Bin percentile###post.percentile"), &s.bin_percentile, 5.0, 10.0, "%.0f");
    text_tooltip(tr("The value of a square of the binned layer (Layers page): 50 is the median."));
    ImGui::PopItemWidth();
    if (s.bin_percentile < 0.0) s.bin_percentile = 0.0;
    if (s.bin_percentile > 100.0) s.bin_percentile = 100.0;
    ImGui::Checkbox(tr("Serving cell changes###post.changes"), &s.serving_changes);
    text_tooltip(tr("Mark the points where the serving cell changed."));

    // PP-03: failed cells.
    if (section_header(tr("Failed cells###post.failed"), tr("Simulate cells that are off (PP-03)."))) {
        ImGui::TextWrapped(tr("Switch cells off: the best server is chosen again from the levels measured."));
        const std::vector<survey::CellRef> cells = pp->cells_seen();
        if (cells.empty()) ImGui::TextDisabled(tr("No cells in the points of this floor."));
        for (std::size_t i = 0; i < cells.size(); ++i) {
            bool off = pp->failed(cells[i]);
            char mhz[32];
            std::snprintf(mhz, sizeof mhz, "%.4f MHz", static_cast<double>(cells[i].frequency_hz) / 1e6);
            const std::string label = std::string(mhz) + (cells[i].cell.empty() ? "" : "  " + cells[i].cell) +
                                      "###post.fail." + std::to_string(i);
            if (ImGui::Checkbox(label.c_str(), &off)) pp->set_failed(cells[i], off);
            text_tooltip(tr("Switch this cell off in the view."));
        }
    }
    if (ImGui::Button(tr("Measured serving level###post.reset"))) pp->reset();
    text_tooltip(tr("Back to the plain measured view."));
    if (!(s == before)) pp->apply();

    // The figures of the view shown.
    const PostStatsView v = pp->stats(state_.settings().layers.bin_size_m);
    ImGui::SeparatorText(tr("Figures"));
    ImGui::TextWrapped("%s", v.metric.c_str());
    if (!v.view_label.empty()) ImGui::TextColored(kBannerColour, "%s", v.view_label.c_str());
    ImGui::Text(tr("%zu points, %zu with a value"), v.stats.points, v.stats.with_value);
    ImGui::Text(tr("Median %s, min %s, max %s"), dbm(v.stats.median, v.stats.unit).c_str(),
                dbm(v.stats.min, v.stats.unit).c_str(), dbm(v.stats.max, v.stats.unit).c_str());
    // IND-43: the report's wording, per threshold "at or above".
    if (!v.stats.thresholds.empty()) ImGui::TextUnformatted(tr("Share of measured points (%)"));
    for (const survey::ThresholdShare& t : v.stats.thresholds)
        ImGui::Text(">= %.1f %s: %.1f", t.threshold, v.stats.unit.c_str(), t.percent_at_or_above);
    if (v.classified) ImGui::Text(tr("%zu good, %zu bad (%.1f %% good)"), v.classified->good, v.classified->bad,
                                  v.classified->percent_good);
    ImGui::Text(tr("%zu serving cell changes"), v.serving_changes);
    if (v.area) {
        ImGui::TextWrapped("%s", v.area->label.c_str());
        ImGui::Text(tr("%.1f %% of %.0f m2 tested"), v.area->percent, v.area->tested_m2);
    } else if (!v.area_note.empty()) {
        ImGui::TextDisabled(tr("Tested area: %s"), v.area_note.c_str());
    }

    // EXP-06: an acceptance profile on one of the three bases.
    if (ImGui::CollapsingHeader(tr("Acceptance###post.acceptance"), ImGuiTreeNodeFlags_DefaultOpen)) {
        AcceptanceForm& a = pp->acceptance();
        using B = survey::AcceptanceProfile::Basis;
        if (ImGui::RadioButton(tr("Points###post.basis.points"), a.basis == B::Points)) a.basis = B::Points;
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Tested area###post.basis.tested"), a.basis == B::TestedArea)) a.basis = B::TestedArea;
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Floor area###post.basis.floor"), a.basis == B::FloorArea)) a.basis = B::FloorArea;
        text_tooltip(tr("Points: the measured points; tested area: the binned squares walked; floor area: the "
                     "interpolated map (its confident cells)."));
        ImGui::PushItemWidth(120.0F);
        ImGui::InputDouble(tr("dBm at or above###post.acc_threshold"), &a.threshold_dbm, 1.0, 5.0, "%.1f");
        ImGui::InputDouble(tr("% at least###post.acc_percent"), &a.min_percent, 1.0, 5.0, "%.1f");
        ImGui::PopItemWidth();
        if (!v.acceptance.note.empty()) ImGui::TextDisabled("%s", v.acceptance.note.c_str());
        if (!v.acceptance_text.empty())
            ImGui::TextColored(v.acceptance.pass ? ImVec4(0.45F, 0.85F, 0.45F, 1.0F) : kErrorColour, "%s",
                               v.acceptance_text.c_str());
    }
    post_view_ = v;
}

}  // namespace yantra::app
