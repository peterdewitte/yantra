// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The run-time half of the UI lint (DOC-20; tools/lint_ui is the
// source half): for one drawn frame it reports
//   - a control without a hover text (DOC-07 b),
//   - a disabled control without the reason (DOC-07 c),
//   - controls that overlap, are cut off at their window's edge, are
//     squashed or show only part of their label (also run by the GUI
//     text-overflow test TST-52 at every size, scale and theme),
//   - in the touch layout, controls lower than 48 px (UI-13),
//   - texts tr() found no translation for in the language (UI-12, DOC-07 f).
// A control is every item the probe saw with a label (buttons, fields,
// switches, menu items, tabs, rows); its hover text is found by the
// rectangle the Gui's tooltip helpers note (gui_widgets.h help_record).
#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "gui_widgets.h"
#include "ui_harness.h"
#include "yantra/app/i18n.h"

namespace yantra::test {

/// Items that are not controls with a hover text of their own: the plan
/// canvas (its tools say what a click does), the status bar's area.
inline constexpr std::string_view kNoTooltip[] = {"plan.canvas", "status_bar", "help.text"};

struct UiLint {
    UiLint(UiHarness& harness, bool touch_layout) : ui(harness), touch(touch_layout) {}
    UiHarness& ui;
    bool touch = false;
    std::vector<std::string> issues;
    std::set<std::string> seen;  // each issue once
    int frames_checked = 0;
    int items_checked = 0;
    /// TST-52 (tests/app/overflow): called just before the checked frame is
    /// drawn, and after it with the frame's hover texts.
    std::function<void()> before_frame;
    std::function<void(const std::string&, const app::gui_detail::HelpRecord&)> after_frame;

    void add(const std::string& where, const std::string& what) {
        if (seen.insert(where + ": " + what).second) issues.push_back(where + ": " + what);
    }

    /// Draws one frame with the hover texts recorded and checks it.
    void check(const std::string& where) {
        app::gui_detail::HelpRecord rec;
        app::record_missing_translations(app::language() != app::Language::En);
        (void)app::take_missing_translations();
        app::gui_detail::help_record() = &rec;
        if (before_frame) before_frame();
        const bool ok = ui.frame();
        app::gui_detail::help_record() = nullptr;
        app::record_missing_translations(false);
        if (!ok) {
            add(where, "the frame failed: " + ui.last_error());
            return;
        }
        ++frames_checked;
        for (const std::string& m : app::take_missing_translations())
            add(where, std::string("no ") + app::language_key(app::language()) + " translation: \"" + m + "\"");
        analyse(where, rec);
        if (after_frame) after_frame(where, rec);
    }

private:
    static bool inside(const ImRect& r, float x, float y) {
        return x >= r.Min.x - 0.5F && x <= r.Max.x + 0.5F && y >= r.Min.y - 0.5F && y <= r.Max.y + 0.5F;
    }

    void analyse(const std::string& where, const app::gui_detail::HelpRecord& rec) {
        const auto& items = ui.items();
        std::vector<const app::ProbeItem*> controls;
        for (const app::ProbeItem& it : items) {
            // Controls with a stable id; windows, child regions, tab bars
            // and table headers are containers or plain text.
            if (it.label.find("###") == std::string::npos || it.label.rfind("###", 0) == 0) continue;
            const std::string_view key = it.key();
            if (key.find('/') != std::string_view::npos || key == window_key(it.window)) continue;
            // The menu bar's titles name their menu; their entries have the texts.
            if (it.window == "##MainMenuBar") continue;
            if (std::find(std::begin(kNoTooltip), std::end(kNoTooltip), key) != std::end(kNoTooltip)) continue;
            controls.push_back(&it);
        }
        for (const app::ProbeItem* it : controls) {
            ++items_checked;
            ImGuiWindow* w = ImGui::FindWindowByName(it->window.c_str());
            const std::string name = std::string(it->key()) + " in " + std::string(window_key(it->window));
            if (w == nullptr) continue;
            // Only what can be seen: items scrolled out of view are drawn later.
            const ImRect clip = w->InnerClipRect;
            if (it->y1 < clip.Min.y || it->y0 > clip.Max.y || clip.GetWidth() <= 0.0F) continue;
            bool covered = false, reason = false;
            for (const auto& e : rec.entries) {
                if (e.window != w->ID || !inside(e.rect, it->cx(), it->cy())) continue;
                covered = true;
                reason = reason || e.reason;
            }
            if (!covered) add(where, "no tooltip: " + name);
            else if (it->disabled && !reason) add(where, "disabled without a reason: " + name);
            const float wd = it->x1 - it->x0, ht = it->y1 - it->y0;
            if (wd < 6.0F || ht < 6.0F) add(where, "squashed: " + name);
            // A label wider than its control is cut off or shortened with an
            // ellipsis (a squashed tab, a button narrower than its text).
            // (A text field draws its label beside the field, outside the item.)
            else if (const float tw = label_width(it->label);
                     (it->status & ImGuiItemStatusFlags_Inputable) == 0 && tw > wd + 0.5F)
                add(where, "label cut off (" + std::to_string(static_cast<int>(tw)) + " px text in " +
                               std::to_string(static_cast<int>(wd)) + " px): " + name);
            if (touch && ht < static_cast<float>(app::kTouchTargetPx) - 0.5F)
                add(where, "lower than 48 px for touch (" + std::to_string(static_cast<int>(ht)) + "): " + name);
            // (A selectable reaches half the item spacing past its row.)
            const float slack = ImGui::GetStyle().ItemSpacing.x * 0.5F + 1.5F;
            if (w->ScrollMax.x <= 0.0F && (it->x1 > clip.Max.x + slack || it->x0 < clip.Min.x - slack))
                add(where, "cut off at the window's edge: " + name);
        }
        for (std::size_t i = 0; i < controls.size(); ++i)
            for (std::size_t j = i + 1; j < controls.size(); ++j) {
                const app::ProbeItem& a = *controls[i];
                const app::ProbeItem& b = *controls[j];
                if (a.window != b.window) continue;
                const float ox = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
                const float oy = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
                // Rows of a list may share the spacing between them.
                const ImVec2 sp = ImGui::GetStyle().ItemSpacing;
                if (ox > std::max(1.5F, sp.x) && oy > std::max(1.5F, sp.y))
                    add(where, "overlap: " + std::string(a.key()) + " and " + std::string(b.key()) + " in " +
                                   std::string(window_key(a.window)));
            }
    }

    /// The width of a label's visible text ("Text##x###id" -> "Text").
    static float label_width(const std::string& label) {
        const std::size_t end = label.find("##");
        const std::string text = label.substr(0, end);
        return text.empty() ? 0.0F : ImGui::CalcTextSize(text.c_str()).x;
    }

public:
    static std::string_view window_key(std::string_view name) {
        // "Title###win.x/child_1234" -> "win.x/child_1234"
        const std::size_t h = name.find("###");
        return h == std::string_view::npos ? name : name.substr(h + 3);
    }
};

}  // namespace yantra::test
