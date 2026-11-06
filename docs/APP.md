<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# The desktop app: structure and UI tests

For contributors working on the app (OA-17, UI-13). The app calls the
core only (`yantra::core`, `yantra/core/...`); it has no radio, DSP or
decoder code (PLG-01).

## Layers

| Library / target | Folder | Needs | Holds |
|---|---|---|---|
| `yantra_app_model` | `app/model` | core only | everything a test needs: `AppState` (screens, actions, dialogs and their forms, commands, status line), command line, settings, window-system choice (NFR-10), data folders, the application icon, the in-app file browser |
| `yantra_imgui` | `third_party/imgui` + `app/gui/ui_probe.cpp` | nothing | Dear ImGui core, built with `IMGUI_ENABLE_TEST_ENGINE`; `UiProbe` implements the item hooks |
| `yantra_app_gui` | `app/gui` | an ImGui context | `Gui`: draws what `AppState` returns and calls its commands, no logic of its own |
| `yantra_imgui_backends` | `third_party/imgui/backends` | GLFW, OpenGL | Dear ImGui's GLFW and OpenGL 3 backends |
| `yantra` | `app/src/main.cpp` | all of the above | window, GL context (3.1, `#version 140`), main loop, window icon, `--screenshot`, `--frames` |

Rule of thumb: logic goes into `AppState` (tested in `tests/app/test_app_model.cpp`
without a GUI); `Gui` only lays out. A new window gets a view struct and
commands in `app_state.h` and a `draw_...` function in `gui.cpp`.

## Actions, menus and ids

Every menu entry and start-screen button is an `Action` with an
`ActionView` (label, tooltip with its shortcut in brackets, enabled, the
reason when disabled, and `planned_step` for entries a later step wires,
e.g. Tour "9.10"). Disabled entries stay visible with their reason
(DOC-07). A step that wires an action clears its `planned_step` in
`AppState::action()` and handles it in `AppState::trigger()`.

ImGui labels are `"<text>###<id>"`; tests and screenshots use the id:

- menus `menu.file`, `menu.help`; menu items the action id (`file.new_project`,
  `file.open_project`, `file.recent`, `file.open_demo`, `file.save`, `file.save_as`,
  `file.add_floor`, `file.close_project`, `file.export`, `file.report`,
  `file.about`, `file.exit`, `help.tour`, `help.user_manual`,
  `help.quick_starts`, `help.reset_settings`, `help.about`);
- windows `main` (the screen below the menu bar), `status_bar`;
- start screen buttons `start.<action id>`, recent projects `start.recent.<n>`;
- dialogs (`dialog_id()` in gui.h) `dlg.new_project`, `dlg.browse`,
  `dlg.add_floor`, `dlg.about`, the overwrite question `dlg.overwrite`;
  their buttons `dlg.ok` and `dlg.cancel`; fields by name (`name`, `site`,
  `operator`, `notes`, `file`, `level`, `height`); file browser items `fb.*`.

`--dialog new-project|open-project|save-as|add-floor|about` opens a
dialog at start (screenshots, launch tests).

## Data folders and the demo

`data_folders()` (app/model/data_paths.h) lists, in order:
`YANTRA_DATA_DIR`, the installed `<exe>/../share/yantra`, and in a build
tree (`<exe>/../CMakeCache.txt`) the build folder (generated demo
project) and the sources' `data/`. No build path is compiled in. The
logo PNGs (`logo/png/yantra_logo_<size>.png`) give the window icon
(BRD-06; X11 only, Wayland uses the desktop file's icon).

Open demo (DEMO-05) copies `demo/fraunces/fraunces.yantra` and its
`recordings/` to `<XDG_DATA_HOME>/yantra/demo/fraunces` and opens the
copy: the project file is live SQLite, so the installed one is never
opened. `installed.json` stamps the copy with the installed file's size
and time; the copy is reused until the installed project changes. A
project in that folder is a demo (`[DEMO]` in the title).

## Headless UI tests (tests/app/harness)

`yantra::test::UiHarness` (library `yantra_ui_harness`) runs a real
ImGui context without window system or GL: fixed display size and time
step, textures marked uploaded, and a `UiProbe` recording every item with
an ImGui id (label, rectangle, window, disabled) per frame. Plain
`Text()` has no id and is not recorded: check text through the
`AppState` view instead.

    UiHarness ui;                                  // 1280 x 800
    AppState state(settings, paths);
    Gui gui(state);
    ui.set_draw([&] { gui.draw(); });
    ui.frames(3);
    REQUIRE(ui.click("start.file.new_project"));   // move, press, release
    REQUIRE(ui.wait_window("dlg.new_project"));    // open, items drawn
    REQUIRE(ui.fill("name", "Depot", "dlg.new_project"));
    REQUIRE(ui.click("dlg.ok", "dlg.new_project"));
    REQUIRE(ui.click_menu("file", "file.exit"));

`click()` refuses disabled or missing items (returns false); `find()`
gives an item's rectangle and state; `dump()` lists the last frame's
items for failure messages; `set_display_size()` checks small windows
(UI-13); `key()`, `type()`, `hover()` and `click_at()` give the other
input. Everything is frame-counted, nothing sleeps. Every `*.cpp` in
`tests/app/ui/` is part of `test_app_ui`; a step adds its own file there
(`test_ui_<window>.cpp`) and links nothing new.

Use `paths.data_folders`, `paths.user_data` and `paths.home` under
`YANTRA_TEST_SCRATCH_DIR`, never the user's folders. `YANTRA_TEST_DEMO_DATA_DIR`
(the build tree) is defined when the demo project is generated.

## Launch tests

`tests/app/CMakeLists.txt` `yantra_app_launch_test()` runs the binary on
a private Xvfb (`with_xvfb.sh`, skipped with 77 without Xvfb) with private
`XDG_*` and `HOME`, and checks its log lines (`expect_output.sh`):
`yantra: window system ...`, `yantra: window icon set ...`,
`yantra: first frame (<platform>, OpenGL <version>, <renderer>)`,
`yantra: screenshot <file> (<w>x<h>)`. `app_screenshot_gl31` renders
under Mesa llvmpipe limited to OpenGL 3.1 (TST-04).
