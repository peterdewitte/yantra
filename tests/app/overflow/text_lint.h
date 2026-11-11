// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The checks of the GUI text-overflow test (TST-52) on one drawn frame:
// every text the probe saw (text_probe.h) must lie inside the rectangle
// it is clipped to (its control, table column or window), unless that
// edge is one of a window that scrolls in that direction; inside the
// screen; inside the control it is the label of; not over another text
// or another control; and a text shortened with an ellipsis must have its
// whole text in the control's tooltip. Tooltips and "(?)" texts are drawn
// in turn (HelpRecord::show) and checked the same way. A failure names
// the place, window, item, text and the overflow in pixels; the frame is
// saved as a PNG (a small software rasteriser of Dear ImGui's draw data,
// the failing texts outlined in red) next to the test's scratch folder.
#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "gui_widgets.h"
#include "text_probe.h"
#include "ui_harness.h"

namespace yantra::test {

class TextLint {
public:
    /// `config`: language, layout, theme, size and scale, in every message.
    TextLint(UiHarness& ui, std::string config, std::filesystem::path shots);

    /// Checks the texts of the frame just drawn (recorded by the probe;
    /// `help`: the hover texts of the same frame).
    void analyse(const std::string& where, const std::vector<DrawnText>& texts, const app::gui_detail::HelpRecord& help);
    /// Draws every hover text of `help` not drawn before in this run (by
    /// text) as a tooltip, two frames each, and checks it.
    void tooltips(const std::string& where, const app::gui_detail::HelpRecord& help);

    std::vector<std::string> issues;
    int frames = 0;
    int texts = 0;
    int tooltips_drawn = 0;

private:
    void add(const std::string& where, const std::string& what);
    void shot(const std::string& where, const std::vector<ImRect>& marks);
    UiHarness& ui_;
    std::string config_;
    std::filesystem::path shots_;
    std::set<std::string> seen_;
    std::set<std::string> tips_done_;
    int shots_taken_ = 0;
    std::size_t issues_before_ = 0;
};

/// Renders the current frame's draw data into RGBA pixels (user textures
/// grey) with red rectangles around `marks`, and writes a PNG.
bool write_frame_png(const std::filesystem::path& file, int width, int height, const std::vector<ImRect>& marks);

}  // namespace yantra::test
