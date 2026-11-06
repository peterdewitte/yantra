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

## Project window (step 9.2)

`ProjectWindow` (`app/model`, no GUI library) is the model of the window
of an open project; `AppState::project_window()` holds it while a
project is open. It listens to the project (`Project::subscribe`) and
rebuilds what changed on the next call:

- `tabs()`: the floors ordered by level (`Name (level)`, points and scale
  in the tooltip); `select()`, `selected()`.
- `content(bin_size_m)`: the selected floor's layers in plan units - walls
  (colour by attenuation), antennas, the measured points
  (`survey::point_list`, exactly the shared list, IND-30), the binned view
  (`survey::bin_points`, with its label, PP-05; needs a scale) and the
  newest stored interpolation (or the one `show_interpolation()` chose)
  as RGBA cells with alpha = confidence, with its staleness (IND-43,
  IND-45). `generation` changes when the content does.
- `layer_rows(LayerSettings)`: the layer panel rows, top layer first,
  with notes ("Stale: ...", "Needs a scale ...").
- `hover(plan point, radius, layers)`: the readout (a point first, then
  the map).
- floor commands: `rename_floor`, `set_level`, `move_floor` (swaps
  levels), `request_remove` / `confirm_remove` / `cancel_remove`
  (`Project::remove_floor(id, true)` removes the floor with its walk
  tests in one transaction).
- `plan_tiles(floor, budget)`: the floor's `plan::TileRenderer`.

Layer order, visibility and the square size are app settings
(`AppSettings::layers`, JSON `"layers"`), not project data. Layers,
bottom first by default: `plan`, `interpolation`, `binned` (hidden),
`walls`, `antennas`, `points`.

The Gui draws it (`draw_project_screen`): DEMO banner (`demo_banner`,
label from the demo description, `AppState::demo_label()`), floor tabs
`tab.<floor id>` and `floors.add`, buttons `floor.properties`,
`floor.down`, `floor.up`, `floor.remove`, `plan.fit`, `plan.zoom_in`,
`plan.zoom_out`, the layer panel (`layer.<key>` checkboxes,
`layer.<key>.up` / `.down`, `layer.bin_size`,
`layer.interpolation.choose`) and the canvas `plan.canvas` (drag pans,
wheel zooms, double click fits). Dialogs `dlg.floor_properties` (fields
`name`, `level`, `height`) and `dlg.remove_floor`, both with `dlg.ok` /
`dlg.cancel`.

`PlanCanvas` (`app/gui`) draws the content in the user's layer order over
the plan tiles (coarsest level first, then the level of the zoom),
badges (map label, `STALE`, binned label, "Not to scale"), the scale bar
(`scale_bar()`), the legend and the hover tooltip. `stats()` says what the
last frame drew (tests). Pictures are `UserTexture`s: registered ImGui
user textures the renderer (or the harness) creates and destroys, so the
GUI layer has no GL. `main()` shuts the renderer down before the `Gui`
frees them; `UserTexture::live_count()` checks for leaks.

The project file is live (7.1): each change is its own transaction, File
> Save is a checkpoint (the file is complete on its own), there is no
Save/Discard on close. A file open in another Yantra (or another window)
is refused with the lock message naming it. Recent files keep the last
opened; a missing file leaves the list when chosen.

## Plan import window (step 9.3)

Add floor (`dlg.add_floor`) offers three ways to make a floor: a new plan
file (`af.plan_file`, the in-app browser with `plan_file_filters()`), a
plan already in the project (`af.src.<source id>`, disabled with the
reason when a referenced file is gone) or a blank floor (`dlg.ok`). A
plan opens the plan import window `dlg.plan_import`, a large modal; the
project window's `floor.plan` ("Plan and scale...") opens it again for a
floor's sheet with that floor selected.

- Model: `PlanImportWindow` (app/model/include/yantra/app/plan_import_window.h)
  holds the editing state over core `plan::PlanImport`: tools
  (`ImportTool`, `tools()` with enabled flag and reason), pointer input in
  page units (`pointer_down/move/up`), trim commands, Add as floor
  (`begin_add_floor`/`confirm_add_floor`, the window stays open), floors
  of this sheet (`placed()`, select, edit with `apply_edit` reanchoring
  walls/antennas/scale points/reference in core, remove after a question),
  scale (`ScaleForm`: two points, scale bar, drawing scale 1:N when
  `drawing_scale_available()`), sharing within one sheet, the reference
  point. `trim_generation()` changes with every trim change (the view
  re-renders the preview then). AppState: `import_plan_file`,
  `import_plan_source`, `edit_floor_plan`, `plan_import()`,
  `close_plan_import()` (selects the last added floor in the project
  window).
- Licence-restricted plans (IND-03, DEMO-12): `open_file` hashes the file
  and looks for a restricted sheet with that SHA-256 in
  `<data folder>/demo/*/*.json` (`find_restricted_sheet`). A match is added
  with `add_source_reference` (no copy) and `restricted()` gives title,
  credit, licence and the approximate-scale note that the window shows.
  `referenced()` is true for every non-embedded source.
- View: `PlanImportView` (app/gui/plan_import_view.h) draws the side
  panel and the canvas; tiles come from `PlanImport::page_tiles()` as
  `UserTexture` quads (the page turns with the trim), the preview from
  `PlanImport::preview(240)`. `stats()` tells tests what was drawn
  (tiles, thumbnails, placed outlines, preview size, credit, canvas
  origin). Ids: `pi.page.<n>`, `pi.tool.<pan|rectangle|polygon|deskew|
  scale_line|scale_bar|reference>`, `pi.rot_left`, `pi.rot_right`,
  `pi.reset`, `pi.deskew`, `pi.finish`, `pi.undo`, `pi.cancel_poly`,
  `pi.add`, `pi.add_name`, `pi.add_level`, `pi.add_height`, `pi.add_ok`,
  `pi.add_cancel`, `pi.apply_edit`, `pi.cancel_edit`, `pi.floor.<id>`,
  `pi.edit.<id>`, `pi.remove.<id>`, `pi.confirm_remove`, `pi.keep`,
  `pi.distance`, `pi.scale_apply`, `pi.ratio`, `pi.ratio_apply`,
  `pi.scale_clear`, `pi.share.<id>`, `pi.ref_clear`, the canvas
  `pi.canvas`, Close `dlg.cancel`.
- Tests drag on the canvas at `stats().canvas_origin +
  view().page_to_screen(page point)`. The side panel scrolls: tests that
  click its lower items use a tall display (clipped items are not
  clickable). Fixtures are generated (`yantra_plan_fixtures`: `fx::png`,
  `fx::png_with_dpi`, `fx::pdf`); PDF cases return early without PDFium.
- Hooks: 9.4 edits walls and antennas on the floor in the project window
  (the trim's reanchoring already moves them); phase 10 exports must use
  `PlanImportWindow::restricted()`/`SourceFile::plan.embedded == false`
  to leave a restricted plan's image out and print its credit.

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
(UI-13); `key()`, `type()`, `hover()`, `click_at()`, `drag()` and `wheel()`
give the other input. Everything is frame-counted, nothing sleeps. Every `*.cpp` in
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
under Mesa llvmpipe limited to OpenGL 3.1 (TST-04); `app_project_demo`
renders a copy of the generated demo project (project window, plan
tiles, layers).
