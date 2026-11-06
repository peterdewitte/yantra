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
| `yantra_audio` | `app/audio` + `third_party/miniaudio` | a sound back-end at run time (loaded by miniaudio) | `make_miniaudio_io()`: the `AudioIo` for voice notes (REC-06); a null back-end for tests |
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

## Editing, walk test and interpolation (step 9.4)

The side panel of the project window has pages `panel.layers` (above),
`panel.edit`, `panel.walk` and `panel.map`. Their logic is in three
`app/model` classes that `AppState` creates with the project (the project
is a `shared_ptr` now) and drops before it; `AppState::tick()` runs each
frame.

`FloorEditor` (`floor_editor()`, IND-11, IND-12) edits the selected floor
through the project and core `building/edit`:

- tools `edit.tool.pan|select|wall|antenna|aim` (keys P, S, W, A; Aim
  needs a selected directional antenna or outdoor donor); `pointer_down /
  move / up`, `double_click`, `hover` in plan units with a hit tolerance
  (8 screen pixels);
- draw wall: a click per corner (snaps to other walls' corners), Enter or
  double click ends it, Backspace removes the last corner, Esc drops it;
  the class of new walls is `edit.draw_class`;
- select: an antenna, a corner of the selected wall, or a wall; a drag
  moves it (preview `drag_wall()` / `drag_antenna()`, one stored change
  on release); double click on the selected wall adds a corner;
  `edit.split` at an inner corner; `edit.delete` / Delete (a corner; a
  wall of two corners goes as a whole);
- forms: `wall_form()` (class, own value 0..100 dB), `antenna_form()`
  (dialog `dlg.antenna`: name, model, outdoor, height, gain, pattern,
  azimuth, beamwidth, front-to-back, direction, EIRP known, cable loss,
  cell, `add_cell`), the wall-class table (dialog `dlg.materials`:
  `mat.new`, `mat.edit.<id>`, `mat.remove.<id>` (disabled while used),
  `mat.reset`, the form `mat.id`, `mat.name`, `mat.band.<n>.mhz|db`,
  `mat.apply`);
- undo / redo (`edit.undo`, `edit.redo`, Ctrl+Z, Ctrl+Y): each edit is a
  snapshot of the floor's walls and antennas and the project's classes
  and cells, put back with `Project::update_floor` (ids and order kept).
  When the floor was changed elsewhere the history is dropped with a
  note (`undo_note()`).

`InterpolationRunner` (`interpolation()`, IND-41..45, OA-03) runs
`survey::InterpolationJob` in the background: `run(floor)` (metric: the
serving carrier's level), `run(floor, &shown)` (the shown map's request,
`interp.rerun`, marked "(stale)" when stale), `cancel` (`interp.cancel`),
`progress()` (copied from the job's listener: stage and fraction for the
progress bar), `poll()` (the message once; `tick()` then shows the new
map). `run_reason()` says why a floor cannot run (no scale, no points, a
run going on). A run with the same request replaces the stored map of
that key. The settings dialog `dlg.propagation` (`interp.settings`) edits
`PropagationSettings` and the dB per floor (`grid`, `reflections`,
`exponent`, `ref_distance`, `reflection_loss`, `rx_height`,
`default_eirp`, `other_floors`, `floor_attenuation`; `prop.defaults`).
Maps with estimated sources (IND-42) draw them as dashed markers with
their fitted EIRP (`InterpolationLayer::sources`).

`LiveSurvey` (`live()`, IND-20, GEO-02, GEO-05, PLG-27, DEMO-14) holds
the radios and one walk test:

- `walk.find_radios`: loads the technology plugins and the radio
  plugins of `AppPaths::radios` (main(): the plugin folder rule and the
  calibration table) through the core radio host and lists every device;
  a simulated radio that uses the context is chosen by default. Levels
  are calibrated with the user's table when present, else dBFS.
- the plugins' context: the shown floor, the mouse (`cursor()`), the walk
  position and the scene, re-exported when walls or antennas change.
- kinds (`form()`): point and click, continuous (GEO-02: clicks on the
  plan close segments; `walk.pause`, `walk.delete_last`), manual (GEO-05:
  a click measures there for `walk.seconds`), mouse survey (DEMO-14: the
  simulator measures where the mouse is; at most one point per
  `walk.spacing` m (`WalkTestOptions::min_spacing_m`); "Move slower"
  above `walk.max_speed` m/s; a click on the plan runs the interpolation;
  Esc or `walk.stop` stops). The pipeline cycles every 1 s for walks and
  every 0.2 s for the mouse survey. Points go into a session of the floor
  and are drawn as they come.
- While a walk runs the editing tools are off and the left button goes to
  the walk.

`PlanCanvas::set_left_pans(false)` gives the left button to a tool
(middle and right drag still pan); `input()` is the mouse in plan units
for the frame; `set_overlay()` draws the tools' overlay. `Gui::
tool_overlay()` says what it drew (tests).

Tests: `test_app` (`test_floor_editor.cpp`, `test_interpolation_runner.cpp`),
`test_app_ui` (`ui/test_ui_edit.cpp`) and `test_app_live`
(`test_live_survey.cpp`, `test_live_ui.cpp`: the simulator from the build
tree in a radio folder of its own, on a copy of the Fraunces demo; real
time, waits on conditions with a deadline; one ctest per case). Combo
boxes have no probe label: tests set their value through the model.

## Survey panels, recording and replay (step 9.5)

One model feeds every survey panel: `SurveyMonitor`
(`app/model/survey_monitor.h`), a `MeasurementObserver` that `AppState`
owns (`monitor()`) and gives to the live radio and to the replay. It keeps
the frame the panels show (newest cycle, the cycle at the live graph
cursor `set_cursor()`, or the replay's cycle `show_cycle()`), the graphs'
history (serving + up to 31 neighbours, level, C1, the serving cell's
first quality value; cell names and colours from the project's cell
database by its match fields, else a 12-colour palette), the event list,
the statistics (UI-11; coverage threshold `survey.threshold`) and the
NFR-07 self-interference warning (core `measure/self_interference.h`).
Rows hold the technology schema's fields as text (PLG-01); threshold
lines are the serving record's SYSTEM_INFO fields in dBm (level graph)
or dB (C1 graph).

Panels (`Gui::draw_survey_panels`, under the plan; `survey.panels`
shows or hides them, they open by themselves when a radio or a replay
starts): tabs `survey.cells` (UI-01 large values, UI-02 serving cell and
broadcast parameters, UI-03 neighbours: listed and received, received
only, listed only in different colours), `survey.graphs` (UI-04:
`graph.level|c1|quality`, threshold lines `graph.th.<field>`; a click
or drag on `graph.level_plot` etc. sets the cursor: live
`SurveyMonitor::set_cursor`, replay `ReplaySession::seek_mono`;
`graph.live` follows the newest again), `survey.scanner` (scan channels
and other carriers by frequency), `survey.events` (newest first),
`survey.stats`. The NFR-07 warning is a line above the tabs and an item
of the status bar.

Side panel page `panel.survey` (`draw_survey_section`):

- radio: `survey.find_radios`, `survey.radio_start` /
  `survey.radio_stop` (monitoring without a walk:
  `LiveSurvey::start_radio()`; the Walk page still chooses the radio);
- recording (`LiveSurvey`, core `recording::Recorder`, files in
  `<project folder>/recordings/`, listed in the project when they stop):
  `rec.name`, `rec.notes`, `rec.split` (minutes), `rec.compress`,
  `rec.record`, `rec.quick`, `rec.stop`, `rec.marker_label`,
  `rec.marker`, `rec.new_file`, `rec.new_file_pause`, `rec.resume`,
  `rec.voice` / `rec.voice_stop` (REC-06: through `AppState::
  set_audio()`; main() gives `make_miniaudio_io()`, tests `FakeAudio`;
  the WAV goes next to the recording file). A walk test always has a
  recording file (REC-03): it records into the running recording or
  starts one named after the walk (`_2`, `_3` ... when the name is in
  use) and stops it at the end; its session links to the recording
  session id. The period cannot change while recording (walk 1 s, mouse
  survey 0.2 s). REC-11: Exit, Close project and the window's close
  button are refused while recording (`AppState::request_exit()`).
- ring buffer (REC-09): `rec.ring`, `rec.ring_minutes` (2..60, files of
  one minute in `RadioPaths::ring_folder`, main(): `$XDG_CACHE_HOME/
  yantra/ring` or `~/.cache/yantra/ring`; old ring files there go when it
  starts); `rec.ring_name` + `rec.ring_save` cut the kept files into a
  recording of the project (core `recording/cut.h`).
- replay (`ReplaySession`, `AppState::open_replay()`; the live radio
  stops first): `replay.item.<n>` + `replay.open`; one pass over the
  whole recording fills the graphs, events and statistics, then the
  monitor's history is frozen and the core `ReplayPlayer` only moves the
  frame. `replay.play|pause|stop|again`, `replay.speed` (0 = as fast as
  possible), `replay.position`, `replay.marker.<t>` (seek), voice notes
  are played at their instant (`ReplaySession::tick()` from
  `AppState::tick()`), `replay.cut_name` + `replay.cut_head` /
  `replay.cut_tail` (REC-08: start to cursor, cursor to end, listed in
  the project), `replay.close`.

Status bar (UI-08, `AppState::status_bar()`): the last message, then
radio state (or the replay), recording state, records dropped (when
any), self-interference, free disk space where the project is and the
battery or mains (`read_system_status()`: `/sys/class/power_supply`,
read at most every 2 s); warnings in amber.

Tests: `test_app` (`test_survey_monitor.cpp`: synthetic cycles, graph
arithmetic, power-supply folder, `FakeAudio`, miniaudio's null back-end),
`test_app_live` (`test_live_survey.cpp`: monitoring, recording with a
marker and a voice note, replay and cut, the walk's recording file, the
ring buffer; `test_live_ui.cpp`: the panels, graphs and cursor, status
bar, recording and replay driven through the harness).

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
