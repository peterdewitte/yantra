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

- menus `menu.file`, `menu.radio`, `menu.view` and `menu.tools` (only when
  a plugin adds entries), `menu.network`, `menu.help`; menu items the action id (`file.new_project`,
  `file.open_project`, `file.recent`, `file.open_demo`, `file.save`, `file.save_as`,
  `file.add_floor`, `file.close_project`, `file.export`, `file.report`,
  `file.settings`, `file.about`, `file.exit`, `network.cell_db`,
  `network.verification`, `network.reselection`, `help.tour`, `help.user_manual`,
  `help.quick_starts`, `help.reset_settings`, `help.about`);
- windows `main` (the screen below the menu bar), `status_bar`;
- start screen buttons `start.<action id>`, recent projects `start.recent.<n>`;
- dialogs (`dialog_id()` in gui.h) `dlg.new_project`, `dlg.browse`,
  `dlg.add_floor`, `dlg.about`, the overwrite question `dlg.overwrite`;
  their buttons `dlg.ok` and `dlg.cancel`; fields by name (`name`, `site`,
  `operator`, `notes`, `file`, `level`, `height`); file browser items `fb.*`.

`--dialog new-project|open-project|save-as|add-floor|about|settings|reset-settings` opens a
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

## Radio windows (step 9.6)

Yantra builds every radio window itself from the radio API (PLG-16,
RAD-40..48): the same code for every radio plugin, no per-plugin UI.
The model is `RadioWindows` (`app/model/radio_windows.h`, owned by
`AppState`, `radio_windows()`, with the project; ticked by
`AppState::tick()`); drawing is `app/gui/radio_view.cpp`. All radio
calls (`RadioDevice` methods wait for the plugin's control thread) run
on the model's worker thread, which publishes a snapshot; records,
events and cycles of the open device come through a pipeline observer
(`LiveSurvey::add_observer`). The windows follow
`LiveSurvey::device_generation()` (the host is rebuilt when the period
changes).

Connection (`LiveSurvey`): `connect()` opens the chosen radio without
measuring, `disconnect()` closes it (refused while measuring); after
Stop radio or the end of a walk a connected device stays open. Opening a
radio window for a radio chooses and connects it.

Radio menu `menu.radio` (between File and Help; disabled on the start
screen): `radio.list` (the Radios window), `radio.find`,
`radio.settings`, `radio.debug`, `radio.calibration`, `radio.info`
(enabled once a radio is chosen), then per plugin with devices found
Radio > `radio.p.<plugin id>` > `radio.d.<n>` >
`radio.d.<n>.settings|debug|calibration|info`. Windows `win.radio.list|settings|
debug|calibration|info`:

- Radios: `radio.list.find`, `radio.row.<n>` (choose),
  `radio.list.connect`, `radio.list.disconnect`,
  `radio.list.settings|debug|calibration|info`, `radio.list.plugins`
  (the plugin entries with status, API, licence, capabilities).
- Settings: a section per group (`set.group.<name>`), the settings by
  sort order; widgets by type (`widget_of`): BOOL checkbox
  `set.<key>`, ENUM combo `set.<key>` with items `set.<key>.item.<name>`,
  INT/UINT/DOUBLE with a closed range a slider `set.<key>` plus the
  input field `set.<key>.input`, an open range only the field, STRING
  the field, ACTION a button `set.<key>` with its result text. Typed
  values are checked by `parse_setting` (type, range, step for INT/UINT,
  255 bytes) before the radio sees them and applied when the field is
  left after an edit (Enter, Tab or a click elsewhere). The
  value read back is shown when it differs (RAD-41). UNAVAILABLE rows are
  greyed with the plugin's reason; WARNING asks first
  (`dlg.radio.confirm`, `radio.confirm.ok|cancel`); RESTART asks while
  the radio measures (the host stops, applies and restarts); ADVANCED
  rows behind `set.advanced`; presets `preset.<id>`; `set.reset` (Reset
  to defaults, asks). Translations `setting.<key>.*`, `preset.<id>.*`,
  `sensor.<key>.*`, `selftest.<name>.label` in the language of
  `RadioWindows::set_language()` (default English; the app language
  arrives with step 9.9). Rebuilt on settings_changed (RAD-44).
- Debug: device state, host and plugin counters (RAD-24), the latest
  record per carrier with every field, flags and the rate over 10 s
  (`dbg.rec.<n>`), sensors (at most once a second, RAD-45), events
  (RAD-25), the plugin log (RAD-14), context calls (PLG-27). No IQ.
- Calibration (RF-02, HW-01b): the key and its status, the table entries
  of this device (`cal.remove.<n>`), the level wizard (core
  `calibration/wizard.h`; `cal.freqs`, `cal.levels`, `cal.days`,
  `cal.readings`, `cal.method.generator|reference`, `cal.start`,
  `cal.measure`, `cal.skip`, `cal.cancel`, `cal.save`; readings are the
  radio's own dBFS records within 12.5 kHz of the target, so the radio
  must measure; a simulated radio makes a `practice` entry; a
  self-calibrated radio needs none), `cal.verify`, `cal.ppm` (frequency
  error from a known carrier's `freq_error_hz`), `cal.file` +
  `cal.import|export` (merge: the newer entry wins). Saving writes
  `RadioPaths::calibration_table` and sets the pipeline's corrector.
- Reference noise (NFR-07, in the calibration window): `cal.noise.take`
  takes the median noise of 5 cycles into
  `SurveyMonitor::set_interference_reference()` (antenna away from the
  PC; the radio must deliver dBm: self-calibrated or calibrated),
  `cal.noise.clear` goes back to the thermal floor.
- Device info: the self-test `info.self_test` (RAD-46; refused while
  measuring), the plugin entry, the device entry (RAD-30), the
  calibration key, sensors.

Stored device settings (RAD-47, NFR-05): `RadioPaths::plugin_state`
(main(): `plugin_state.json` in the user's config folder next to
`app.json`; "" = in memory), written atomically after each change by
`PluginStateFile`:
`{"format":"yantra-plugin-state","version":1,"enabled":{id:bool},
"settings":{id:{key:text}},"devices":[{"plugin","device","settings":
[{"key","type":"bool|int|uint|double|string|enum","value"}]}]}`. A file
that cannot be read is reported and never overwritten.

Hooks for the next steps: the plugin manager (step 9.7, below) switches
radio plugins through the `enabled` part of the state file; the
language (9.9) calls `RadioWindows::set_language()`.

Tests: `test_app` (`test_radio_windows.cpp`: widgets, validation, texts,
groups, translations, the state file), `test_app_live`
(`test_radio_windows_live.cpp`: the simulator, beacon, `soapysdr_fake`,
`hackrf_fake` with a WARNING setting; `test_radio_ui.cpp`: every window
by clicking for the simulator, SoapySDR, HackRF and PEI plugins), and
`test_app_live_pei` (the PEI cases under `yantra-pei-sim`). The fakes
are test builds of yantra-radios (`YANTRA_RADIOS_FAKE_SDR`).

## About, settings, reset and plugin UI (step 9.7)

**About** (UI-15, `AppState::about()`, `Gui::draw_about`): the Yantra
logo (BRD-06), name, version, build date (`YANTRA_BUILD_DATE`, the
configure date in UTC), licence and the source link with the AGPL note
(NFR-01), the curtat.be logo, statement and homepage (BRD-01..03), the
credits read from `THIRD_PARTY.md` (`app/model/credits.h`: the
"Libraries" and "Data" tables; installed as `share/yantra/THIRD_PARTY.md`,
in a build tree the sources' copy next to `data/`) and the loaded plugins
with versions, licences and status. The curtat.be logo follows the
theme: `branding/curtat_logo_dark_256.png` on the dark theme, `_light_`
on the light one (`curtat_logo_relative()`); logos keep their
transparency (`load_data_png`, stb_image without the plan reader's white
paper). Test overlay: `Gui::about_overlay()`.

**Settings window** (`Dialog::Settings`, `file.settings`, Ctrl+,;
`app/gui/settings_view.cpp`): a working copy `AppState::settings_form()`
applied by `apply_settings()` (checked by `check_settings()`), tabs
`settings.tab.general` (theme `settings.theme.dark|light`, tooltips
`settings.tooltips` and `settings.tooltip_delay` (DOC-07 h; off hides the
hover texts, the "(?)" markers and disabled reasons stay), window system
(next start)), `settings.tab.survey` (`settings.threshold` coverage
threshold, `settings.graph_span` live graph span 1..60 min,
`settings.ring` / `settings.ring_minutes` ring buffer default for a
project that opens) and `settings.tab.plugins`; buttons `dlg.apply`,
`dlg.ok`, `dlg.cancel`. Enter in a field ends the edit only (ImGui
applies typed numbers when the field is left). The survey panel's
threshold field calls `set_coverage_threshold()`, so it is kept too.
Settings JSON adds `"theme"` and `"survey"`. The theme switch is only
dark/light here: `Gui::apply_theme()` follows `settings().theme` each
frame; step 9.9 adds the themes, touch layout and language.

**Plugin manager** (PLG-09, Plugins tab, `AppState::plugin_manager()`,
`set_plugin_enabled(radio, id, on)`): one row per plugin.h plugin
(`AppState::plugins()`, the loader `AppState` owns for the whole run)
and, with a project open, per radio plugin (the `RadioHost` of
`LiveSurvey`, one per process): name, version, API, author, licence,
status, message; switch `plugins.toggle.<n>`. The loader and the state
store (`plugin_state.json`) are shared with `LiveSurvey`
(`PluginServices`) so two writers never clobber the file. A change is
refused while a walk, a recording, the radio or a replay runs; it goes
through `LiveSurvey::change_plugins()`, which closes the radio, rebuilds
the host and pipeline and asks to find the radios again.

**Reset all settings** (UI-16, TST-37; `help.reset_settings`,
`Dialog::ResetSettings`, `dlg.reset_settings`): lists what is reset and
what is kept (`reset_summary()`), `reset.calibration` also resets the
calibration table, `dlg.ok` resets. `reset_settings_files()`
(`app/model/settings_reset.h`) copies `app.json`, `plugin_state.json`
(and the calibration table when asked) into
`<config>/backups/YYYYMMDD-HHMMSS[-n]/` and removes them; projects,
recordings and cell databases are not touched.
`AppState::confirm_reset_settings()` closes the project (refused during a
recording), drops the plugins, sets the defaults in memory (nothing is
written until a change, as on first start) and bumps `ui_generation()`;
the Gui then restarts its own state (`Gui::restart_ui`). `yantra
--reset-settings [--reset-calibration]` does the same without a window
and exits (launch test `app_reset_settings`).

**Plugins' own UI** (PLG-06; model `PluginUi`, `app/model/plugin_ui.h`;
drawing `app/gui/plugin_view.cpp`): the windows and menu entries that
enabled plugin.h plugins declare. Menu entries go to File, View, Radio,
Tools, Help (`plg.<plugin id>.<item id>`; the View and Tools menus appear only
with entries; `menu_state` greys or checks them); choosing one opens its
window or calls `menu_selected`. Windows `plg.<plugin id>.<window id>`:
`YANTRA_UI_PANEL` is a window docked at the right edge, `YANTRA_UI_WINDOW`
a floating window, `YANTRA_UI_DIALOG` a modal; closing calls
`window_closed`; `ui_open_window` / `ui_close_window` requests are
applied in `PluginUi::tick()`; a plugin switched off loses its windows.
Yantra implements every `yantra_ui_api` control with ImGui, with the
plugin's translations (`PluginUi::set_language`, step 9.9) and the help
rules: controls `plg.<control id>`; a number with both limits is a slider
`plg.<id>.slider` plus a typed field `plg.<id>.input` (clamped and
snapped to the step); `YANTRA_UI_CONFIRM` asks first (`plg.confirm.ok`,
the click is reported in the next frame); `YANTRA_UI_DISABLED` shows the
reason. Test hooks: `Gui::plugin_frame()`, `Gui::plugin_window_state()`
(texts of `value`, rows of `table`). No plugin API change was needed:
the API stays 1.0.

Hooks for the next steps: 9.8 adds its views as `Action`s/dialogs the
same way and may add a settings tab in `Gui::draw_settings`; 9.9 extends
`Theme` and `apply_theme`, sets the language with
`PluginUi::set_language` and `RadioWindows::set_language`, and adds help
to the settings tabs; the About logos already follow the theme.

Tests: `test_app` (`test_settings_plugins.cpp`: settings round trip and
range checks, apply and the threshold, reset with backup and the app
reset, `--reset-settings`, credits and logos, the plugin manager and
`PluginUi` with the example beacon plugin), `test_app_ui`
(`ui/test_ui_settings.cpp`: About, the settings tabs by clicking, the
plugin switch, Reset, and the beacon's Tools entry and window with its
channel field), launch tests `app_settings` and `app_reset_settings`.

## Cell database, network checks and post-processing (step 9.8)

The Network menu opens three non-modal windows (`AppState::network_windows()`
flags, drawn by `app/gui/network_view.cpp`); they need an open project.
The models call core `celldb` only; the technology rules (key fields,
channel raster, value comparison, C1 and reselection) are the
technology plugin's (PLG-01), so the windows work for any technology
plugin. `AppState::technologies()` gives the enabled technology plugins
as `celldb::Technologies`.

- **Cell database** `win.cell_db` (`CellDbEditor`, DB-01, DB-02): the
  list `cells.table` (rows `cells.row.<id>`: name, technology, site and
  sector, carrier in MHz, the plugin's channel number, identity with `la`
  in hexadecimal, neighbour count) with the filter `cells.filter`;
  `cells.new.<technology>` starts an entry whose form lists the plugin's
  KEY and VERIFY fields (`cells.field.<name>`; typed as decimal, `0x..`,
  a number or a text), `cells.form.*` (id, label, site, sector,
  azimuth, colour, notes), the neighbour list `cells.form.neighbours`
  (one `name=value/name=value` line per neighbour, at most 31),
  `cells.add_field` for another field; `cells.apply` stores the whole
  database at once (`Project::set_cell_database`), `cells.cancel`,
  `cells.remove` (refused while an antenna serves the cell). CSV
  `cells.csv.*`: file, `la` in hexadecimal, semicolons, import replaces
  or merges, `cells.import` / `cells.export`; skipped rows are listed.
  Pairing `cells.match.<technology>` / `cells.match_apply.<technology>`:
  the fields received cells are paired by (empty: the KEY fields).
- **Verification** `win.verification` (`NetworkChecks`, DB-03..05): the
  cells received since the radio or the replay started
  (`SurveyMonitor::observed_cells()`, a `celldb::Observations` fed by
  every cycle, cleared with the monitor) against the database: counts,
  then one row per check coloured match (green), differs (red), new
  (blue), missing / not seen (amber) (`state_rgb`); `verify.matches`
  hides the matches. `verify.learn` learns (adds new cells, fills fields
  entries lack, never overwrites); `verify.log` appends the differences
  to the CSV `verify.log_file` (default `<project>_differences.csv`;
  each difference once, `verify.not_seen` adds the entries not seen);
  the neighbour cross-check over the database and the survey lists
  not-mutual, unknown-list, not-seen and ambiguous entries. The report
  tables of the same checks are phase 10's.
- **Reselection simulator** `win.reselection` (DB-08): serving cell
  `resel.serving.<id>` (entries of a technology with a reselection
  rule), MS power `resel.ms_power`, a level per cell
  `resel.level.<id>` (its neighbours from the database list), or
  `resel.received` for the levels received; the plugin's answer: C1 per
  cell, the serving state, the cell chosen and its reason line. The rule
  is the plugin's as it is (`celldb::simulate`; its check against
  EN 300 392-2 clause 18.3.4 is an open owner question).

The **Post** page of the side panel `panel.post` (`PostProcessing`,
PP-01..05) sets `ProjectWindow::set_post(PostSettings)`, so the plan's
points, the binned layer, the legend and the figures show the same view;
the default `PostSettings{}` is the plain serving level of 9.2.

- PP-01: `post.folder` and `post.load.<n>` load a recording into the
  project as walk-test points (`survey::import_recordings` with the live
  run's technology catalog; a recording already in the project is
  refused). The hover readout adds the plan position, the serving, the
  strongest and every neighbour cell (`ProjectWindow::point_detail`,
  cached per point).
- PP-02: the value `post.carrier.{serving,strongest,frequency}` (with
  `post.frequency`) and `post.value.{level,c1,quality}` (with
  `post.quality_field`, e.g. `mer_pct`, `ber_pct`); the view
  `post.mode.{values,goodbad,offset}` with `post.threshold`; good/bad
  recolours the points (`survey::classify`); `post.changes` marks the
  serving-cell changes (yellow squares); the figures give the points'
  statistics, the share at or above the threshold and the coverage of
  the tested area (`survey::area_coverage` on the binned squares).
- PP-03: `post.fail.<n>` switches cells off (the cells the floor's points
  name, `survey::cells_seen`); the best server is chosen again from the
  measured levels (`Metric::failed_cells`).
- PP-04: `post.offset` adds dB to every level (`survey::with_offset`);
  the view's label ("Simulation: ...") is a badge on the plan.
- PP-05: `post.percentile` is the binned layer's percentile (the square
  size stays the Layers page's).
- EXP-06 view: `post.basis.{points,tested,floor}`, `post.acc_threshold`,
  `post.acc_percent`: one criterion evaluated on the measured points, the
  tested squares or the shown interpolation's confident area
  (`survey::evaluate`, `survey::evaluate_area`), PASS or FAIL. Stored
  profiles and the acceptance report are phase 10's.

Hooks for the next steps: 9.9 translates the texts of `network_view.cpp`
and `post_view.cpp`, gives the windows touch sizes and help pages
(`network.*` tooltips are the in-app help, DOC-07); 9.10's tour can open
`win.cell_db` and `panel.post` by their ids; 9.11's screenshots use the
same ids; phase 10 reports the same views (`NetworkChecks::verification`,
`PostProcessing::stats`) and adds stored acceptance profiles and the
exports.

The TETRA calculator (DB-07: channel <-> frequency with offset, duplex and
reverse operation, subscriber class bits) is the TETRA plugin's own
window `plg.yantra.tetra.calculator` under Tools (docs/plugins/TETRA.md),
so it needs no core code.

Tests: `test_app` (`test_network.cpp`: the editor's form, neighbours,
filter, CSV round trip, match fields; verification, log, learn and
cross-check with generated received cells; the reselection cases of the
TETRA rule; the Network actions; the post-processing views, acceptance
bases and loading a recording on the demo), `test_app_ui`
(`ui/test_ui_cells.cpp`: the calculator, the editor on the demo,
verification and reselection with the demo's replayed walk, the Post
page and the hover readout).

## Touch layout, themes, languages, help and sounds (step 9.9)

The style follows the settings (`Gui::style_of(AppSettings)`, applied
by `Gui::apply_style` at start and whenever the settings change):

- **Themes** (UI-13): `settings.theme.dark`, `settings.theme.light`,
  `settings.theme.high_contrast` (black windows, white text, yellow
  focus). `theme_is_light()` picks the logo variants (BRD-03).
- **Touch layout and scale** (UI-13): `settings.touch` scales the font
  by 1.5 and pads frames so buttons, fields, menu entries, list rows
  and `small_button`s are at least `kTouchTargetPx` (48 px) high;
  `settings.ui_scale` (75..250 %) scales every size. The widgets take
  their sizes from `gui_detail::layout()` (`target_h`, `button_w`,
  `row_h`, `px`). Dialogs never get higher than the app window; the
  settings tabs scroll (`###settings.body`) so Apply, OK and Cancel stay
  in view.
- **Languages** (UI-12): `settings.language` (English, Nederlands,
  Français, Deutsch). The code holds the English texts; `tr()` (labels,
  tooltips, reasons, status texts), `trf()` (`{}` places) and
  `tr_mark()` (static tables, translated where shown) look them up in
  `data/i18n/<lang>.json`, embedded at build time
  (`build/app/i18n_tables.cpp`). The `###id` part of a label never goes
  through a table, so ids are the same in every language. The plugins'
  windows and the radio windows get the language too
  (`PluginUi::set_language`, `RadioWindows::set_language`). Texts written
  to files (CSV, JSON, project keys) stay English. To add a text: write
  it in English in `tr()`, then add it to the three tables;
  `tools/ci/lint_ui.py --extract` lists every text.
- **Help** (DOC-07 e): the user manual in `docs/manual/NN-*.md`
  (installed to `share/yantra/manual`; `manual_sections()` in
  `app/model/src/manual.cpp` lists its chapters and sections). Every
  window and page has `help` at its top right, opening its section;
  F1 opens the section of the window that had the focus; Help > User
  manual opens the contents. The help window is `win.help`
  (`help.contents`, `help.close`, `help.chapter.<n>`, the text in
  `###help.text`); over a modal dialog it opens as a modal on top, and
  the dialog stays as it was. `AppState::open_help(section)` /
  `close_help()` / `help_view()`.
- **Sounds and level alerts** (UI-09, UI-10): the Sound tab
  `settings.tab.sound`: `settings.sound` (on/off), `settings.volume`,
  `settings.sound_sync`, `settings.sound_reselection`,
  `settings.sound_battery`, the level tone `settings.level_tone` with
  `settings.level_interval` (2..120 s), the test buttons
  `settings.test.<sound>`, and up to 8 level alerts
  (`settings.alert.cell.<i>`, `settings.alert.threshold.<i>`,
  `settings.alert.remove.<i>`, `settings.alert.add`, popup on/off
  `settings.alert_popup`). `AudibleAlerts` (model) turns the live radio's
  events (sync lost/back, serving cell change), a low battery and the
  serving level into short tones (`alert_clip`: patterns of sine tones;
  the level tone's pitch goes from 300 Hz at -120 dBm to 1500 Hz at
  -40 dBm) played through the `AudioIo` (miniaudio; `FakeAudio` in the
  tests). A sound never cuts a playing one (a voice note); at most 4
  wait. A level alert's cell is matched by name, identity, key or
  carrier in MHz; it fires once when the level drops below the
  threshold (or the cell is no longer received) and again only after
  it was 2 dB above. Its popup is `win.alerts` (`alerts.dismiss`).
  Replay plays no sounds.
- **UI lint** (DOC-20): `tools/ci/lint_ui.py` (ctest `ui_lint`) checks
  the tables against the code; `tests/app/ui/test_ui_lint.cpp` draws
  every window in every language, desktop and touch, and checks the
  frame (`tests/app/ui/ui_lint.h`): every control with an id has a
  hover text (the tooltip helpers of `gui_widgets.h` note their item
  while a `HelpRecord` is set), a disabled control has its reason, no
  controls overlap or are cut off at their window's edge, touch targets
  are 48 px, and every text has its translation. A new window joins the
  lint by adding it to `visit_all()`.

Hooks for the next steps:

- 9.10 (guided tour): the tour's steps can open help sections with
  `AppState::open_help("N.M")` and point at the stable ids above; its
  progress belongs in `AppSettings` next to `language` and `touch`
  (settings JSON, `check_settings`, reset); its texts go through `tr()`
  and the three tables, and `visit_all()` gets the tour window.
- 9.11 (screenshots): `Gui::apply_style(GuiStyle{theme, touch, scale})`
  and `set_language()` give every theme, language and the touch layout
  headless; the windows and pages to shoot are those `visit_all()`
  opens (the demo project for the project pages).

## Guided tour (step 9.10)

Help > Tour, the first-start offer on the start screen (`start.tour_offer.yes/no`, once; UI-16 resets it) and
`yantra --tour` open the guided tour (DEMO-11); `yantra --demo` opens the demo project with the simulator
(DEMO-01). Model: `app/model/include/yantra/app/tour.h` (`Tour`, owned by `AppState::tour()`); views:
`app/gui/tour_view.cpp`; manual 1.3.

- Steps (`TourStep`, keys in `tour_step_key`): choose, new_project, import_sheet, trim_floors, second_source,
  scale, walls, antennas, mouse_survey, results, switch_floors, report, finish. Each `TourView` has the title and
  text, `targets` (control ids, most specific first; the Gui frames the first one the UiProbe saw in the last
  frame, `Gui::tour_overlay().target`), `done` (from the app state), `progress`, "Show me" (`can_show`,
  `show_reason`) and the DEMO-07 label. Back, Skip/Next, Leave; progress in `AppSettings::tour`
  (`TourProgress`: offered, building, step, done, finished) saved with the settings.
- Show me goes through the same commands as the controls: `AppState::new_project` (the tour's project
  `<user_data>/tour/<id>.yantra`), `import_plan_file`/`import_plan_source`, the plan import window's rectangle and
  Add as floor per described floor (several floors from one sheet), the described scale (factor or scale bar,
  mapped through the page), walls and antennas mapped from the description's trim to the user's trim, the
  description's cell database, the mouse survey with the simulated radio (`request_panel("walk")`), the layers,
  the next floor tab, and at the end the finished example (Fraunces: Open demo; MAK: a copy of the DEMO-13
  project in `<user_data>/demo/mak/<id>`). New floors on a described sheet get its `example_id` (trim centre
  inside the user's trim), so the DEMO-07 label and the simulator's scenario follow.
- DEMO-12, MAK buildings: `choose` checks the cache (`AppPaths::sheet_cache`: `$YANTRA_MAK_CACHE`, else
  `<XDG_CACHE_HOME or ~/.cache>/yantra/mak`); a missing or wrong file opens `Dialog::TourConsent`
  (`dlg.tour_consent`: credit, licence, URL, size, cache file, what is sent). Download (`dlg.ok`) runs
  `SheetFetch` (sheet_fetch.h) on a worker: the `Downloader` (main: `make_curl_downloader`, app/net, library
  `yantra_app_net`; default: none, with the reason) writes `<cache>/.download/<file>`, `demo::check_sheet_file`
  checks hash, size and pixel size, and only then is it renamed into the cache (unchanged). No, Esc, offline, a
  failure or a different file (message with both SHA-256 values and the URL): `Tour::fall_back` continues with
  the Fraunces Tavern and says why; nothing stays in the cache. Projects reference the sheet (IND-03);
  `ProjectWindow::set_sheet_folder` reads it with `Project::read_source_from` when the kept path is gone. The
  credit, licence and approximate-scale note are shown under the floor tabs (`AppState::plan_credit`,
  `Gui::tour_overlay().credit_text`); trims and overlays stay display-time only.
- curl: one GET, `User-Agent: Yantra/<version>`, no cookies or referrer; protocols https, http and file
  (descriptions must use https; the tests map it to file:// or a loopback server), redirects to https only (5).
- Dialogs block windows behind them, so inside an open dialog the tour shows as a strip at its top
  (`Gui::tour_overlay().strip`).
- Not done here: the report step is shown as not available (phase 10, File > Report). The simulator's levels
  are dBFS without a calibration table, so a click in the tour's mouse survey runs the interpolation but on a
  new project it reports "no measured point with a level in dBm" (the demo project interpolates with its
  ground-truth walk); see BUILD_LOG Open questions.
- Tests: `tests/app/test_tour.cpp` (steps and settings, first-start offer, Fraunces by Show me with progress,
  Leave, Back, reset; MAK with a synthetic stand-in through file:// and a loopback HTTP server checking the
  request; refused, Esc, no downloader, offline, wrong hash/size/pixel size, a wrong cached file),
  `tests/app/ui/test_ui_tour.cpp` (clicks, framed control, consent, strip, credit line),
  `tests/app/test_live_ui.cpp` (the tour's mouse survey with the simulator), and the tour windows in
  `test_ui_lint.cpp`.
- Hooks for 9.11 (screenshots): `yantra --tour` and `--demo`; `AppState::tour()` with `go_to(step)` and
  `show_me()` put any step on screen; `request_panel(key)` selects a side-panel page. Screenshots use the Fraunces
  Tavern only (never a MAK sheet).

## Export, report and sharing (step 10.4)

File > Export and File > Report (EXP-01, EXP-03..08; manual 10) need a project with a floor. Model:
`app/model/include/yantra/app/report_export.h` (`ReportExport`, owned by `AppState::output()`, made and
dropped with the project's tools); sharing: `share.h` (`SharingSettings` in `AppSettings::sharing`,
`ShareJob`, `Sender`, `check_sharing`, `copy_to_folder`, `share_files`) and `app/net/curl_sender.{h,cpp}`
(libcurl); views: `app/gui/report_view.cpp`; dialogs `dlg.export`, `dlg.report`, `dlg.profiles`,
`dlg.sharing` (manual 10.1-10.4).

- Export (`ExportForm`): kind CSV / DXF / floor picture / batch; the shown floor or all floors (CSV: one
  file; DXF, picture: `<stem>_<floor id>.<ext>`); CSV columns in `exports::all_columns()` order
  (`ex.col.<column id>`), separator (`, ; tab |`), UTC or local time, neighbours 0..32; the picture's size
  and the stored map under the points (the newest interpolation of the window's metric, else the newest;
  never a licence-restricted plan, DEMO-12); batch: the folder tree (default `<project>/recordings`),
  merged or separate, CSV or an exporter plugin's format (PLG-02). Files default to
  `<project folder>/exports/`; Browse uses the in-app browser (`BrowsePurpose::ExportFile`). The rows
  are the project window's metric (IND-30). CSV, DXF and the picture are written at once; the batch
  runs on the worker.
- Report (`ReportForm`): title, surveyor (EXP-05 operator field: kept for the run, never stored),
  threshold, more thresholds (`-85, -105`), histogram bin, the point table, the floors (`rp.floor.<id>`),
  the profiles (stored ones, else the default; or a choice, `rp.profile.<i>`), PDF and/or HTML (same name,
  `.html`); default `<project folder>/reports/<stem>_report.pdf`. The worker replays the received cells
  for the network verification (`report::observed_cells`, with the TETRA catalog of the live survey) and
  calls `report::write_coverage_report` in the app's language (`tr`); `SOURCE_DATE_EPOCH` fixes the
  creation time. The tour's report step (`TourStep::Report`) is done after a report of the tour project;
  Show me runs File > Report with its defaults.
- Acceptance profile editor (`ProfileForm`, EXP-06): add (a free name), remove, name, basis (points,
  tested area with square and percentile, floor area with the least confidence), criteria (level and
  share); `survey::validate` and unique names; Save stores them with `survey::set_acceptance_profiles`
  and returns to the Report dialog.
- The worker: one thread at a time (`busy()`; a second job is refused), progress and cancel, the
  project's own lock (the project is thread-safe); `AppState::tick()` shows the end in the status line
  once, then starts the sending that was asked for. Closing the project cancels and waits.
- EXP-07 sharing (optional; `Sharing settings...` in both dialogs, `dlg.sharing`): Off, a shared folder
  (copied as `<name>.part`, then renamed), HTTP PUT of each file to `<url>/<name>` (basic login, no
  redirects, `Content-Type` by extension), or one e-mail through the team's own SMTP server
  (`smtp://` with STARTTLS when offered, `smtps://`; MIME text plus base64 attachments). The password is
  typed per run and never stored. `on_walk_end`: when a walk test stops, the floor's CSV goes to
  `<project folder>/exports/` and is sent. `main()` installs the libcurl sender; without it (screenshots,
  tests) network methods are refused with the reason. Tests use a local folder and fake HTTP/SMTP
  servers on the loopback interface only (`tests/app/test_report_export.cpp`).
- Tests: `tests/app/test_report_export.cpp` (model, sharing, libcurl to the fakes),
  `tests/app/ui/test_ui_report.cpp` (clicking), the UI lint visits every new dialog; screenshots
  `export_*` and `report_*`.

## Remote API and the probe plugin (step 11.1)

The probe feature plugin (`probe.so`, PLG-24) is loaded like any other
plugin; the app gives it two host services of plugin API 1.1:

- `data_folder`: `<user data>/plugin-data/<plugin id>` (the loader's
  `data_root`, set in `AppState::ensure_plugins`), where the probe keeps
  its token file and its summaries.
- `control`: `AppControl` (app/model/app_control.h), set on the loader
  before `load_all`. A plugin calls it on its own thread, so it never
  waits for the UI thread: `status` is answered from the copy that
  `AppState::tick()` stores each frame (application, version, source
  link, state, monitoring, recording, sound, radio), and `record.start`,
  `record.stop`, `monitor.on`, `monitor.off`, `sound.on` and `sound.off`
  are queued (reply `{"queued":true}`, at most 32) and run by
  `AppState::run_remote_commands()` in `tick()`, with the outcome on the
  status line. `play`, `rotate` and `quit` are refused: they belong to the
  headless runner `yantra-probe` ([PROBE.md](PROBE.md)).

The walk test's pipeline and the replay's player (ReplaySession::
add_observer; not the pass that fills the graphs) each have a
`measure::FeatureFeed` observer, so feature plugins see the live and the
replayed records (`on_record`) for their live view, as with the headless
runner's `play` (step 11.2). The
probe's window, Tools > Remote probes, starts and stops this app's API and
follows other probes (the central viewer, [API.md](API.md)). The app
opens no socket without the probe plugin, or with it but without
`api.listen`. Tests: tests/app/test_app_control.cpp (the replay case feeds the C test
plugin's feature), the UI lint (the window), and tests/api (TST-51, on
the headless runner).

## Screenshots and layout check (step 9.11)

The documentation's screenshot set (SCR-01..05, SCR-07) is made by the app itself:

    yantra --screenshot FILE --shot NAME [--theme dark|light|high-contrast] [--language en|nl|fr|de]
           [--touch] [--scale PERCENT] [--size WxH] [--input SHEET]
    yantra --list-shots          # name<TAB>flags<TAB>what; flags demo, touch, radio=..., input

- `app/gui/screenshot.{h,cpp}`: `shot_scenes()` (27 scenes: start menu, survey with graphs, events and
  scanner, plan import of two floors from one sheet, walk test, interpolation, About, reset, radio list,
  the four generated windows of the simulator and of the HackRF, survey settings, cell database,
  verification, reselection, the Export and Report dialogs, Tools > Remote probes (step 11.2;
  `ShotScene::plugin` names the feature plugin it needs, flag `plugin=` of `--list-shots`), touch layout), `shot_settings()` (theme/touch/layers of a scene) and
  `ShotRunner`: a list of steps (open the demo, replay to 60 %, start the simulator, click a control
  by id, wait frames) played before each frame; `ready()` when the capture may happen. A step that
  waits more than 120 s fails the shot (exit 1). Before the capture the mouse leaves the window (no
  tooltip); the status bar shows a fixed disk and power state (`AppState::fix_system_status`) so the
  pictures do not depend on the machine.
- `tools/screenshots/screenshots.py` renders every scene in the dark and light theme
  (`<scene>_<theme>.png`, the survey also as `survey_<nl|fr|de>_<theme>.png`) on a private Xvfb with
  a private HOME/XDG under the fixed `/tmp/yantra-screenshots` (it shows in the project window), the
  HackRF scenes on the fake SoapySDR device (`hackrf_fake.so` of the build tree), the plan import on the
  two Fraunces sheets, which the app combines into one (`--input data/demo/fraunces`); then oxipng
  (Python's standard library only). `--compare DIR` fails on a new, missing or
  changed picture (more than `--threshold` % pixels, SCR-05). Data: the Fraunces demo and the simulated
  radios only, never a MAK sheet (DEMO-12). The set is `docs/img/` (58 pictures, about 6.2 MB),
  referenced by the manual; `.github/workflows/screenshots.yml` regenerates it on release branches
  (artifact) and compares it on pull requests.
- Tests: `tests/app/test_screenshots.cpp` plays every scene in the harness; the launch tests
  `app_shot_*` render three with OpenGL; `app_shot_errors` the CLI refusals; the opt-in
  `screenshots_compare` runs the script against `docs/img`; `doc_images` (`tools/ci/check_doc_images.py`)
  checks that every manual image exists and every image in `docs/img` is used. The in-app help leaves
  the image lines out.
- Layout check (TST-50): `tests/app/ui/test_ui_lint.cpp` runs the UI lint (`ui_lint.h`: tooltips,
  disabled reasons, overlap, labels cut off, squashed controls, touch targets) over every window on a
  1280 x 800 desktop and touch layout in every language, and in English on a 1024 x 640 window, a
  full-HD screen at 150 % and a 7-inch tablet (1024 x 600, touch). Widths are set with
  `gui_detail::px()` so they follow the scale.
- The side panel's pages (`panel.layers/edit/walk/map/survey/post`) are a wrapping row of buttons, not
  tabs, so their labels stay readable on narrow and touch layouts.

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
