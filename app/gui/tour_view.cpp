// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The guided tour's views (DEMO-11, DEMO-12, step 9.10): the tour window
// (or its strip inside a dialog, which blocks windows behind it), the
// frame around the control a step is about (from the item probe's last
// frame, ui_probe.h), the download question, the first-start offer and
// the MAK credit line under the plan. The model is tour.h.
#include "gui.h"
#include "gui_widgets.h"
#include "ui_probe.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "yantra/app/i18n.h"

namespace yantra::app {

using namespace gui_detail;

namespace {

void report(AppState& state, const Result& r) { (void)state.show(r); }

}  // namespace

void Gui::draw_tour_body(const TourView& v, bool strip) {
    Tour& tour = state_.tour();
    if (strip) tour_.strip = true;
    // DEMO-07: the tour runs in demo mode.
    ImGui::TextColored(kBannerColour, "%s", v.demo_label.c_str());
    tour_.banner = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trf("Tour, step {} of {}", {std::to_string(v.index + 1), std::to_string(v.count)}).c_str());
    help_marker(tr("The guided tour: each step points at the control it is about (framed in orange) and says what to "
                   "do. Do it yourself or press Show me; Back repeats a step, Skip goes on, Leave closes the tour (it "
                   "remembers where you were)."));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15F);
    ImGui::TextUnformatted(v.title.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(v.text.c_str());
    if (!v.available) ImGui::TextColored(kBannerColour, "%s", v.unavailable.c_str());
    if (!v.progress.empty()) ImGui::TextDisabled("%s", v.progress.c_str());
    if (v.done && v.step != TourStep::Finish && v.available) ImGui::TextColored(ImVec4(0.45F, 0.85F, 0.45F, 1.0F), "%s", tr("Done: go on with Next."));
    if (!v.message.empty()) {
        if (v.message_error) ImGui::TextColored(kErrorColour, "%s", v.message.c_str());
        else ImGui::TextDisabled("%s", v.message.c_str());
    }
    if (v.credit && !strip) {
        ImGui::TextDisabled("%s", trf("Plan: {}, {}", {v.credit->credit, v.credit->licence}).c_str());
        text_tooltip(v.credit->licence_url.c_str());
    }
    ImGui::PopTextWrapPos();

    // The example buildings (first step).
    if (v.step == TourStep::Choose && !strip) {
        for (const TourBuildingView& b : v.buildings) {
            ImGui::BeginDisabled(!b.available || v.downloading);
            const std::string label = b.title + "###tour.building." + b.id;
            if (ImGui::Button(label.c_str(), ImVec2(-1.0F, target_h()))) report(state_, tour.choose(b.id));
            ImGui::EndDisabled();
            disabled_tooltip(b.detail.c_str(), b.available && !v.downloading,
                             !b.available ? b.reason : std::string(tr("A download is running.")));
        }
    }
    if (v.downloading) {
        const float f = v.fraction < 0.0 ? 0.0F : static_cast<float>(v.fraction);
        char bytes[48];
        std::snprintf(bytes, sizeof bytes, "%.1f MB", static_cast<double>(v.bytes) / 1e6);
        ImGui::ProgressBar(f, ImVec2(-1.0F, 0.0F), bytes);
        if (small_button(tr("Cancel download###tour.cancel_download"))) tour.cancel_download();
        text_tooltip(tr("Stop the download; the tour then uses the Fraunces Tavern."));
    }

    // Back, Show me, Skip / Next, Leave.
    ImGui::Spacing();
    ImGui::BeginDisabled(!v.can_back);
    if (ImGui::Button(tr("Back###tour.back"), ImVec2(0.0F, target_h()))) report(state_, tour.back());
    ImGui::EndDisabled();
    disabled_tooltip(tr("The step before: to repeat it."), v.can_back, tr("This is the first step."));
    ImGui::SameLine();
    ImGui::BeginDisabled(!v.can_show);
    if (ImGui::Button((v.show_me + "###tour.show_me").c_str(), ImVec2(0.0F, target_h()))) report(state_, tour.show_me());
    ImGui::EndDisabled();
    disabled_tooltip(tr("Do this step with the example building's data, through the same commands as the controls."),
                     v.can_show, v.show_reason);
    ImGui::SameLine();
    const bool can_next = v.step != TourStep::Choose || v.done;
    ImGui::BeginDisabled(!can_next);
    const char* next = v.last ? tr("Finish###tour.next") : (v.done || !v.available) ? tr("Next###tour.next") : tr("Skip###tour.next");
    if (ImGui::Button(next, ImVec2(0.0F, target_h()))) report(state_, tour.next());
    ImGui::EndDisabled();
    disabled_tooltip(v.last ? tr("End the tour.")
                     : v.done ? tr("Go on to the next step.")
                              : tr("Go on without doing this step (it is marked skipped)."),
                     can_next, tr("Choose an example building first."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Leave###tour.leave"), ImVec2(0.0F, target_h()))) tour.leave();
    text_tooltip(tr("Close the tour. Help > Tour opens it again at this step."));
    if (!strip) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Help###tour.help"), ImVec2(0.0F, target_h()))) (void)state_.open_help(v.help);
        text_tooltip(trf("Open the user manual at {}", {v.help}).c_str());
    }
    if (strip) ImGui::Separator();
}

void Gui::draw_tour() {
    Tour& tour = state_.tour();
    if (!tour.active()) return;
    // A dialog shows the tour as a strip inside it (draw_dialog_body).
    if (state_.dialog() != Dialog::None) return;
    const TourView v = tour.view();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(440.0F), vp->WorkSize.x * 0.45F);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - w - 12.0F, vp->WorkPos.y + 12.0F), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(w, 0.0F), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    bool open = true;
    if (ImGui::Begin(tr("Guided tour###tour"), &open, flags)) {
        tour_.window = true;
        draw_tour_body(v, false);
    }
    ImGui::End();
    if (!open) tour.leave();
}

void Gui::draw_tour_highlight(const TourView& v) {
    auto* probe = static_cast<UiProbe*>(ImGui::GetCurrentContext()->TestEngine);
    if (probe == nullptr) return;
    for (const std::string& t : v.targets) {
        for (const ProbeItem& it : probe->items()) {
            if (it.key() != t || it.x1 <= it.x0 || it.y1 <= it.y0) continue;
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const float pad = 4.0F;
            const float pulse = 0.6F + 0.4F * static_cast<float>(std::sin(ImGui::GetTime() * 4.0));
            dl->AddRect(ImVec2(it.x0 - pad, it.y0 - pad), ImVec2(it.x1 + pad, it.y1 + pad),
                        ImGui::GetColorU32(ImVec4(kBannerColour.x, kBannerColour.y, kBannerColour.z, pulse)), 4.0F, 3.0F);
            tour_.target = t;
            return;
        }
    }
}

void Gui::draw_tour_consent() {
    const auto& c = state_.tour().consent();
    tour_.consent = true;
    if (!c) {
        ImGui::TextDisabled("%s", tr("There is no question open."));
        if (dialog_button(tr("Close###dlg.cancel"), tr("Close this dialog."))) state_.cancel_dialog();
        return;
    }
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(trf("The tour's example {} uses a plan sheet of the MAK collection. Yantra does not ship "
                               "it: it is downloaded once from the MAK, after you agree.",
                               {c->building})
                               .c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("%s", c->sheet_title.c_str());
    ImGui::TextUnformatted(trf("Credit: {}", {c->credit}).c_str());
    ImGui::TextUnformatted(trf("Licence: {} ({})", {c->licence, c->licence_url}).c_str());
    ImGui::TextUnformatted(trf("Download: {} ({})", {c->url, c->size}).c_str());
    ImGui::TextUnformatted(trf("Kept in: {}", {c->cache_file}).c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted(c->privacy.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (dialog_button(tr("Download###dlg.ok"), tr("Download the sheet once into your cache folder and check it."), 160.0F))
        report(state_, state_.tour().answer_consent(true));
    ImGui::SameLine();
    if (dialog_button(tr("Use the Fraunces Tavern###dlg.cancel"),
                      tr("Download nothing: the tour uses the Fraunces Tavern, which comes with Yantra."), 220.0F))
        report(state_, state_.tour().answer_consent(false));
}

void Gui::draw_tour_offer() {
    if (!state_.tour_offer()) return;
    tour_.offer = true;
    ImGui::SeparatorText(tr("First time here?"));
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(tr("The guided tour walks you through an indoor survey with an example building and the "
                              "simulated radio \"Demo (simulated network)\": no radio needed."));
    ImGui::PopTextWrapPos();
    if (ImGui::Button(tr("Start the tour###start.tour_offer.yes"), ImVec2(0.0F, target_h()))) report(state_, state_.answer_tour_offer(true));
    text_tooltip(tr("Start the guided tour now (also Help > Tour)."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Not now###start.tour_offer.no"), ImVec2(0.0F, target_h()))) report(state_, state_.answer_tour_offer(false));
    text_tooltip(tr("Hide this offer; Help > Tour starts the tour whenever you want."));
    ImGui::Spacing();
}

void Gui::draw_plan_credit(const std::string& floor_id) {
    if (floor_id.empty()) return;
    const auto c = state_.plan_credit(floor_id);
    if (!c) return;
    // DEMO-12: credit and licence with the plan; the approximate scale.
    std::string line = trf("Plan: {}. Licence {}.", {c->credit, c->licence});
    if (!c->scale_note.empty()) line += " " + c->scale_note;
    tour_.credit = true;
    tour_.credit_text = line;
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextDisabled("%s", line.c_str());
    ImGui::PopTextWrapPos();
    text_tooltip((c->licence_url + " - " + c->item_url).c_str());
}

}  // namespace yantra::app
