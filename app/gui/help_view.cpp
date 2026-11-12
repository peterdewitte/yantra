// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The in-app help (DOC-07 e): the help button of every window, F1, and the
// help window `win.help` that shows a section of the bundled user manual
// (Markdown: headings, paragraphs, bullets). Over a modal dialog the help
// opens as a modal of its own on top, so the dialog stays as it is. Also
// the popup of the level alerts `win.alerts` (UI-10). Ids: docs/APP.md
// "Touch layout, themes, languages, help and sounds (step 9.9)".
#include "gui.h"
#include "gui_widgets.h"

#include <imgui.h>

#include <sstream>
#include <string>

#include "yantra/app/i18n.h"
#include "yantra/app/manual.h"

namespace yantra::app {

using namespace gui_detail;

void Gui::help_button(const char* section) {
    const ManualSection* m = find_manual_section(section);
    const std::string label = std::string(tr("Help")) + "###help";
    const float w = button_w(label.c_str(), 60.0F);
    const float x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(std::max(x, x + ImGui::GetContentRegionAvail().x - w));
    if (ImGui::Button(label.c_str(), ImVec2(w, 0.0F))) (void)state_.open_help(section);
    text_tooltip(trf("Open the user manual at {} {} [F1]", {section, m != nullptr ? tr(m->title) : ""}).c_str());
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) help_section_ = section;
}

namespace {

// A small Markdown subset: "#" headings, "- " bullets, paragraphs, `code`
// marks dropped. Links show their text.
std::string plain(std::string s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '`' || s[i] == '*') continue;
        if (s[i] == '[') {
            const std::size_t close = s.find("](", i);
            const std::size_t end = close == std::string::npos ? close : s.find(')', close);
            if (close != std::string::npos && end != std::string::npos) {
                out += s.substr(i + 1, close - i - 1);
                i = end;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

void draw_markdown(const std::string& text) {
    std::istringstream in(text);
    std::string line, para;
    auto flush = [&] {
        if (para.empty()) return;
        ImGui::TextWrapped("%s", plain(para).c_str());
        ImGui::Spacing();
        para.clear();
    };
    while (std::getline(in, line)) {
        if (line.rfind("#", 0) == 0) {
            flush();
            std::size_t n = 0;
            while (n < line.size() && line[n] == '#') ++n;
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * (n <= 1 ? 1.5F : 1.25F));
            ImGui::TextWrapped("%s", plain(line.substr(std::min(line.size(), n + 1))).c_str());
            ImGui::PopFont();
            ImGui::Spacing();
        } else if (line.rfind("- ", 0) == 0) {
            flush();
            ImGui::Bullet();
            ImGui::TextWrapped("%s", plain(line.substr(2)).c_str());
        } else if (line.empty()) {
            flush();
        } else {
            para += (para.empty() ? "" : " ") + line;
        }
    }
    flush();
}

}  // namespace

void Gui::draw_help_body() {
    const HelpView& v = state_.help_view();
    if (!v.number.empty()) {
        if (ImGui::Button(tr("Contents###help.contents")))
            (void)(v.quick_starts ? state_.open_quick_start("") : state_.open_help(""));
        text_tooltip(v.quick_starts ? tr("The list of the quick starts.") : tr("The list of the manual's chapters."));
        ImGui::SameLine();
    }
    if (ImGui::Button(tr("Close###help.close"))) state_.close_help();
    text_tooltip(tr("Close the help [Esc]"));
    ImGui::Separator();
    if (ImGui::BeginChild("###help.text", ImVec2(0.0F, 0.0F))) {
        ImGui::PushTextWrapPos(0.0F);
        if (v.number.empty()) {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5F);
            ImGui::TextUnformatted(v.title.c_str());
            ImGui::PopFont();
            const bool quick = v.quick_starts;
            for (const auto& [number, title] : v.chapters) {
                const std::string text = number + "  " + title;
                const std::string id = (quick ? "###help.quick_start." : "###help.chapter.") + number;
                bool cut = false;
                if (selectable_ellipsis(text, id, false, target_h(0.6F), cut)) {
                    (void)(quick ? state_.open_quick_start(number) : state_.open_help(number));
                    break;
                }
                const std::string tip = std::string(cut ? text + "\n" : "") + (quick ? tr("Open this quick start.") : tr("Open this chapter."));
                text_tooltip(tip.c_str());
            }
        } else if (!v.error.empty()) {
            ImGui::TextColored(kErrorColour, "%s", v.error.c_str());
        } else {
            draw_markdown(v.text);
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        state_.close_help();
}

void Gui::draw_help_window() {
    if (!state_.help_view().open || state_.dialog() != Dialog::None) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.45F, vp->WorkPos.y + 40.0F), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(std::min(em(560.0F), vp->WorkSize.x * 0.5F), vp->WorkSize.y * 0.8F),
                             ImGuiCond_Appearing);
    bool open = true;
    const std::string name = std::string(tr("Help")) + "###win.help";
    if (ImGui::Begin(name.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) draw_help_body();
    ImGui::End();
    if (!open) state_.close_help();
}

void Gui::draw_help_popup() {
    // Inside a modal dialog: the help is a modal on top of it.
    const char* id = "###win.help";
    if (!state_.help_view().open) return;
    if (!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(em(560.0F), vp->WorkSize.x * 0.9F), vp->WorkSize.y * 0.8F),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    const std::string name = std::string(tr("Help")) + id;
    bool open = true;
    if (ImGui::BeginPopupModal(name.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        draw_help_body();
        if (!state_.help_view().open) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!open) state_.close_help();
}

void Gui::draw_alerts_window() {
    AudibleAlerts& a = state_.alerts();
    if (a.popups().empty()) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5F, vp->WorkPos.y + 30.0F), ImGuiCond_Appearing,
                            ImVec2(0.5F, 0.0F));
    ImGui::SetNextWindowBgAlpha(0.95F);
    const std::string name = std::string(tr("Level alert")) + "###win.alerts";
    if (ImGui::Begin(name.c_str(), nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        help_button("5.5");
        ImGui::PushTextWrapPos(px(420.0F));
        for (const AlertPopup& p : a.popups()) ImGui::TextColored(kBannerColour, "%s", p.text.c_str());
        ImGui::PopTextWrapPos();
        if (dialog_button(tr("Dismiss###alerts.dismiss"), tr("Close the alerts. A cell alerts again after it was "
                                                                 "2 dB above its threshold."))) a.dismiss_popups();
    }
    ImGui::End();
}

}  // namespace yantra::app
