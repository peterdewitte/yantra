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
#include <cmath>
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

/// A non-modal tool window (Network and Window menus), placed and sized
/// the first time (sizes for the 13 px font); its title is translated, its
/// id "###<id>" stable. End it with ImGui::End() in every case.
inline bool begin_tool_window(const char* title, const char* id, bool& open, float x, float y, float w, float h) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + x, vp->WorkPos.y + y), ImGuiCond_FirstUseEver);
    w = std::min(em(w), vp->WorkSize.x * 0.95F);
    h = em(h);
    ImGui::SetNextWindowSize(ImVec2(w, std::max(std::min(em(320.0F), vp->WorkSize.y * 0.6F), std::min(h, vp->WorkSize.y - y - 20.0F))),
                             ImGuiCond_FirstUseEver);
    const std::string name = std::string(title) + "###" + id;
    const bool shown = ImGui::Begin(name.c_str(), &open, ImGuiWindowFlags_NoSavedSettings);
    if (shown) wrap_texts();
    return shown;
}

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

/// DEMO-15: the colours of a demo explanation (the tour's window, its strip
/// in a dialog, its offer on the start screen, the mouse-survey hint on the
/// plan): a green background in every theme, with text colours of at least
/// 4.5:1 contrast on it (checked by the UI lint, tests/app/ui/ui_lint.h).
/// The DEMO banner (DEMO-07) keeps its own colour.
struct DemoColours {
    ImVec4 bg, text, muted, warn, error, ok;
};
inline DemoColours demo_colours(Theme theme) {
    auto c = [](int r, int g, int b) { return ImVec4(r / 255.0F, g / 255.0F, b / 255.0F, 1.0F); };
    if (theme_is_light(theme))
        return {c(215, 242, 220), c(11, 41, 18), c(46, 79, 54), c(107, 69, 0), c(160, 21, 21), c(11, 93, 30)};
    // Dark and high contrast.
    return {c(24, 84, 44), c(255, 255, 255), c(200, 230, 201), c(255, 213, 79), c(255, 205, 210), c(185, 246, 202)};
}
/// The WCAG contrast ratio of two opaque colours (1 to 21).
inline double contrast_ratio(const ImVec4& a, const ImVec4& b) {
    auto lum = [](const ImVec4& c) {
        auto f = [](float v) { return v <= 0.03928F ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * f(c.x) + 0.7152 * f(c.y) + 0.0722 * f(c.z);
    };
    const double la = lum(a), lb = lum(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
/// The demo explanations drawn in a frame, with their background and the
/// text colours drawn on it (the UI lint checks them when it sets a record).
struct DemoRecord {
    struct Box {
        std::string what;
        ImVec4 bg;
        std::vector<ImVec4> texts;
    };
    std::vector<Box> boxes;
};
inline DemoRecord*& demo_record() {
    static DemoRecord* r = nullptr;
    return r;
}
/// Notes a demo explanation drawn with `c` (and the style's text colours
/// in use, which the box pushes).
inline void note_demo_box(const char* what, const DemoColours& c) {
    if (DemoRecord* r = demo_record())
        r->boxes.push_back({what, c.bg,
                            {ImGui::GetStyleColorVec4(ImGuiCol_Text), ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                             c.text, c.muted, c.warn, c.error, c.ok}});
}
/// Text colours of a demo explanation (pop with ImGui::PopStyleColor(2)).
inline void push_demo_text(const DemoColours& c) {
    ImGui::PushStyleColor(ImGuiCol_Text, c.text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, c.muted);
}

/// UI lint (DOC-20) and the GUI text-overflow test (TST-52): when a test
/// sets a record, every hover text notes the rectangle of the item it
/// belongs to, its text and whether it says why the item is disabled, so
/// the lint can find controls without one; with `show` set, the hover text
/// of entry number `show` is drawn as if hovered (the test draws every
/// tooltip and "(?)" text in turn). With `show_text` set instead, the
/// first entry with that text is drawn: a live list (the radio debug
/// window's records) can gain a row between two frames, which moves the
/// numbers of the entries after it but not their texts.
struct HelpRecord {
    struct Entry {
        ImRect rect;
        ImGuiID window = 0;
        bool reason = false;
        std::string text;
    };
    std::vector<Entry> entries;
    int show = -1;
    std::string show_text;
    bool shown = false;  ///< `show_text` was drawn in this frame
    /// Before the next frame: no entries, nothing drawn yet.
    void next_frame() {
        entries.clear();
        shown = false;
    }
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
    if (!r->show_text.empty()) {
        const bool now = !r->shown && t == r->show_text;
        r->shown = r->shown || now;
        r->entries.push_back({g.LastItemData.Rect, g.CurrentWindow != nullptr ? g.CurrentWindow->ID : 0, reason, std::move(t)});
        return now;
    }
    r->entries.push_back({g.LastItemData.Rect, g.CurrentWindow != nullptr ? g.CurrentWindow->ID : 0, reason, std::move(t)});
    return r->show == static_cast<int>(r->entries.size()) - 1;
}

/// True while a test shows one recorded hover text on purpose: then no
/// other item's hover text joins it in the tooltip window (a list that
/// moves under the mouse would add the row's text to the same window).
inline bool help_shown_on_purpose() {
    const HelpRecord* r = help_record();
    return r != nullptr && (r->show >= 0 || !r->show_text.empty());
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
    if (!shown && (help_shown_on_purpose() || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)))
        return;
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
    if (!shown && (help_shown_on_purpose() || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)))
        return;
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
    if (shown || (!help_shown_on_purpose() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))) {
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

/// input_text with a hint shown while the field is empty: a field
/// without a label of its own says what it is for (DOC-07).
inline bool input_text_hint(const char* label, const char* hint, std::string& s, std::size_t capacity = 1024) {
    std::string buf = s;
    buf.resize(std::max(capacity, s.size() + 1), '\0');
    const bool changed = ImGui::InputTextWithHint(label, hint, buf.data(), buf.size());
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
