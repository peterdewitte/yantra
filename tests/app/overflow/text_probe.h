// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every text Dear ImGui draws in a frame, for the GUI text-overflow test
// (TST-52): its text, its bounds, the clipping rectangle it was drawn
// with (the control's, the table column's or the window's), the window
// and the item it belongs to, and whether it was shortened with an
// ellipsis. Dear ImGui (vendored, not changed) has no hook for text, so
// the test executable is linked with `--wrap` on ImDrawList::AddText (both
// overloads) and ImFont::RenderChar (the ellipsis of RenderTextEllipsis):
// every widget, table header, menu, tooltip and the app's own draw-list
// texts go through them. Only this test binary is linked so; the app is
// not (tests/app/overflow/CMakeLists.txt). The contents of text fields
// (ImFont::RenderText from InputText) are not recorded: they scroll in
// their field by design.
#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <vector>

namespace yantra::test {

struct DrawnText {
    std::string text;     ///< as drawn (shortened when `ellipsis`)
    std::string full;     ///< with an ellipsis: the whole label
    ImRect bounds;        ///< the drawn text's box (spaces around it left out)
    ImRect clip;          ///< the clipping rectangle it was drawn with
    ImDrawList* list = nullptr;
    ImGuiWindow* window = nullptr;  ///< owner of `list` (nullptr: fore/background)
    std::string window_name;
    ImGuiID item = 0;     ///< the last item submitted when it was drawn (0: plain text)
    ImRect item_rect;
    int table_column = -1;
    bool ellipsis = false;
    ImVec2 ellipsis_at;
};

/// Starts recording the texts drawn from now on (clears the list).
void text_probe_start();
/// Stops recording; the texts drawn since the start.
std::vector<DrawnText> text_probe_stop();
/// True when the executable was linked with the wraps (the probe works).
bool text_probe_linked();

}  // namespace yantra::test
