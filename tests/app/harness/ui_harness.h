// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Headless UI test harness (TST-50, TST-04 groundwork) for the app's ImGui
// layer, shared by the UI tests of every app step (9.1..9.11). It runs a
// real Dear ImGui context without a window system or OpenGL: a fixed
// display size, a fixed time step, textures "uploaded" by marking them
// ready, and Yantra's item probe (app/gui/ui_probe.h) recording every
// item of every frame. Tests drive it with mouse and key events exactly
// as a user would (move, press, release), so a click goes through ImGui's
// own hit testing, popups, disabled state and focus rules.
//
//   UiHarness ui;                       // 1280 x 800, 30 frames/s
//   AppState state(...); Gui gui(state);
//   ui.set_draw([&] { gui.draw(); });
//   ui.frames(2);
//   REQUIRE(ui.click("file.new_project"));   // menu item via its "###" id
//   REQUIRE(ui.wait_window("dlg.new_project"));
//
// Items and windows are found by their stable key: the part after "###"
// in the label (gui.h: "<label>###<id>"), else the whole label. Menu
// items need their menu open first: click_menu("file", "file.save").
// Everything is synchronous and frame-counted; nothing waits on time.
// Only items with an ImGui id are recorded (buttons, fields, menus,
// selectables, windows); plain Text() has none, so check text through the
// AppState view the GUI draws from.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui_probe.h"

struct ImGuiContext;

namespace yantra::test {

class UiHarness {
public:
    struct Options {
        float width = 1280.0F;
        float height = 800.0F;
        float frame_s = 1.0F / 30.0F;  ///< io.DeltaTime of every frame
    };
    UiHarness();
    explicit UiHarness(Options options);
    ~UiHarness();
    UiHarness(const UiHarness&) = delete;
    UiHarness& operator=(const UiHarness&) = delete;

    /// The code that draws one frame (between NewFrame and Render).
    void set_draw(std::function<void()> draw);
    /// Changes the display size (small windows, display scaling, UI-13).
    void set_display_size(float width, float height);

    /// Runs one frame / n frames. Returns false when the draw code threw
    /// or ImGui reported an error (the message is in last_error()).
    bool frame();
    bool frames(int n);
    /// Runs frames until `done` is true (checked after each frame), at
    /// most `max_frames`; false when it never was.
    bool run_until(const std::function<bool()>& done, int max_frames = 60);

    // -- queries on the last complete frame -----------------------------
    const std::vector<app::ProbeItem>& items() const;
    /// The last item with this key ("file.new_project", "dlg.ok", or a
    /// whole label without "###"); nullopt when not drawn last frame. With
    /// `window_key`, only items in that window or popup (or its child
    /// windows): its name has that key.
    std::optional<app::ProbeItem> find(std::string_view key, std::string_view window_key = {}) const;
    bool has(std::string_view key, std::string_view window_key = {}) const { return find(key, window_key).has_value(); }
    /// Keys of every item of the last frame (debug output of failing tests).
    std::string dump() const;
    /// A window (or popup) with this key was active in the last frame.
    bool window_open(std::string_view key) const;
    /// Runs frames until the window is open with its items drawn / until
    /// it is closed (at most 30). `key` names a top-level window or popup.
    bool wait_window(std::string_view key);
    bool wait_window_closed(std::string_view key);

    // -- input ----------------------------------------------------------
    void mouse_move(float x, float y);
    /// A left click at a display position (e.g. on empty space, which
    /// closes an open menu as it does for a user).
    void click_at(float x, float y);
    /// Presses a mouse button (0 left, 1 right, 2 middle) at (x0, y0),
    /// moves to (x1, y1) in `steps` frames and releases it (pan, draw).
    void drag(float x0, float y0, float x1, float y1, int button = 0, int steps = 4);
    /// The left button goes down at (x, y) (and stays down) or up, one
    /// frame each; `touch`: the events come from a touch screen (IND-48
    /// long press).
    void press(float x, float y, bool touch = false);
    void release(bool touch = false);
    /// Turns the mouse wheel by `delta` notches at (x, y) (zoom).
    void wheel(float x, float y, float delta);
    /// Hovers the item (moves the mouse to its centre and runs frames
    /// until the hover delay of tooltips passed). False if not found.
    bool hover(std::string_view key, std::string_view window_key = {}, int frames = 30);
    /// Clicks the item: moves the mouse to its centre, presses and
    /// releases the left button, each in its own frame. False when the
    /// item is not drawn or disabled (a disabled item is not clicked).
    bool click(std::string_view key, std::string_view window_key = {});
    /// Opens a menu of the main menu bar ("menu.<menu>") and clicks one of
    /// its items.
    bool click_menu(std::string_view menu, std::string_view item);
    /// A key press and release (with modifiers, e.g. ImGuiMod_Ctrl).
    void key(int imgui_key, int mods = 0);
    /// Types text into the focused text field.
    void type(std::string_view text);
    /// Clicks a text field, selects its text and types `text` over it.
    bool fill(std::string_view key, std::string_view text, std::string_view window_key = {});

    const std::string& last_error() const { return error_; }
    ImGuiContext* context() const { return ctx_; }

private:
    void serve_textures();
    Options options_;
    ImGuiContext* ctx_ = nullptr;
    std::unique_ptr<app::UiProbe> probe_;
    std::function<void()> draw_;
    std::vector<std::string> windows_;  // active windows of the last frame
    std::string error_;
    int next_texture_ = 1;
};

}  // namespace yantra::test
