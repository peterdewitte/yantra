// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small widgets shared by the ImGui layer's files (gui.cpp, gui_tools.cpp):
// tooltips on every control and on disabled ones with the reason (DOC-07
// b, c), "(?)" help markers (DOC-07 a), text input on std::string, dialog
// buttons of touch size (UI-13), the error line of a dialog.
#pragma once

#include <imgui.h>

#include <algorithm>
#include <string>

#include "yantra/app/app_state.h"

namespace yantra::app::gui_detail {

inline constexpr float kTouchTarget = 48.0F;  // UI-13: >= 48 px targets
inline const ImVec4 kBannerColour{0.95F, 0.62F, 0.07F, 1.0F};
inline const ImVec4 kErrorColour{1.0F, 0.45F, 0.40F, 1.0F};

/// Tooltips on or off (settings, DOC-07 h). Off hides the hover texts;
/// the "(?)" markers and the reasons of disabled controls stay.
inline bool& tooltips_shown() {
    static bool shown = true;
    return shown;
}
inline void set_tooltips_shown(bool on) { tooltips_shown() = on; }

/// The hover text of the last item, also when it is disabled (DOC-07 b, c).
inline void item_tooltip(const ActionView& a) {
    if (!tooltips_shown() && a.enabled) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
    ImGui::TextUnformatted(a.tooltip.c_str());
    if (!a.enabled) {
        ImGui::Spacing();
        ImGui::TextColored(kBannerColour, "%s", a.disabled_reason.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

inline void text_tooltip(const char* text) {
    if (!tooltips_shown()) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/// A dimmed "(?)" marker with an explanation on hover (DOC-07 a).
inline void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

inline std::string item_label(const ActionView& a) { return a.label + "###" + a.id; }

/// ImGui::InputText on a std::string (no imgui_stdlib needed).
inline bool input_text(const char* label, std::string& s, std::size_t capacity = 1024) {
    std::string buf = s;
    buf.resize(std::max(capacity, s.size() + 1), '\0');
    const bool changed = ImGui::InputText(label, buf.data(), buf.size());
    if (changed) s = std::string(buf.c_str());
    return changed;
}

inline bool dialog_button(const char* label, const char* tooltip, float width = 140.0F) {
    const bool pressed = ImGui::Button(label, ImVec2(width, kTouchTarget * 0.8F));
    text_tooltip(tooltip);
    return pressed;
}

inline void error_line(const AppState& state) {
    if (!state.status_is_error()) return;
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextColored(kErrorColour, "%s", state.status().c_str());
    ImGui::PopTextWrapPos();
}


}  // namespace yantra::app::gui_detail
