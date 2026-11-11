// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The text probe of the overflow test, see text_probe.h: the `--wrap`
// functions the linker puts between Dear ImGui's callers and its text
// drawing (Itanium names, the same on Linux and MinGW; the object of the
// member function is the first argument).
#include "text_probe.h"

#include <cfloat>
#include <cstring>

namespace yantra::test {
namespace {

struct Probe {
    bool on = false;
    bool linked = false;
    std::vector<DrawnText> texts;
    const char* last_begin = nullptr;  // of the last text (for its ellipsis)
    ImDrawList* last_list = nullptr;
    bool last_kept = false;  // the last text is texts.back()
};
Probe& probe() {
    static Probe p;
    return p;
}

ImGuiWindow* owner(ImDrawList* list) {
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (g == nullptr) return nullptr;
    if (g->CurrentWindow != nullptr && g->CurrentWindow->DrawList == list) return g->CurrentWindow;
    for (ImGuiWindow* w : g->Windows)
        if (w->DrawList == list) return w;
    return nullptr;
}

void record(ImDrawList* list, ImFont* font, float size, const ImVec2& pos, ImU32 col, const char* begin,
            const char* end, float wrap, const ImVec4* fine) {
    Probe& p = probe();
    p.linked = true;
    p.last_begin = begin;
    p.last_list = list;
    p.last_kept = false;
    if (!p.on || (col & IM_COL32_A_MASK) == 0 || begin == nullptr || begin == end || *begin == '\0') return;
    if (end == nullptr) end = begin + std::strlen(begin);
    if (font == nullptr) font = list->_Data->Font;
    if (size == 0.0F) size = list->_Data->FontSize;
    ImGuiWindow* w = owner(list);
    // A window measuring its contents in a hidden frame is not on screen.
    if (w != nullptr && (w->Hidden || w->HiddenFramesCannotSkipItems > 0 || w->HiddenFramesCanSkipItems > 0)) return;
    DrawnText t;
    t.text.assign(begin, end);
    // The box of what is seen: spaces before and after take no ink.
    const char* b = begin;
    const char* e = end;
    while (b < e && *b == ' ') ++b;
    while (e > b && (e[-1] == ' ' || e[-1] == '\n')) --e;
    if (b == e) return;
    // (A wrapped text is measured as Dear ImGui wraps it: from its start.)
    const float lead = b > begin && wrap <= 0.0F ? font->CalcTextSizeA(size, FLT_MAX, 0.0F, begin, b).x : 0.0F;
    const ImVec2 sz = wrap > 0.0F ? font->CalcTextSizeA(size, FLT_MAX, wrap, begin, e) : font->CalcTextSizeA(size, FLT_MAX, 0.0F, b, e);
    t.bounds = ImRect(ImVec2(pos.x + lead, pos.y), ImVec2(pos.x + lead + sz.x, pos.y + sz.y));
    ImVec4 c = list->_CmdHeader.ClipRect;
    if (fine != nullptr) {
        c.x = ImMax(c.x, fine->x);
        c.y = ImMax(c.y, fine->y);
        c.z = ImMin(c.z, fine->z);
        c.w = ImMin(c.w, fine->w);
    }
    t.clip = ImRect(c.x, c.y, c.z, c.w);
    t.list = list;
    t.window = w;
    if (w != nullptr) t.window_name = w->Name;
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    t.item = g.LastItemData.ID;
    t.item_rect = g.LastItemData.Rect;
    if (g.CurrentTable != nullptr && w == g.CurrentTable->InnerWindow) t.table_column = g.CurrentTable->CurrentColumn;
    p.texts.push_back(std::move(t));
    p.last_kept = true;
}

}  // namespace

void text_probe_start() {
    probe().texts.clear();
    probe().on = true;
}

std::vector<DrawnText> text_probe_stop() {
    probe().on = false;
    return std::move(probe().texts);
}

bool text_probe_linked() { return probe().linked; }

}  // namespace yantra::test

// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" {
void __real__ZN10ImDrawList7AddTextEP6ImFontfRK6ImVec2jPKcS6_fPK6ImVec4(ImDrawList*, ImFont*, float, const ImVec2&, ImU32,
                                                                       const char*, const char*, float, const ImVec4*);
void __wrap__ZN10ImDrawList7AddTextEP6ImFontfRK6ImVec2jPKcS6_fPK6ImVec4(ImDrawList* list, ImFont* font, float size,
                                                                       const ImVec2& pos, ImU32 col, const char* begin,
                                                                       const char* end, float wrap, const ImVec4* fine);
void __real__ZN10ImDrawList7AddTextERK6ImVec2jPKcS4_(ImDrawList*, const ImVec2&, ImU32, const char*, const char*);
void __wrap__ZN10ImDrawList7AddTextERK6ImVec2jPKcS4_(ImDrawList* list, const ImVec2& pos, ImU32 col, const char* begin,
                                                     const char* end);
void __real__ZN6ImFont10RenderCharEP10ImDrawListfRK6ImVec2jtPK6ImVec4(ImFont*, ImDrawList*, float, const ImVec2&, ImU32,
                                                                     ImWchar, const ImVec4*);
void __wrap__ZN6ImFont10RenderCharEP10ImDrawListfRK6ImVec2jtPK6ImVec4(ImFont* font, ImDrawList* list, float size,
                                                                     const ImVec2& pos, ImU32 col, ImWchar c,
                                                                     const ImVec4* fine);
}

void __wrap__ZN10ImDrawList7AddTextEP6ImFontfRK6ImVec2jPKcS6_fPK6ImVec4(ImDrawList* list, ImFont* font, float size,
                                                                       const ImVec2& pos, ImU32 col, const char* begin,
                                                                       const char* end, float wrap, const ImVec4* fine) {
    yantra::test::record(list, font, size, pos, col, begin, end, wrap, fine);
    __real__ZN10ImDrawList7AddTextEP6ImFontfRK6ImVec2jPKcS6_fPK6ImVec4(list, font, size, pos, col, begin, end, wrap, fine);
}

void __wrap__ZN10ImDrawList7AddTextERK6ImVec2jPKcS4_(ImDrawList* list, const ImVec2& pos, ImU32 col, const char* begin,
                                                     const char* end) {
    yantra::test::record(list, nullptr, 0.0F, pos, col, begin, end, 0.0F, nullptr);
    __real__ZN10ImDrawList7AddTextERK6ImVec2jPKcS4_(list, pos, col, begin, end);
}

void __wrap__ZN6ImFont10RenderCharEP10ImDrawListfRK6ImVec2jtPK6ImVec4(ImFont* font, ImDrawList* list, float size,
                                                                     const ImVec2& pos, ImU32 col, ImWchar c,
                                                                     const ImVec4* fine) {
    auto& p = yantra::test::probe();
    if (p.on && c == font->EllipsisChar && p.last_list == list && p.last_begin != nullptr) {
        // RenderTextEllipsis: the text just drawn was shortened (or nothing
        // of it fits); its whole label runs to the end of the string or a
        // "##" id.
        if (!p.last_kept) {
            yantra::test::DrawnText t;
            t.list = list;
            t.window = yantra::test::owner(list);
            if (t.window != nullptr) t.window_name = t.window->Name;
            t.bounds = ImRect(pos, pos);
            t.clip = t.bounds;
            p.texts.push_back(std::move(t));
            p.last_kept = true;
        }
        yantra::test::DrawnText& t = p.texts.back();
        t.ellipsis = true;
        t.ellipsis_at = pos;
        t.full = std::string(p.last_begin, ImGui::FindRenderedTextEnd(p.last_begin));
    }
    __real__ZN6ImFont10RenderCharEP10ImDrawListfRK6ImVec2jtPK6ImVec4(font, list, size, pos, col, c, fine);
}
// NOLINTEND(bugprone-reserved-identifier)
