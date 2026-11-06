// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A record of the items Dear ImGui submitted in a frame (id, label,
// rectangle, window, disabled), for the headless UI tests (TST-50,
// tests/app/harness) and later the layout check of the screenshot mode
// (TST-50, SCR-01). It uses Dear ImGui's test-engine item hooks
// (IMGUI_ENABLE_TEST_ENGINE, set for yantra_imgui): Yantra implements the
// hooks here itself; no Dear ImGui Test Engine code is used. Without an
// attached probe the hooks are off (ImGuiContext::TestEngineHookItems is
// false) and cost nothing, so the app ships the same library.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiContext;

namespace yantra::app {

struct ProbeItem {
    std::uint32_t id = 0;  ///< ImGuiID
    std::string label;     ///< the full label, e.g. "Open project...###file.open_project"; "" if none
    std::string window;    ///< name of the window the item is in
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  ///< screen rectangle
    bool disabled = false;
    std::uint32_t status = 0;  ///< ImGuiItemStatusFlags (checkable/checked/openable/...)

    /// The stable part of the label: after "###" if present, else the label.
    std::string_view key() const;
    float cx() const { return (x0 + x1) * 0.5F; }
    float cy() const { return (y0 + y1) * 0.5F; }
};

class UiProbe {
public:
    /// Attaches to `ctx` (one probe per context): from the next item on,
    /// every item is recorded into the current frame's list.
    explicit UiProbe(ImGuiContext* ctx);
    ~UiProbe();
    UiProbe(const UiProbe&) = delete;
    UiProbe& operator=(const UiProbe&) = delete;

    /// Starts a new frame's list (call before ImGui::NewFrame); the last
    /// complete frame stays readable through items().
    void begin_frame();
    /// Ends the frame (after ImGui::Render): its list becomes items().
    void end_frame();
    /// The items of the last complete frame, in submission order.
    const std::vector<ProbeItem>& items() const { return done_; }

    // Called by the hooks.
    void add(std::uint32_t id, float x0, float y0, float x1, float y1, bool disabled, const char* window);
    void info(std::uint32_t id, const char* label, std::uint32_t status);

private:
    ImGuiContext* ctx_;
    std::vector<ProbeItem> current_;
    std::vector<ProbeItem> done_;
};

}  // namespace yantra::app
