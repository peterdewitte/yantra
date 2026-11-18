// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tools > Spectrum (UI-24, RAD-140): the spectrum line (power over
// frequency, axis in MHz), the waterfall of about the last 120 s with its
// colour legend, the serving and neighbour carriers marked, the band
// setting and the flags. The model is app/model spectrum_window.h; ids:
// win.spectrum, spectrum.first, spectrum.last, spectrum.default,
// spectrum.start_radio, spectrum.plot, spectrum.waterfall, spectrum.legend.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "gui.h"
#include "gui_widgets.h"
#include "yantra/app/i18n.h"

namespace yantra::app {

using namespace gui_detail;

namespace {

const ImVec4 kNoteColour{1.0F, 0.75F, 0.25F, 1.0F};
constexpr int kWaterfallColumns = 512;
constexpr int kWaterfallRows = 120;  // one per second of the last 120 s

ImU32 rgb_u32(std::uint32_t rgb, int alpha = 255) {
    return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, alpha);
}

}  // namespace

void Gui::draw_spectrum_window() {
    bool& shown = state_.spectrum_window();
    if (!shown) return;
    bool open = true;
    if (begin_tool_window(tr("Spectrum"), "win.spectrum", open, 50.0F, 50.0F, 780.0F, 660.0F)) {
        help_button("5.6");
        draw_spectrum_body();
    }
    ImGui::End();
    if (!open) {
        shown = false;
        spectrum_tex_.reset();
        spectrum_tex_received_ = 0;
    }
}

void Gui::draw_spectrum_body() {
    const SpectrumView v = state_.spectrum_view();
    if (!v.radio.empty()) {
        ImGui::TextUnformatted(trf("Radio: {}", {v.radio}).c_str());
    }
    if (v.state == SpectrumView::State::NoProject || v.state == SpectrumView::State::NoRadio ||
        v.state == SpectrumView::State::NoCapability) {
        // UI-24: hidden without the SPECTRUM capability, with why.
        ImGui::TextDisabled("%s", v.message.c_str());
        return;
    }

    // The band (MHz), clipped to the radio's plan ranges.
    SpectrumForm& form = state_.spectrum_form();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Band"));
    ImGui::SameLine();
    const float field_w = em(110.0F);
    ImGui::SetNextItemWidth(field_w);
    ImGui::InputDouble(tr("First MHz###spectrum.first"), &form.first_mhz, 0.0, 0.0, "%.3f");
    text_tooltip(tr("The lowest frequency of the spectrum, in MHz."));
    same_line_if(field_w + label_w(tr("Last MHz")) + em(20.0F));
    ImGui::SetNextItemWidth(field_w);
    ImGui::InputDouble(tr("Last MHz###spectrum.last"), &form.last_mhz, 0.0, 0.0, "%.3f");
    text_tooltip(tr("The highest frequency of the spectrum, in MHz."));
    same_line_if(button_w(tr("380-400 MHz"), em(80.0F)));
    if (ImGui::Button(tr("380-400 MHz###spectrum.default"))) form = SpectrumForm{};
    text_tooltip(tr("The whole TETRA band, the default."));
    ImGui::SameLine();
    help_marker(tr("The radio is asked for the spectrum of this band while this window is open; the band is clipped to "
                   "what the radio can measure. An SDR shows only the part it receives at once (a few MHz)."));
    if (!v.band.note.empty()) ImGui::TextColored(kNoteColour, "%s", v.band.note.c_str());

    if (v.state == SpectrumView::State::NotMeasuring) {
        ImGui::TextDisabled("%s", v.message.c_str());
        LiveSurvey* live = state_.live();
        const bool replaying = state_.replay() != nullptr && state_.replay()->is_open();
        ImGui::BeginDisabled(live == nullptr || replaying);
        if (ImGui::Button(tr("Start radio###spectrum.start_radio")) && live != nullptr)
            (void)state_.show(live->start_radio());
        ImGui::EndDisabled();
        text_tooltip(replaying ? tr("Close the replay first.")
                               : tr("Measure with the chosen radio without a walk test (monitoring)."));
        return;
    }
    if (v.state == SpectrumView::State::Waiting) {
        ImGui::TextDisabled("%s", v.message.c_str());
        return;
    }

    // Unit and flags (SIMULATED, OVERLOAD, UNCALIBRATED).
    const radio::Spectrum& s = *v.latest;
    {
        char f_lo[32];
        char f_hi[32];
        char width[32];
        std::snprintf(f_lo, sizeof f_lo, "%.3f", s.start_hz / 1e6);
        std::snprintf(f_hi, sizeof f_hi, "%.3f", s.stop_hz / 1e6);
        std::snprintf(width, sizeof width, "%.1f", s.bin_width_hz / 1e3);
        const char* scale = s.calibrated ? tr("calibrated") : s.dbm ? tr("dBm from the radio") : tr("uncalibrated");
        ImGui::TextDisabled("%s", trf("{}-{} MHz, {} bins of {} kHz, {} ({})",
                                      {f_lo, f_hi, std::to_string(s.bin_count()), width, v.unit, scale})
                                      .c_str());
        for (const std::string& f : v.flags) {
            ImGui::SameLine();
            ImGui::TextColored(f == "SIMULATED" ? ImVec4(0.4F, 0.7F, 1.0F, 1.0F) : kNoteColour, "%s", f.c_str());
        }
    }
    if (!v.coverage_note.empty()) ImGui::TextColored(kNoteColour, "%s", v.coverage_note.c_str());
    if (!v.pending_note.empty()) ImGui::TextColored(kNoteColour, "%s", v.pending_note.c_str());

    // The frequency axis: the spectrum's own span (what the radio covers).
    const double f0 = s.start_hz;
    const double f1 = s.stop_hz;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line_h = ImGui::GetTextLineHeight();
    const float label_left = ImGui::CalcTextSize("-120").x + px(8.0F);
    const float legend_w = em(60.0F);
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float plot_w = std::max(em(120.0F), avail_w - label_left - legend_w - px(8.0F));
    const ImU32 grid = ImGui::GetColorU32(ImGuiCol_Border, 0.5F);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    auto sx = [&](double hz, float ax, float bx) {
        return ax + (bx - ax) * static_cast<float>((hz - f0) / std::max(1.0, f1 - f0));
    };

    // -- the spectrum line ----------------------------------------------------------------
    const float plot_h = std::max(em(140.0F), ImGui::GetContentRegionAvail().y * 0.45F);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("###spectrum.plot", ImVec2(label_left + plot_w, plot_h + line_h * 2.0F + px(4.0F)));
    {
        const ImVec2 a(p0.x + label_left, p0.y + line_h);
        const ImVec2 b(a.x + plot_w, a.y + plot_h);
        dl->AddRect(a, b, grid);
        auto sy = [&](double db) {
            return b.y - (b.y - a.y) * static_cast<float>((db - v.min_db) / std::max(1.0, v.max_db - v.min_db));
        };
        float last_label_y = 1e9F;
        for (double t : nice_ticks(v.min_db, v.max_db, std::max(2, static_cast<int>(plot_h / (line_h * 2.0F))))) {
            dl->AddLine(ImVec2(a.x, sy(t)), ImVec2(b.x, sy(t)), grid);
            char l[24];
            std::snprintf(l, sizeof l, "%.0f", t);
            const float ly = std::clamp(sy(t) - line_h * 0.5F, a.y, b.y - line_h);
            if (ly + line_h > last_label_y) continue;
            last_label_y = ly;
            dl->AddText(ImVec2(a.x - px(4.0F) - ImGui::CalcTextSize(l).x, ly), text, l);
        }
        dl->AddText(ImVec2(a.x - label_left + px(2.0F), p0.y), text, v.unit.c_str());
        const float tick_w = ImGui::CalcTextSize("000.000").x + px(16.0F);
        for (double t : nice_ticks(f0 / 1e6, f1 / 1e6, std::max(2, std::min(10, static_cast<int>(plot_w / tick_w))))) {
            const float x = sx(t * 1e6, a.x, b.x);
            if (x < a.x || x > b.x) continue;
            dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), grid);
            char l[24];
            std::snprintf(l, sizeof l, "%.3f", t);
            const float tw = ImGui::CalcTextSize(l).x;
            dl->AddText(ImVec2(std::clamp(x - tw * 0.5F, a.x, b.x - tw), b.y + 1.0F), text, l);
        }
        const char* axis = "MHz";
        dl->AddText(ImVec2(b.x - ImGui::CalcTextSize(axis).x, b.y + line_h + 1.0F), text, axis);
        dl->PushClipRect(a, b, true);
        // The carriers measured now: serving (green) and neighbours (blue).
        for (const SpectrumMarker& m : v.markers) {
            if (m.frequency_hz < f0 || m.frequency_hz > f1) continue;
            const float x = sx(m.frequency_hz, a.x, b.x);
            const ImU32 c = m.serving ? IM_COL32(67, 160, 71, 230) : IM_COL32(30, 136, 229, 200);
            dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), c, m.serving ? 2.0F : 1.0F);
            const std::string l = (m.serving ? tr("Serving: ") : "") + m.label;
            const float tw = ImGui::CalcTextSize(l.c_str()).x;
            dl->AddText(ImVec2(std::min(x + px(3.0F), b.x - tw - px(2.0F)), a.y + px(2.0F) + (m.serving ? 0.0F : line_h)),
                        c, l.c_str());
        }
        // Power per bin: the strongest bin under each pixel column.
        const int cols = std::max(2, static_cast<int>(plot_w));
        ImVec2 prev;
        bool have = false;
        const ImU32 lc = ImGui::GetColorU32(ImGuiCol_PlotLines);
        for (int c = 0; c < cols; ++c) {
            const double h0 = f0 + (f1 - f0) * c / cols;
            const double h1 = f0 + (f1 - f0) * (c + 1) / cols;
            long i0 = static_cast<long>(std::floor((h0 - s.start_hz) / s.bin_width_hz));
            long i1 = static_cast<long>(std::ceil((h1 - s.start_hz) / s.bin_width_hz)) - 1;
            i0 = std::clamp(i0, 0L, static_cast<long>(s.bin_count()) - 1);
            i1 = std::clamp(i1, i0, static_cast<long>(s.bin_count()) - 1);
            float val = -1e30F;
            for (long i = i0; i <= i1; ++i) val = std::max(val, s.power_db[static_cast<std::size_t>(i)]);
            const ImVec2 q(a.x + plot_w * (static_cast<float>(c) + 0.5F) / static_cast<float>(cols), sy(val));
            if (have) dl->AddLine(prev, q, lc, 1.5F);
            prev = q;
            have = true;
        }
        dl->PopClipRect();
        if (ImGui::IsItemHovered()) {
            const float mx = ImGui::GetIO().MousePos.x;
            if (mx >= a.x && mx <= b.x) {
                const double hz = f0 + (f1 - f0) * (mx - a.x) / plot_w;
                const auto i = static_cast<std::size_t>(
                    std::clamp((hz - s.start_hz) / s.bin_width_hz, 0.0, static_cast<double>(s.bin_count() - 1)));
                ImGui::SetTooltip("%.4f MHz: %.1f %s", s.bin_centre_hz(i) / 1e6, s.power_db[i], v.unit.c_str());
            }
        }
    }

    // -- the waterfall ----------------------------------------------------------------------
    ImGui::TextDisabled("%s", tr("Waterfall: the last 120 s, newest at the top"));
    if (!spectrum_tex_.valid() || v.received != spectrum_tex_received_) {
        const std::vector<std::uint8_t> px_rgba =
            waterfall_pixels(v.history, f0, f1, kWaterfallColumns, kWaterfallRows, v.min_db, v.max_db);
        spectrum_tex_.set(kWaterfallColumns, kWaterfallRows, px_rgba.data());
        spectrum_tex_received_ = v.received;
    }
    const float wf_h = std::max(em(100.0F), ImGui::GetContentRegionAvail().y - px(8.0F));
    p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("###spectrum.waterfall", ImVec2(label_left + plot_w, wf_h));
    {
        const ImVec2 a(p0.x + label_left, p0.y);
        const ImVec2 b(a.x + plot_w, a.y + wf_h);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg));
        dl->AddImage(spectrum_tex_.ref(), a, b);
        dl->AddRect(a, b, grid);
        for (int sec : {0, 30, 60, 90, 120}) {
            const float y = a.y + (b.y - a.y) * static_cast<float>(sec) / 120.0F;
            char l[16];
            std::snprintf(l, sizeof l, "-%ds", sec);
            const float ly = std::clamp(y - line_h * 0.5F, a.y, b.y - line_h);
            dl->AddText(ImVec2(a.x - px(4.0F) - ImGui::CalcTextSize(l).x, ly), text, l);
        }
        for (const SpectrumMarker& m : v.markers) {
            if (m.frequency_hz < f0 || m.frequency_hz > f1) continue;
            const float x = sx(m.frequency_hz, a.x, b.x);
            dl->AddLine(ImVec2(x, a.y), ImVec2(x, a.y + px(6.0F)),
                        m.serving ? IM_COL32(67, 160, 71, 255) : IM_COL32(30, 136, 229, 255), 2.0F);
        }
    }
    // The legend: the colour scale with its dB values.
    ImGui::SameLine(0.0F, px(8.0F));
    const ImVec2 l0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("###spectrum.legend", ImVec2(legend_w, wf_h));
    text_tooltip(tr("The colour of each power level in the waterfall."));
    {
        const float bar_w = em(14.0F);
        const int steps = 32;
        for (int i = 0; i < steps; ++i) {
            const float y0 = l0.y + wf_h * static_cast<float>(i) / steps;
            const float y1 = l0.y + wf_h * static_cast<float>(i + 1) / steps;
            const double t = 1.0 - (i + 0.5) / steps;
            dl->AddRectFilled(ImVec2(l0.x, y0), ImVec2(l0.x + bar_w, y1), rgb_u32(waterfall_rgb(t)));
        }
        dl->AddRect(l0, ImVec2(l0.x + bar_w, l0.y + wf_h), grid);
        char hi[24];
        char lo[24];
        std::snprintf(hi, sizeof hi, "%.0f", v.max_db);
        std::snprintf(lo, sizeof lo, "%.0f", v.min_db);
        dl->AddText(ImVec2(l0.x + bar_w + px(4.0F), l0.y), text, hi);
        dl->AddText(ImVec2(l0.x + bar_w + px(4.0F), l0.y + wf_h * 0.5F - line_h * 0.5F), text, v.unit.c_str());
        dl->AddText(ImVec2(l0.x + bar_w + px(4.0F), l0.y + wf_h - line_h), text, lo);
    }
}

}  // namespace yantra::app
