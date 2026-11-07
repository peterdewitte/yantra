// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small widgets shared by the ImGui layer's files (gui.cpp, gui_tools.cpp):
// tooltips on every control and on disabled ones with the reason (DOC-07
// b, c), "(?)" help markers (DOC-07 a), text input on std::string, dialog
// buttons of touch size (UI-13), the error line of a dialog.
#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <string>
#include <vector>

#include "yantra/app/app_state.h"

namespace yantra::app::gui_detail {

inline constexpr float kTouchTarget = 48.0F;  // UI-13: >= 48 px targets

/// The layout in use (Gui::apply_style): touch sizes, the UI scale, the theme.
struct Layout {
    bool touch = false;
    float scale = 1.0F;
    Theme theme = Theme::Dark;
};
inline Layout& layout() {
    static Layout l;
    return l;
}
inline void set_layout(bool touch, float scale, Theme theme) { layout() = Layout{touch, scale, theme}; }
/// A size in pixels at the UI scale.
inline float px(float v) { return v * layout().scale; }
/// The height of a large button: `desktop` x kTouchTarget on the desktop,
/// at least kTouchTarget in the touch layout (UI-13), at the UI scale.
inline float target_h(float desktop = 0.8F) {
    const float h = kTouchTarget * (layout().touch ? std::max(1.0F, desktop) : desktop) * layout().scale;
    return std::max(h, ImGui::GetFrameHeight());
}
/// A button width of at least `min_w` (at the UI scale) that fits its
/// (translated) label.
inline float button_w(const char* label, float min_w) {
    const float text = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0F;
    return std::max(px(min_w), text);
}
inline const ImVec4 kBannerColour{0.95F, 0.62F, 0.07F, 1.0F};
inline const ImVec4 kErrorColour{1.0F, 0.45F, 0.40F, 1.0F};

/// UI lint (DOC-20, TST-50): when a test sets a record, every hover text
/// notes the rectangle of the item it belongs to (and whether it says why
/// the item is disabled), so the lint can find controls without one.
struct HelpRecord {
    struct Entry {
        ImRect rect;
        ImGuiID window = 0;
        bool reason = false;
    };
    std::vector<Entry> entries;
};
inline HelpRecord*& help_record() {
    static HelpRecord* r = nullptr;
    return r;
}
inline void note_help(bool reason) {
    HelpRecord* r = help_record();
    if (r == nullptr) return;
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    r->entries.push_back({g.LastItemData.Rect, g.CurrentWindow != nullptr ? g.CurrentWindow->ID : 0, reason});
}

/// Tooltips on or off (settings, DOC-07 h). Off hides the hover texts;
/// the "(?)" markers and the reasons of disabled controls stay.
inline bool& tooltips_shown() {
    static bool shown = true;
    return shown;
}
inline void set_tooltips_shown(bool on) { tooltips_shown() = on; }

/// The hover text of the last item, also when it is disabled (DOC-07 b, c).
inline void item_tooltip(const ActionView& a) {
    note_help(!a.enabled && !a.disabled_reason.empty());
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
    note_help(false);
    if (!tooltips_shown()) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/// The hover text of the last item, and why it is disabled when it is
/// (DOC-07 b, c): for controls inside BeginDisabled(!enabled).
inline void disabled_tooltip(const char* tooltip, bool enabled, const std::string& reason) {
    ActionView a;
    a.tooltip = tooltip;
    a.enabled = enabled;
    a.disabled_reason = reason;
    item_tooltip(a);
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
    const bool pressed = ImGui::Button(label, ImVec2(button_w(label, width), target_h()));
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
