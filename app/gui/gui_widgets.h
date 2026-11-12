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
#include <cstring>
#include <initializer_list>
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
/// A size given for the 13 px font, at the font size in use: the UI scale
/// and the touch layout's larger font (window and panel widths that must
/// hold text, TST-52).
inline float em(float v) { return v * ImGui::GetFontSize() / 13.0F; }
/// The height of a large button: `desktop` x kTouchTarget on the desktop,
/// at least kTouchTarget in the touch layout (UI-13), at the UI scale.
inline float target_h(float desktop = 0.8F) {
    const float h = kTouchTarget * (layout().touch ? std::max(1.0F, desktop) : desktop) * layout().scale;
    return std::max(h, ImGui::GetFrameHeight());
}
/// TST-52: the plain texts of the current window wrap at its right edge
/// (in a table at their column's) instead of running past it. Called right
/// after Begin/BeginChild; Dear ImGui clears the window's wrap stack at its
/// next Begin, so there is no matching pop. Not for auto-resizing windows
/// (menus, tooltips), whose width the texts make.
inline void wrap_texts() { ImGui::PushTextWrapPos(0.0F); }

/// A one-line text shortened with "..." to `width` when it is longer, with
/// its whole text on hover then (TST-52); `colour` 0 = the text colour.
inline void text_ellipsis(const char* text, float width, ImU32 colour = 0) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float w = std::max(1.0F, std::min(width, ts.x));
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    if (colour != 0) ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + h), p.x + w, text, nullptr, &ts);
    if (colour != 0) ImGui::PopStyleColor();
}

/// A full-width selectable row showing `text`, shortened with "..." when
/// the row is narrower; `cut` tells whether it was, so the hover text can
/// start with the whole text (TST-52). `id` is the "###id" part.
inline bool selectable_ellipsis(const std::string& text, const std::string& id, bool selected, float height, bool& cut) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const bool pressed = ImGui::Selectable(id.c_str(), selected, 0, ImVec2(0.0F, std::max(height, ts.y)));
    cut = ts.x > w;
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + std::max(height, ts.y)), p.x + w,
                              text.c_str(), nullptr, &ts);
    return pressed;
}

/// The value of a "label  value" row at `x` (window coordinates), or just
/// after a label that is longer than that (TST-52: never over the label).
inline void value_column(float x) {
    const float label_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX();
    ImGui::SameLine(std::max(x, label_end + ImGui::GetStyle().ItemSpacing.x));
}

/// UI-13, TST-52: puts the next item on the same line when `width` still
/// fits there, else on a new line (rows of buttons and switches that wrap
/// in narrow panels, long translations and large UI scales).
inline void same_line_if(float width) {
    const ImVec2 last = ImGui::GetItemRectMax();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (last.x + ImGui::GetStyle().ItemSpacing.x + width <= right) ImGui::SameLine();
}
/// The width of a radio button or checkbox with its label.
inline float switch_w(const char* label) {
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x;
}
/// The width of a combo box whose preview shows the longest of `items`
/// (at most the room left on the line).
inline float combo_w(std::initializer_list<const char*> items) {
    float w = 0.0F;
    for (const char* i : items) w = std::max(w, ImGui::CalcTextSize(i, nullptr, true).x);
    return std::min(w + ImGui::GetStyle().FramePadding.x * 2.0F + ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().x);
}
/// The width of a button that fits its label.
inline float label_w(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0F;
}
/// The width of a number field with its - and + buttons (as wide as the
/// field is high; the touch layout's are larger) that shows `widest`.
inline float step_field_w(const char* widest) {
    return ImGui::CalcTextSize(widest).x + ImGui::GetStyle().FramePadding.x * 2.0F +
           2.0F * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
}
/// A button width of at least `min_w` (at the UI scale) that fits its
/// (translated) label.
inline float button_w(const char* label, float min_w) {
    const float text = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0F;
    return std::max(px(min_w), text);
}
inline const ImVec4 kBannerColour{0.95F, 0.62F, 0.07F, 1.0F};
inline const ImVec4 kErrorColour{1.0F, 0.45F, 0.40F, 1.0F};

/// UI lint (DOC-20) and the GUI text-overflow test (TST-52): when a test
/// sets a record, every hover text notes the rectangle of the item it
/// belongs to, its text and whether it says why the item is disabled, so
/// the lint can find controls without one; with `show` set, the hover text
/// of entry number `show` is drawn as if hovered (the test draws every
/// tooltip and "(?)" text in turn).
struct HelpRecord {
    struct Entry {
        ImRect rect;
        ImGuiID window = 0;
        bool reason = false;
        std::string text;
    };
    std::vector<Entry> entries;
    int show = -1;
};
inline HelpRecord*& help_record() {
    static HelpRecord* r = nullptr;
    return r;
}
/// Notes the last item's hover text; true when the test shows it now.
inline bool note_help(bool reason, const char* text = nullptr, const char* text2 = nullptr) {
    HelpRecord* r = help_record();
    if (r == nullptr) return false;
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    std::string t = text != nullptr ? text : "";
    if (text2 != nullptr && *text2 != '\0') t += std::string("\n") + text2;
    r->entries.push_back({g.LastItemData.Rect, g.CurrentWindow != nullptr ? g.CurrentWindow->ID : 0, reason, std::move(t)});
    return r->show == static_cast<int>(r->entries.size()) - 1;
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
    const bool shown = note_help(!a.enabled && !a.disabled_reason.empty(), a.tooltip.c_str(),
                                 a.enabled ? nullptr : a.disabled_reason.c_str());
    if (!tooltips_shown() && a.enabled) return;
    if (!shown && !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) return;
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
    // On a disabled item the text is written to say why (DOC-07 c).
    const bool shown = note_help((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0, text);
    if (!tooltips_shown()) return;
    if (!shown && !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/// A table header with its hover text. When the column is narrower than
/// the name, Dear ImGui shortens it with "..." and the hover text starts
/// with the whole name (TST-52).
inline void table_header(const char* label, const char* tooltip = nullptr) {
    const char* end = ImGui::FindRenderedTextEnd(label);
    const bool cut = ImGui::CalcTextSize(label, end).x > ImGui::GetContentRegionAvail().x;
    ImGui::TableHeader(label);
    std::string tip = tooltip != nullptr ? tooltip : "";
    if (cut) tip = std::string(label, end) + (tip.empty() ? "" : "\n" + tip);
    if (!tip.empty()) text_tooltip(tip.c_str());
}
/// ImGui::TableHeadersRow() with table_header(): the whole name of a
/// shortened column header on hover.
inline void headers_row() {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    const int n = ImGui::TableGetColumnCount();
    for (int i = 0; i < n; ++i) {
        if (!ImGui::TableSetColumnIndex(i)) continue;
        ImGui::PushID(i);
        table_header(ImGui::TableGetColumnName(i));
        ImGui::PopID();
    }
}

/// The height of a list row or menu entry in the touch layout (48 px
/// with the spacing, UI-13); 0 (the text's height) on the desktop.
inline float row_h() {
    if (!layout().touch) return 0.0F;
    return std::max(0.0F, static_cast<float>(kTouchTargetPx) * layout().scale - ImGui::GetStyle().ItemSpacing.y + 0.5F);
}
/// SmallButton on the desktop; a full touch target in the touch layout.
inline bool small_button(const char* label) {
    if (!layout().touch) return ImGui::SmallButton(label);
    return ImGui::Button(label, ImVec2(0.0F, target_h()));
}

/// A tab with its hover text (DOC-07 b); true when its page shows.
inline bool tab_item(const char* label, const char* tooltip, ImGuiTabItemFlags flags = 0) {
    const bool open = ImGui::BeginTabItem(label, nullptr, flags);
    text_tooltip(tooltip);
    return open;
}
/// A collapsing header with its hover text (DOC-07 b).
/// A name wider than the header is shortened with "..." and the hover
/// text starts with the whole name (TST-52; Dear ImGui would cut it).
inline bool section_header(const char* label, const char* tooltip, ImGuiTreeNodeFlags flags = 0) {
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImGuiStyle& st = ImGui::GetStyle();
    // As Dear ImGui places a framed header's text: after the arrow.
    const float text_x = ImGui::GetFontSize() + st.FramePadding.x * 3.0F;
    const ImVec2 size = ImGui::CalcTextSize(label, end);
    if (size.x <= ImGui::GetContentRegionAvail().x - text_x - st.FramePadding.x) {
        const bool open = ImGui::CollapsingHeader(label, flags);
        text_tooltip(tooltip);
        return open;
    }
    const char* id = std::strstr(label, "###");
    const std::string hidden = id != nullptr ? std::string(id) : "###" + std::string(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool open = ImGui::CollapsingHeader(hidden.c_str(), flags);
    const float y = p.y + st.FramePadding.y;
    const float right = ImGui::GetItemRectMax().x - st.FramePadding.x;
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(p.x + text_x, y), ImVec2(right, y + size.y), right, label, end,
                              &size);
    const std::string tip = std::string(label, end) + (tooltip != nullptr && *tooltip != '\0' ? "\n" + std::string(tooltip) : "");
    text_tooltip(tip.c_str());
    return open;
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
    same_line_if(ImGui::CalcTextSize("(?)").x);
    ImGui::TextDisabled("(?)");
    const bool shown = note_help(false, text);
    if (shown || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
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
