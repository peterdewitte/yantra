<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Changelog

What changed in Yantra per release, for users (DOC-16), with real dates.
Changes of the plugin API are in
[docs/PLUGIN_API_CHANGELOG.md](docs/PLUGIN_API_CHANGELOG.md). The radio
parts (radio API, radio plugins, generator; formerly yantra-radios) are
part of this repository since the merge (PLG-29): their changes are
listed here, and their history before the merge, with how the radio
API is versioned (RAD-11), in the section
[Radio parts before the merge (yantra-radios 0.1.0-0.4.0)](#radio-parts-before-the-merge-yantra-radios-010-040).

## [Unreleased]

A simpler indoor workflow (specification 0.4 and 0.4.1): the project
window has a Walk and a Map tab, the coverage map is interpolated from
the measured points only, and the demo uses fixed scenes. Then
specification 0.5: an About building tab, notes on the plan, a Report
ready window, one Plan and properties button, a failing demo report that
names its weak area, Tools > Spectrum (radio API 1.2) and the packages
workflow. The version stays 0.2.0 until the next release. Not run for
this state: the sanitizer trees and the Windows tests (the Windows tree
is cross-built and packaged).

### Added

- The About building tab (IND-47), the first tab of every project: the
  address, Find on map (OpenStreetMap's Nominatim, only on request), an
  OpenStreetMap map of the position (tiles through libcurl, cached in the
  user's cache folder, with the attribution; "No map" offline without
  cached tiles), a photo of the building from a file or taken with a
  camera (Yantra's own capture code: V4L2 on Linux, Media Foundation on
  Windows), the surveyor, the survey date and notes. The report starts
  with this information (EXP-05); the report's surveyor field replaces
  the tab's surveyor only when filled in. Project schema 9. The tour has
  an About building step. Manual 13.6.
- Notes on the plan (IND-48): a long press (about 0.6 s) on a spot of the
  plan opens the Note window there, with a picture (file or camera), a
  text and an audio recording (record, play, delete; at most 2 minutes,
  File > Settings > Survey sets a shorter limit). Notes show as numbered
  pins (Map tab > Show > Notes), open with a click and move by dragging;
  the report marks them on the floor's plan and lists them. The demo
  floors have one note each. Project schema 10. Manual 13.7.
- The Report ready window (UI-22): when a report is written, Open shows
  it in the system viewer (the PDF first), Show in folder opens its
  folder, Close closes the window.
- A failing acceptance profile names its weak areas in the report (EXP-06):
  the points below the threshold that lie together, by floor and by their
  place on the plan ("First floor, centre of the plan"), with a table.
- Tools > Spectrum (UI-24, radio API 1.2): the spectrum of the TETRA band
  and a waterfall of the last two minutes from a radio with the SPECTRUM
  capability (the simulator, HackRF and other SDR radios), with the
  serving and neighbour carriers marked, the band in MHz (380-400 by
  default, narrowed to what the radio measures; an SDR shows the span it
  receives at once) and the flags SIMULATED, OVERLOAD and UNCALIBRATED.
  The radio is asked for spectra only while the window is open; spectra
  are not recorded. The Tools menu is now always there (Spectrum, then
  the plugins' entries). Manual 5.6.

- The packages workflow `packages.yml` (TST-64): Linux x86-64 and
  Windows x64 (cross-compiled) with clang from the cross SDK, the fast
  tests on Linux and on a Windows runner, the .deb, the setup .exe and
  the portable zip as artifacts, and a GitHub release with them on a
  version tag. The Windows jobs of `ci.yml` moved to the reusable
  `windows-cross.yml`, the SDK steps to `.github/actions/linux-sdk`.

### Changed

- The Fraunces Tavern demo has a weak inner area (DEMO-16): the first
  floor's stairwell is a closed core of the fixed demo scene, its walk
  loops through it, and the demo report's verdict is FAIL (90.8 % of the
  points at or above -95 dBm, against 95 %), naming the first floor's
  weak area. The sample report was regenerated (DEMO-06).
- One Plan and properties... button per floor (UI-23) opens the plan
  import window with the floor selected and its name, level and height in
  a Floor properties section at the top (Apply); a floor without a plan
  gets the properties alone.
- The plan import window (UI-20): Info next to the file name (file type,
  page, size, resolution; an example plan's credit and licence), a "(?)"
  on every trim option, Add as floor... asks in a popup, the floors of
  the sheet are listed above the preview, Scale and Alignment are closed
  until opened.
- The Walk tab (UI-21): the radio in use once with its state and
  Select..., the walk test kind as radio buttons with a "(?)" each, the
  Recording and Replay sections closed until opened.
- Floor tabs are dragged to reorder the floors; a tab's menu has Move up,
  Move down and Remove... (IND-04).
- The project window has two tabs, Walk and Map (UI-18, UI-19); the
  Layers, Edit, Survey and Post pages are gone. Walk holds the radio
  (Find radios, Start radio, Stop radio), the live values of the serving
  cell with a small level graph, the walk test, the recording controls
  and Replay. Map has a Show row (plan, points, map, and the map's opacity,
  85 % by default), Compute map and an Advanced dropdown with the
  interpolation settings, the binned squares, the post-processing views
  (PP-02..05) and the acceptance check (EXP-06). The layers are drawn in
  a fixed order (plan, map, binned squares, points); the drawing-order
  arrows are gone.
- The survey windows Neighbours, Graphs, Scanner, Events and Statistics
  open from the new Window menu; each item opens or closes its window and
  is ticked while the window is open. The coverage threshold is set in the
  Statistics window. The report is made from File > Report (UI-14).
- The guided tour follows the Walk and Map workflow and has a step that
  shows where the MAK plan sheets were saved, with Open folder (DEMO-11).
- Every demo explanation (the tour's window and strips, the tour offer,
  the mouse-survey hint) has a green background in every theme, with text
  of at least 4.5:1 contrast (DEMO-15).

- The coverage map of a floor is an inverse-distance-weighted (IDW)
  interpolation of its measured points (IND-41): grid 0.5 m, power 2,
  search radius 10 m by default, all set in the Interpolation settings
  dialog and stored in the project; a grid cell holding a measured point
  takes that point's level (the median when several). It no longer uses
  walls, antennas or a fitted propagation model. The confidence mask
  fades from 2 m to the search radius (IND-43); it runs in the
  background with progress and cancel (IND-45). The map, the mouse-survey
  click (DEMO-14), the report (EXP-05) and the demo projects use it; the
  report gives the method and its settings instead of the fit quality.
  Project schema 8: the ray-tracing settings and stored ray-tracing maps
  of older project files are dropped when they are opened (a backup of the
  old file is kept; run the interpolation again).
- The simulator gets its signal levels from fixed demo scenes shipped
  with the demo data (DEMO-05, DEMO-12, DEMO-13, PLG-27): one scene file
  per bundled example floor (the Fraunces Tavern and the three MAK
  buildings) in `data/demo/scenes`, exported once from the example
  descriptions with the scene exporter of Yantra 0.2.0 and installed to
  `share/yantra/demo/scenes`. `get_scene` serves them by the floor's
  example id; a user's own floor has no scene (NOT_FOUND: the simulator
  uses its own defaults) and its walls and antennas no longer reach the
  simulator. The demo recordings and the simulator's demo levels are
  unchanged (byte-identical). `yantra-demo` needs `--scene-dir`.

### Removed

- Walls, wall classes (and their attenuation tables), antennas, slab
  attenuations, the ray tracer and the inter-floor propagation are gone
  (spec 0.4 drops IND-11, IND-12, IND-40, IND-42, IND-44 and TST-36): no
  Edit page, no wall or antenna layers, no proposed walls, no estimated
  sources. The demo descriptions no longer hold walls or antennas (the
  simulator keeps them in its fixed demo scenes); a cell can be removed
  from the cell database at any time. The DXF drawing (EXP-03) and the
  floor image hold the measured points only; `yantra-building` JSON
  ignores the old members.
- Opening a project of Yantra 0.2 or earlier (project schema 7 or lower, IND-46) drops its walls,
  wall classes, antennas and ray-tracing maps, after a byte-for-byte copy
  of the old file (`<file>.schema<N>.bak`), and says once in the status
  line how many of each were dropped and where the copy is. Floors,
  plans, scales, reference points, cells, walk tests and points are kept.

### Fixed

- Windows: the camera code (Media Foundation) loads its DLLs when a
  camera is first used instead of importing them, so the package passes
  its DLL import check and Yantra starts on Windows editions without
  Media Foundation (no camera then).
- In the Walk tab a long radio name and the walk kind end in "..." with
  the whole text on hover, and labels move above their field in a narrow
  panel, so no text is cut off in any language (TST-52).
- The scanner view's State column shortens a long list of flags (such as
  "no_sync,simulated,late" when the app runs slowly) with "..." and shows
  the whole list on hover, instead of cutting it off (UI-13, DOC-07,
  TST-52); the GUI text test now draws the scanner with every flag set.
- Cycles built together by one late tick take their UTC from one reading
  of the system clock, so a wall clock being set or slewed while recording
  no longer makes the replay of that recording differ ("replay mismatch:
  cycle N: time"); the mismatch message now shows the recorded and
  replayed times (OA-13, REC-07).
- The self-interference warning no longer comes on by mistake in the
  demo or next to a strong indoor DAS carrier: the noise is the least of
  level - SNR over the carriers (a strong carrier's SNR is the receiver's
  own limit, not its noise), a noise rise needs a carrier below that
  limit, and unsynced carriers at the noise or in the skirts of a much
  stronger carrier do not count (NFR-07, DEMO-01).
- Reports, the UI, its translations, the plugins' texts and the demo
  projects' notes no longer show requirement IDs such as "(IND-43)"; the
  UI lint fails on one (DOC-07, DOC-20).

### Documentation

- The user manual, the quick starts (English, Dutch, French, German), the
  README and the developer and file-format documents describe the Walk
  and Map workflow, the Window menu, the Map tab's Advanced dropdown,
  File > Report, the IDW coverage map, the fixed demo scenes, the opening
  of older projects and the green demo explanations (DOC-02, DOC-02a).
  The manual's chapter 1 names the Windows packages, the WinUSB driver
  and the Windows folders; the PDF manual's index has the new terms.
- The manual, the quick starts (all four languages), the README, the
  architecture and the project file format (schema 10) describe the
  About building tab, the notes on the plan, the Report ready window,
  the Plan and properties button, the weak area of the demo and
  Tools > Spectrum; the PDF manual's index has their terms.
- Screenshots, the indoor-workflow GIF and WebM show the new project
  window with the About building tab and the demo notes (SCR-03, SCR-06).

### Radio plugins

- 2026-11-18: pinned to yantra-radios **v0.4.0** (commit 2526090), radio
  API 1.2 (RAD-140): an optional SPECTRUM capability, the plan flag
  `YR_PLAN_SPECTRUM` with a band, and spectra (power per frequency bin)
  through the host table's new `push_spectrum` sink. The radio host checks
  and counts every spectrum and offers it to the app; plugins of radio API
  1.0 and 1.1 still load. The simulator, HackRF and SoapySDR plugins have
  the capability, the PEI terminal and the beacon do not. Record and scene
  files are unchanged. The sample report names yantra-gen 0.4.0 and was
  regenerated (DEMO-06).
- 2026-11-17: pinned to yantra-radios **v0.3.2** (commit d6e8c41): a
  floor without a scene (a user's own floor, PLG-27) means "no scene, use
  the defaults" to the simulator, which says so once in its log at INFO
  instead of a warning at every start; `yantra-radio-host --no-scene` and
  a kit check for it. Radio API 1.1, record files and scene files are
  unchanged. The sample report names yantra-gen 0.3.2 and was regenerated
  (DEMO-06): its floor maps no longer draw the walls and antennas
  removed above.
- The radio parts are part of this repository (PLG-29, RAD-141): merged
  with their history, then laid out plugin-first (`radio-api/`,
  `plugins/`, `radio-common/`, `gen/`) and built by the same CMake
  project as the application (one configure, one build, one ctest, tests
  labelled by area); `-DYANTRA_RADIO_PARTS_ONLY=ON` (preset
  `radio-parts`) builds the radio parts alone. Some radio test names
  changed so every name is unique (`fuzz_radio_seeds`,
  `fuzz_radio_record_file`, `doc_snippets_radio`, `test_radio_api_header`,
  `test_radio_host_kit`).

## [0.2.0] - 2026-11-12

Yantra for Windows x64 next to Linux, built with clang from one pinned
LLVM release; the simulator in dBm; the GUI checked for cut-off text in
every language; the user manual as one PDF; yantra-radios v0.3.1.

### Documentation

- The user manual as one PDF, `yantra-manual-<version>.pdf` (DOC-02a):
  title page with the Yantra logo, contents with page numbers, the
  manual's section numbers, bookmarks, links for the contents and the
  section references, numbered screenshots, an index of the main terms,
  windows and menus, the Inter font embedded, tagged for accessibility.
  The docs build checks it (bookmarks, links, pictures, fonts, index, no
  text past the margins). It is in the .deb
  (`/usr/share/doc/yantra/`) and the Windows zip and setup
  (`share\doc\yantra\`), and a release artefact of its own.
- Help > User manual (PDF) opens it in your PDF viewer, also offline; the
  entry is greyed, with the reason, when the PDF is missing (UI-14).
- The manual stays English only; the quick starts are in English, Dutch,
  French and German (DOC-06).
- BUILD.md: the Windows test runs in the test VM with their exact
  commands, the PDF manual, making a release.

### Windows

- 2026-11-11: Windows x64 packages (NFR-09): the portable zip
  `yantra-<version>-windows-x86_64.zip` and the setup
  `yantra-<version>-windows-x86_64-setup.exe` (per-user install, Start
  menu entry, uninstaller, silent with `/S`), made on Linux from the cross
  build. They hold the app, the probe, the plugin test kit, Yantra's and
  the radio plugins, the radio tools `yantra-hackrf` and
  `yantra-radio-host`, every DLL the programs import (the MSYS2 UCRT64
  runtime and libraries, PDFium), the data and the licence texts, with a
  note on the corresponding source. Every program and DLL in them imports
  only packaged DLLs and Windows system DLLs (checked by the build).
- `yantra.exe` carries the Yantra icon (BRD-06) and version information,
  and opens no console window; started from a console it prints there.
- The Fraunces demo's plan-sheet notes (source and licence) are installed
  with the demo project.
- The packages (Windows and the .deb) hold the TETRA test-signal
  generator `yantra-gen` of yantra-radios again, with its scenario files
  beside it (`share/yantra-radios/scenarios`), where it finds them by
  itself.

### Radio plugins

- 2026-11-11: pinned to yantra-radios **v0.3.0** (commit 6fac3ee): radio
  API 1.1, Windows x64 plugins, the terminal plugin on Windows serial
  ports, the PDF plugin manual. Yantra loads radio API 1.1 plugins.
- 2026-11-12: pinned to yantra-radios **v0.3.1** (commit 9c6d3c9):
  yantra-gen finds its scenario files relative to the program (no folder
  of the build machine compiled in); plugins and file formats unchanged.
- The simulator reports dBm by default ("Levels" setting, dBm or dBFS):
  such levels, and every level a radio flags as dBm, are shown as dBm and
  need no calibration; the calibration table applies to dBFS levels only
  (HW-01b). The device information names dBm devices.
- Record files with the flag `level_dbm` (radio API 1.1) are read.

### Projects

- The project file stores the paths of its referenced plans and its
  recordings relative to its own folder when they are on the same drive,
  so a project folder can be moved or shared; Save as into another folder
  keeps naming the same files. Projects with full paths from 0.1.0 still
  open (IND-01).

### Surveying

- The mouse survey with the simulator no longer applies an automatic
  practice calibration (DEMO-14): the simulator's dBm levels need none.
  With the simulator set to dBFS the points are uncalibrated until you
  calibrate it with the calibration wizard (practice calibration); the
  Walk page names the unit.
- A mouse-survey point takes the simulator position of its serving cell's
  record, whose level it shows, instead of the first record that has one:
  a held scan channel the busy simulator had not measured again for a
  while kept an old position, so on a loaded computer a point could lie
  metres from where its level was computed (RAD-19).

### Layout and languages

- The name and notes fields of the survey window's Recording section (and
  the marker label and the ring buffer's save name) show what they are
  for while empty.
- No text is cut off, runs past its window, column or button, or covers
  another in any UI language, in the desktop and touch layout, at
  1280 x 720 and at UI scales up to 200 % (TST-52): windows, dialogs and
  the side panel grow with the font; rows of buttons and switches wrap
  when there is no room; texts wrap at their window or table column; the
  status bar shortens a long message with "..." (the whole message on
  hover) and puts its items on more lines when needed; a long project or
  file path is shortened the same way; a shortened table header shows its
  whole name on hover; the project window scrolls when it is too low for
  the plan and the side panel. Map labels and marks on the plan grow with
  the UI scale.
- A pseudo-language for checking layouts (`--language pseudo`, not
  offered in Settings): every text 40 % longer, in brackets and with
  accented letters, plugin texts included.

### Reports

- A report's long words wrap in the HTML version; in the PDF the floors
  table's columns are wider for the translated headers and a chart's y
  axis label longer than its axis is set smaller (TST-52). The sample
  report is regenerated.
- The statistics header says "Share of measured points (%)" next to
  "Share of floor area (interpolated) (%)", in the report and on the Post
  page alike, also in Dutch, French and German (IND-43). The sample report
  is regenerated.

### Radio menu

- The Radio menu lists every loaded radio plugin; a plugin without a
  usable device is greyed out and its tooltip says why: not searched yet,
  no device found, the device is in use by another program or not
  accessible, or the plugin is switched off (PLG-16).

### Plugins

- Plugin API 1.2 (compatible): a neighbour entry names its cell by its
  own location area and network code when it carries them, else by those
  of the cell that sent the list (PLG-05). The TETRA plugin uses it, so a
  neighbour of another network or location area is found in the cell
  database by its own MCC, MNC and LA.

### Building

- New test (TST-52, label `slow`, `test_app_overflow`, Linux and Windows):
  every window, dialog, menu, tab, tooltip and radio window, in every UI
  language and the pseudo-language, both layouts and themes, at
  1280 x 720 and 2560 x 1440 at 100, 150 and 200 % UI scale, and the
  reports in every language: no text clipped, past its box or over
  another. It replaces the TST-50 layout check of the UI lint. A failure
  saves a picture of the window with the text outlined.
- clang from the cross SDK of yantra-radios is the default compiler of
  every build tree; gcc is the second Linux tree (preset `gcc`). CI builds
  both (BUILD.md).

## [0.1.0] - 2026-11-09

The first release of Yantra (version of the CMake project, 0.1.0; git tag
v0.1.0).

Nothing is sent over the network unless you choose a sharing method
(folder, HTTP upload or your own mail server, EXP-07). The only other
network use is the download of a MAK example plan, and only when you
agree to it; the probe plugin's remote API listens only when you set an
address.

### What you can do with Yantra 0.1.0

- Survey a building indoors: import floor plans from PDF or image files,
  trim several floors from one sheet, set the scale and a common reference
  point, draw walls (or let Yantra propose them from the plan's dark
  lines) and place antennas and cells.
- Measure TETRA networks with a HackRF, an RTL-SDR or Airspy through the
  generic SoapySDR plugin, a network radio (SoapyRemote), a TETRA terminal
  over its PEI port, or the built-in simulator without any hardware; Yantra
  shows the serving cell, neighbours, levels, quality, C1 and the broadcast
  parameters, never subscriber data, voice or messages.
- Walk a floor while Yantra records (continuous or point by point), cut
  and replay recordings, add voice notes, and see the measured points,
  statistics and a binned view on the plan.
- Calibrate a radio against a known signal (or practise with the simulated
  radio) so levels are absolute.
- Keep a cell database per project, check the received broadcast
  parameters against it and simulate cell reselection.
- Predict coverage with the ray tracer, fit it to the measured points and
  fill the gaps between them (interpolation with confidence), and find
  sources that no planned cell explains.
- Check acceptance criteria per floor or for the whole building and write
  a coverage and acceptance report (PDF and a single HTML file), export
  CSV, DXF and floor pictures, and optionally send the results by folder,
  HTTP upload or e-mail. Nothing is sent unless you choose a method.
- Learn with the Fraunces Tavern demo project, the guided tour, the three
  MAK example buildings (the plans are downloaded only when you agree) and
  the mouse survey.
- Run a headless probe (`yantra-probe`) and watch it remotely through the
  optional probe plugin's REST/WebSocket API; without that plugin, or until
  you set an address, Yantra opens no network socket.
- Use the app in English, Dutch, French or German, in a dark or light
  theme, with a touch layout for tablets; every control has a tooltip and
  "(?)" help that opens the manual.
- Extend Yantra with plugins (plugin API 1.1, documented with a tutorial
  and a test kit).

### Documentation in this release

- The user manual, three quick starts (first indoor project, walk test and
  acceptance, calibrating a radio), the method note and the legal and
  scope note. Help > Quick starts opens the quick starts.
- Every stored file format is documented in docs/formats/ (project file,
  recordings, configuration profiles, cell database, calibration table,
  building model and scene export, stored interpolation maps, trims,
  exports, acceptance profiles), with examples that the tests check.
- ARCHITECTURE.md, BUILD.md and the remote API reference (docs/API.md).
- The manual as one HTML page and a PDF, the quick starts and the notes as
  HTML and PDF, and the plugin API reference, built from the Markdown with
  `ninja docs` (every link and picture checked); the .deb ships them in
  /usr/share/doc/yantra/html.
- The quick starts in Dutch, French and German; Help > Quick starts shows
  the one of the app's language. These translations are not yet checked
  by a native speaker; the English text decides.
- A short animated capture of the indoor workflow (GIF and WebM) in the
  README and the manual.

### Privacy and packaging

- Yantra's programs and plugins no longer contain the folders of the
  computer they were built on (debug information and source file names
  are relative); a test checks the installed files.
- The pictures in the manual show a plugin's file name without the local
  folder it was loaded from.
- A Debian package (.deb) built by Yantra's own script (`ninja deb`): the
  app, the probe, the plugins with the self-contained radio plugins and
  their udev rules, the desktop entry and icons, the demo project and the
  documentation. Maintainer Curtat BV, homepage https://curtat.be, source
  https://github.com/peterdewitte/yantra. Windows packages come later.
- A settings file of another kind or of a newer Yantra is no longer read
  as settings: the defaults are used and the reason is shown.

### Radio plugins in this release

- yantra-radios **v0.2.7** is the pinned release (radio API 1.0); its
  history is below under "Radio plugins (yantra-radios)".

The dated entries below list the changes as they were made during the
development of 0.1.0.

### Indoor projects

- 2026-11-05: the indoor project file `*.yantra` (an SQLite database,
  documented in docs/formats/PROJECT.md, schema version 1): project name,
  site, operator and notes; the building's floors with name, level and
  height, ordered by level, renamed, reordered and removed; a copy of
  every plan source file (or, for licence-restricted plans, a reference by
  SHA-256). Every change is stored at once and survives a crash; New and
  Save as write whole files; a project is open in one Yantra at a time;
  older files are migrated after a backup, newer and damaged files are
  refused with the reason.
- 2026-11-05: plan sources (IND-02): PDF plans (any page, through a
  pinned PDFium release) and every image format stb_image reads (PNG,
  JPEG, BMP, GIF, TGA, PSD, PNM, HDR); a file that cannot be read is
  refused with the reason. Large sheets render in tiles at the detail the
  zoom needs, within a memory budget. Floors are trimmed non-destructively
  from their source (IND-05..07): a rectangle or a polygon, quarter turns
  and a fine deskew; several floors from one sheet and several sheets per
  project (IND-06, IND-10); trims are kept in the project file (schema
  version 2, older files are migrated) and can be edited later. The plan
  import window itself comes with the desktop UI.
- 2026-11-05: project schema version 4: the project lists its recording
  files (name, size, SHA-256, header) and stores its walk tests and
  manual-point sets per floor (several per floor, with name, date, radio,
  calibration and notes) and their measured points, with the plan
  position in plan units and in metres; metres follow the floor scale, a
  trim edit keeps the points where they were measured on the sheet, and
  a floor with walk tests is not removed. Older files are migrated.
- 2026-11-05: walk tests on a floor plan (docs/SURVEY.md): point and
  click and continuous mode with the samples spread evenly between the
  clicks, pause and delete last segment (GEO-02); manual points averaged
  over N seconds as power (GEO-05); a floor change in the middle of a walk
  goes on in a new session on the other floor; with the simulator, the
  walk position is where it measures. Every action is recorded, so a
  replay gives the same points. One point list per floor for the project
  window and the report, never thinned (IND-30), with coverage
  statistics: shares above thresholds, percentiles, per area, per floor
  and for the building, serving-cell changes and acceptance profiles
  (EXP-05, EXP-06, PP-02).
- 2026-11-05: the cell database (docs/CELL_DATABASE.md), kept in the
  project's cells (project schema 5): site, sector, azimuth, graph
  colour, notes, broadcast and reselection parameters and up to 31
  neighbours per cell; cells identified by LA, by channel or by both.
  CSV import and export with LA in hex or decimal and channel numbers;
  rows that cannot be read are reported by line. Received broadcasts are
  verified against it (match, differs, new, missing, per parameter and
  neighbour) with a difference log, can fill or extend it (auto-learn),
  and neighbour relations that are not mutual are found across all cells
  of a survey or project. A reselection simulator shows the serving-cell
  state and the reselection that would occur, also along a walk.
- 2026-11-05: post-processing (docs/SURVEY.md "Post-processing"):
  recordings are loaded into a project and give the same points as the
  live walk test (PP-01), with a hover readout of a point's position,
  serving and strongest cell and every neighbour (the stored points now
  keep the serving cell's announced neighbour list); good/bad against a
  threshold, serving-cell changes and the coverage rate of the tested
  area (PP-02); switching sites off and recomputing the best server and
  coverage from the measured levels (PP-03); a level offset simulation
  (PP-04); a binned view with the median or another percentile per square
  (PP-05). Acceptance profiles can be evaluated over the tested area
  besides the points (EXP-06). A serving cell on another carrier now
  counts as a serving-cell change.
- 2026-11-05: the propagation model (docs/PROPAGATION.md, IND-40,
  IND-44): a ray tracer per floor predicts each cell's level on a grid
  (default 0.25 m) from the drawn walls and antennas, with free-space
  loss, the loss of every wall crossed at the cell's carrier, specular
  reflections (default 2) and the slabs to antennas on other floors,
  summed in power per cell. It gives the levels the simulator plugin
  computes for the same floor, runs on all cores and can be cancelled.
  The coverage map built on it comes with the measured-point fit.
- 2026-11-05: the interpolated coverage map of a floor (docs/PROPAGATION.md
  "Interpolation of a floor", IND-41..45): the propagation model is fitted
  to the measured points (unknown EIRPs, path-loss exponent, wall-class
  losses and the dB per floor, within bounds; the fit quality is shown),
  the remaining differences are spread over the floor limited by walls,
  and a confidence mask fades the map away from the points. A floor
  without antennas gets estimated sources from its points, labelled as
  such. The map always says "interpolated"; its "% of floor area
  (interpolated)" stays apart from the measured-point statistics and is a
  new acceptance basis (EXP-06). Maps run in the background (progress,
  cancel) and are stored in the project with their parameters; a stored
  map is marked stale when the building, the points or the settings
  change. Project schema version 6 (propagation settings and stored maps;
  older files are migrated).
- 2026-11-05: the Fraunces demo project (DEMO-05, DEMO-06, DEMO-13;
  docs/DEMO.md): two floors of the Fraunces Tavern from public-domain
  HABS sheets (Library of Congress), with trims, scale, walls, DAS
  antennas, a fictitious cell database, one simulated walk recording per
  floor and the stored ray-tracing interpolation of each floor. It is made
  at build time by the new tool `yantra-demo` from its description
  (`data/demo/fraunces/fraunces.json`) with the radios' Fraunces scenario
  and yantra-gen, the same on every build, and installed to
  `share/yantra/demo/fraunces`. The demo scenarios (DEMO-09) are listed in
  `data/demo/scenarios.json`. Recordings can have a fixed session id, and
  a walk test can take its start time from the measurement clock.
- 2026-11-05: the three MAK example buildings of the guided tour
  (DEMO-12, DEMO-13; docs/DEMO.md): Beer house and Villa Bunzl in Vienna
  and Haus Claeson in Falsterbo, one description each in `data/demo/mak/`
  (image URL, SHA-256, size, licence and credit; per floor the trim,
  an approximate scale, walls, DAS antennas and a walk route). The MAK
  plans themselves (CC BY-NC-ND 4.0) are never shipped: the example
  projects, made at build time with one simulated walk recording and
  interpolation per floor and installed to `share/yantra/demo/mak/`,
  reference the plan by its SHA-256 (IND-03). A plan file that is not the
  described one is refused with both hashes and the download address.
  `yantra-demo` takes `--sheets DIR` (the cache) or `--reference-sheets`.

### Export and reports

- 2026-11-08: exports (docs/EXPORT.md): CSV with selectable columns,
  separator, UTC or local time and up to N neighbours (EXP-01), DXF per
  floor in metres (EXP-03), the floor image with points and legend as
  PNG or JPEG (EXP-08), batch export of a folder of recordings, merged or
  separate, also through exporter plugins (EXP-04, PLG-02). The rows are
  the project window's points (IND-30).
- 2026-11-08: the indoor coverage and acceptance report as PDF (libharu,
  embedded Inter and JetBrains Mono subsets) and self-contained HTML
  (EXP-05, docs/REPORT.md): cover, measurement metadata, building
  summary, one section per floor with every measured point, the stored
  interpolated map, "% of measured points" next to "% of floor area",
  acceptance profiles on both bases (EXP-06; stored in the project,
  schema 7), histogram and CDF, per-cell statistics, serving-cell
  changes, network verification and neighbour cross-check; the curtat.be
  logo and reference on the cover and every page, the Yantra favicon,
  the DEMO label.
- 2026-11-08: the sample report of the Fraunces demo in `docs/samples/`
  (PDF and HTML, DEMO-06), made by `ninja sample_report`
  (`yantra-demo --sample-report`) and compared again by the ctest
  `sample_report_compare` and CI within a set tolerance.
- 2026-11-08: File > Export and File > Report in the app (manual 10):
  the CSV columns, separator and time, DXF, the floor picture with or
  without the map, the batch export with exporter plugins; the report as
  PDF and/or HTML with the surveyor typed in (never stored), the floors,
  the thresholds and a choice of acceptance profiles, made in the
  background; the acceptance profile editor; the guided tour's report
  step works now.
- 2026-11-08: sending exports and reports (EXP-07, optional): copied to a
  shared folder, uploaded with HTTP PUT or e-mailed through your own SMTP
  server when they are written, and the floor's CSV when a walk test
  ends. The password is never stored.

### Measurements

- 2026-11-05: a self-interference warning: raised noise next to the
  carriers or many carriers without sync for three seconds suggest that
  the antenna picks up the laptop's RF noise; the advice is to keep the
  antenna away from the PC, its USB ports and cables.

- 2026-11-04: the TETRA technology plugin (`tetra.so`): TETRA cell,
  neighbour, scan and terminal records with their own columns, the
  380/410/450 MHz channel raster, C1 (MS power 30 dBm portable, 35 dBm
  mobile) and cell reselection states. A C1 the radio does not deliver is
  calculated from the calibrated level and marked estimated.
- 2026-11-04: the TETRA channel raster covers every band the radios tune
  (350-470 MHz and 790-920 MHz, including 350-370 MHz and 800 MHz TETRA);
  channel numbers are band-qualified (band code x 10000 + carrier number)
  so one number names one frequency.

### Remote API and headless probe

- 2026-11-08: plugin API 1.1: the host services `control` (run an application
  command) and `data_folder` (a private folder per plugin); feature
  plugins are started, stopped and fed the measurement records.
- 2026-11-08: `yantra-probe`, the headless runner (API-01, API-03): the
  measurement engine without a window, commands on standard input (record,
  monitor, play, sound, status, rotate, quit), rotating recording files.
- 2026-11-08: the probe feature plugin (PLG-24): a REST and WebSocket API
  (API-02, docs/API.md) with owner and LAN tokens, a refused foreign
  Origin, a filtered live view (sync, system information, neighbour
  broadcast, levels) and the source link; periodic health and coverage
  summaries (API-03); Tools > Remote probes in the app. Off until an
  address is set: without it Yantra opens no network socket.

### Radio plugins (yantra-radios)

- 2026-11-09: pinned to yantra-radios **v0.2.7** (commit 26c05ff): the
  plugins and tools name no folder of the machine that built them, and
  the installed plugins are checked for it. The plugins behave as in
  v0.2.6. Radio API 1.0 unchanged.
- 2026-11-06: pinned to yantra-radios **v0.2.6** (commit 5394318): the
  simulator change of 0.2.5 is taken back (plugin 0.2.4): a slowly
  simulated record has the time its position was taken again, so its
  sim_position matches its time; such records arrive late and are flagged
  late. Radio API 1.0 unchanged.
- 2026-11-06: pinned to yantra-radios **v0.2.5** (commit ba7814c): the
  simulator (plugin 0.2.3) keeps its record times on the host clock when
  the computer simulates slower than real time; before, each record came
  an interval's processing time after its time, was flagged late and
  stale, and on a loaded machine the serving carrier left the cycles
  between two records. Radio API 1.0 unchanged.
- 2026-11-05: pinned to yantra-radios **v0.2.4** (commit c41718a): the
  TETRA terminal plugin (pei 0.1.1) no longer takes the late reply of a
  stopped session as the answer to the next session's first command, so
  a self-test no longer passes a terminal that does not answer; the SDR
  tests are limited in signal time. Radio API 1.0 unchanged.
- 2026-11-05: pinned to yantra-radios **v0.2.3** (commit 547d834): the
  simulator calculates a project cell on the carrier of one of its
  default cells (390.0 or 390.1 MHz) from the drawn floor; before, the
  default cell kept the carrier and the records showed no walls. Radio
  API 1.0 unchanged.
- 2026-11-05: pinned to yantra-radios **v0.2.2** (commit a78c5c1): the
  SDR plugins (SoapySDR, HackRF) return from stop within the 1 s control
  call limit also on a slow or loaded computer; before, stopping (or a
  replug restart followed by a stop) could take seconds and the plugin
  was disabled. Radio API 1.0 unchanged.
- 2026-11-04: pinned to yantra-radios **v0.2.1** (commit f359ee1): the
  simulator plugin measures every cell of its scenario's capture, not only
  the carrier at the capture centre, and a carrier pinned twice (a given
  serving carrier) gets one decoder, so one record per carrier and
  interval; radio API 1.0 unchanged.
- 2026-11-04: pinned to yantra-radios **v0.2.0** (commit 205ca7a): scene
  format 1.1 (per-band wall attenuation, storeys with their own height and
  slab attenuation, wall polylines whose vertex is crossed once); radio API
  1.0 unchanged, so every radio plugin keeps loading.
- 2026-11-04: first pin, yantra-radios **v0.1.0** (commit fb194f5): radio
  API 1.0 with the HackRF, generic SoapySDR, simulator and TETRA terminal
  plugins.

## Radio parts before the merge (yantra-radios 0.1.0-0.4.0)

The changelog of yantra-radios (RAD-98) and the radio API changelog
(RAD-11) up to the merge into this repository (PLG-29), kept as it was;
its releases are tagged `radios-v0.1.0` .. `radios-v0.4.0`. Paths in it
are those of yantra-radios (`include/`, `receiver/`, `common/`, `tests/`
are now `radio-api/include/`, `radio-common/receiver/`,
`radio-common/radios_common/`, `tests/radio/`). Later radio API changes
are listed in the sections above.

### How the radio API is versioned

The radio API (`include/yantra/radio.h`) has its own version
`MAJOR.MINOR` (`YR_API_VERSION_MAJOR`, `YR_API_VERSION_MINOR`), separate
from the release version and from Yantra's plugin API. Yantra loads a radio
plugin only when the plugin's major version equals its own and the
plugin's minor version is not newer than its own; otherwise it refuses
the plugin with a message that names both versions (RAD-11).

Every change to the radio API is listed below under "Radio API" and
marked:

- **compatible**: an addition that older plugins and hosts survive (a new
  member at the end of a versioned struct, a new optional callback, a new
  constant, schema field or event). Raises the minor version.
- **breaking**: anything else (a removed or reordered member, a changed
  meaning, type or calling rule). Raises the major version; plugins must
  be rebuilt.

Record file (RAD-65) and scene JSON (RAD-36) versions are listed here as
well; they change independently of the radio API version.

### Unreleased at the merge

#### CI

- `release.yml` is the packages workflow: also by hand and on pull
  requests that touch the packaging (workflow artifacts only, version
  v<project version>); a tag must name the project version. The Windows
  zip is made by the reusable `windows-cross.yml` (shared with `ci.yml`),
  so the release's Windows tree is also tested on a Windows runner.

### [0.4.0] - 2026-11-17

Tag v0.4.0 (RAD-98). Radio API 1.2 (compatible): spectra (RAD-140). The
simulator synthesises a spectrum from its scene, the HackRF and SoapySDR
plugins compute one from their own IQ samples; the test host shows and
writes spectra and the conformance kit checks them. Record files 1, scene
JSON 1.1 and scenario files 1 unchanged (record files name the new
capability and plan flag `spectrum`). Plugin versions: simulator 0.4.0,
hackrf 0.2.0, soapysdr 0.2.0, beacon 0.1.1, pei 0.2.0 (unchanged);
yantra-radio-host 0.3.0. Linux only for this release: no Windows build
or test (owner, 2026-11-17; the Windows cross-build follows with Yantra
0.5).

#### Radio API

- **Radio API 1.2** (compatible, RAD-140, RAD-11):
  - capability `YR_CAP_SPECTRUM` (bit 6; `YR_CAP_ALL_MASK` 0x7F, the
    1.0/1.1 bits `YR_CAP_ALL_MASK_1_1` 0x3F);
  - plan flag `YR_PLAN_SPECTRUM` (bit 1) and the band
    `spectrum_first_hz`, `spectrum_last_hz` appended to `yr_plan`
    (56 -> 72 bytes; 0 and 0: the default 380-400 MHz,
    `YR_SPECTRUM_DEFAULT_FIRST_HZ`/`LAST_HZ`);
  - the versioned record `yr_spectrum` (72 bytes): record flags (OVERLOAD,
    UNCALIBRATED, SIMULATED, LEVEL_DBM; `YR_SPECTRUM_FLAGS_MASK`), time,
    `start_hz`, `stop_hz`, `bin_width_hz`, `bin_count` (at most
    `YR_SPECTRUM_MAX_BINS`, 4096) and `power_db` (dBFS, or dBm as the
    records);
  - the sink `push_spectrum` appended to `yr_host_api` (112 -> 120 bytes),
    with the same rules as `push_record`;
  - helpers `yr_plan_read()` (keeps a plan as large as the host's
    `struct_size`, the rest zero), `yr_host_spectrum_sink()` (NULL for an
    older host table or a host that wants no spectra) and the type
    `yr_push_spectrum_fn`; `radio.hpp` `validate_spectrum()` and
    `spectrum_band()`; the capability bits are checked against the
    plugin's API version.
  1.2 hosts load 1.0 and 1.1 plugins (a 1.1 plugin keeps a 72-byte plan
  by its own 56-byte copy); 1.1 hosts refuse 1.2 plugins (newer minor
  version). The 1.0 minimum sizes are unchanged (plan 56, host table 112).
- Only power per frequency bin crosses the API, never IQ (RAD-26).

#### Added
- Simulator: SPECTRUM. Once a second a synthetic spectrum of the plan's
  band within its simulated span: the carriers on the air at the levels
  its records measure, with the TETRA downlink's root-raised-cosine power
  shape, on the scenario's noise, with a seeded jitter (the same seed,
  the same spectrum); dBm with LEVEL_DBM or dBFS as its `level_unit`;
  SIMULATED (RAD-61, RAD-140).
- HackRF and SoapySDR plugins: SPECTRUM. Once a second an averaged FFT
  (pocketfft, RAD-04; about 1 kHz bins, 16 frames) of their own samples,
  the plan's band within the span received at once; dBFS, OVERLOAD on
  clipping; the measurement goes on (RAD-60, RAD-62, RAD-140).
- `yantra-radio-host stream --spectrum`, `--spectrum-band FIRST:LAST` and
  `--spectrum-out FILE` (CSV); `info` names the capability `spectrum`.
- Conformance kit: the `spectrum` check (valid spectra about once a
  second inside the band asked for, flags as the records; none without
  the capability) and, in `records`, no spectrum without the plan flag.
  The terminal plugin (pei) has no SPECTRUM.

#### Tests
- `test_radio_api`: ABI sizes and offsets of `yr_spectrum`, `yr_plan`
  and `yr_host_api`; `validate_spectrum`, `spectrum_band`,
  `yr_plan_read` and `yr_host_spectrum_sink`, also from C.
- `test_radio_host`, `host_cli_stream_spectrum`: spectra through the
  host's sink, and none for a host without one; `test_conformance`: the
  kit passes the fake plugin (now with SPECTRUM) and the beacon, and finds
  five injected spectrum faults.
- `test_simulator`: rate, band, a carrier's bins against its records,
  the noise floor, seeds, the dBFS unit and an own band; `test_hackrf`:
  the fake HackRF's spectra (rate, bins, band, carrier power against the
  records); the kit's `spectrum` check on the simulator and the fake
  HackRF, SoapySDR and SoapyRemote devices.

### [0.3.2] - 2026-11-17

Tag v0.3.2 (RAD-98). "No scene": `get_scene` returning `YR_ERR_NOT_FOUND`
for the floor shown means "no scene, use the defaults" (owner decision
2026-11-17; Yantra 0.4 gives scenes only for its bundled example floors).
Radio API 1.1, record files 1, scene JSON 1.1 and scenario files 1
unchanged. Simulator 0.3.1, yantra-radio-host 0.2.2; pei 0.2.0, hackrf
and soapysdr as in 0.2.6.

#### Changed
- Simulator: a floor without a scene (`get_scene` NOT_FOUND) gets the
  scenario's own levels, logged once per plugin instance and plan at INFO
  ("the floor shown has no scene; the scenario's own levels are used")
  instead of a WARNING at every start and context change; a scene removed
  while streaming (`SCENE_CHANGED`, then NOT_FOUND) rebuilds with the
  defaults. Other statuses and a scene that is not valid JSON keep their
  warnings (RAD-18, RAD-36, RAD-61).

#### Added
- `yantra-radio-host stream --no-scene` (`ContextOptions::no_scene`): the
  emulated plan's floors have no scene, so `get_scene` gives NOT_FOUND.
- Conformance kit: the `context` check also streams on a floor without a
  scene and expects records (RAD-18).

#### Tests
- `test_simulator`: NOT_FOUND gives the scenario's levels, exactly one
  INFO line and no warning, also after PLAN_CHANGED, SCENE_CHANGED and a
  second stream; another plan gets its own line.
- `simulator_cli_no_scene`: the same through the `yantra-radio-host` command
  line; `test_radio_host`: the emulated NOT_FOUND.

### [0.3.1] - 2026-11-12

Tag v0.3.1 (RAD-98). A fix release: no shipped program or library names a
folder of the machine it was built on. Radio API 1.1, record files 1,
scene JSON 1.1 and scenario files 1 unchanged; the plugins' code is
unchanged and they keep their versions (simulator 0.3.0, pei 0.2.0;
hackrf and soapysdr as in 0.2.6).

#### Fixed
- `yantra-gen` no longer has the build's install folder
  (`<prefix>/share/yantra-radios/scenarios`) compiled in as a last place
  to look for scenarios. A build whose prefix lay in a home folder (as
  Yantra's build of this submodule) put that folder into
  `yantra-gen`/`yantra-gen.exe`. The program finds its scenarios only
  with `--scenario-dir`, `$YANTRA_SCENARIO_DIR` and
  `<program folder>/../share/yantra-radios/scenarios`, the folder taken
  from the running program (`/proc/self/exe`, `GetModuleFileNameW`), so
  an installed or unpacked tree works wherever it is (RAD-71, RAD-137,
  RAD-138; Yantra NFR-09).

#### Tests and release
- `shipped_paths`: installs the tree into a scratch prefix and fails when
  an installed file holds the source tree, the build tree, the cross SDK
  or the home folder, or when a program or library holds the configured
  install folders (`tools/ci/check_shipped_paths.py`, with its self-test
  `shipped_paths_selftest`); a build-machine check (`build_host`) in the
  Windows trees.
- `yantra_gen_install`: the installed `yantra-gen` lists its own
  scenarios from another working folder without `$YANTRA_SCENARIO_DIR`,
  again after the prefix is moved, and finds none once they are removed
  (Linux, and the release `.zip` layout in the Windows test VM).
- `package_release.sh` also refuses a package that names the cross SDK's
  folder.

### [0.3.0] - 2026-11-10

Tag v0.3.0 (RAD-98). Radio API 1.1 (compatible: LEVEL_DBM), the
simulator's "Levels" setting with dBm by default and plan cells only,
Windows x64 (`.dll` plugins and `.exe` tools cross-compiled with the new
cross SDK, tested on Windows), clang from the SDK as the default compiler,
the terminal plugin's serial ports on Windows, and the PDF manual.
Removed requirement IDs: the real-signal fixtures, the independent
decoder cross-check and the terminal's call control (below). Record files
stay version 1, scene JSON 1.1, scenario files 1. Plugin versions:
simulator 0.3.0, pei 0.2.0; hackrf and soapysdr as in 0.2.6.

#### Radio API

- **Radio API 1.1** (compatible, RAD-21, RAD-30): a device can report
  calibrated dBm without being SELF_CALIBRATED. New device flag
  `YR_DEVICE_LEVEL_DBM` (in enumerate and device_info: the device's
  current level unit) and record flag `YR_RECORD_LEVEL_DBM` (on every
  record of such a device; a unit change takes effect with the next
  record); `YR_DEVICE_ALL_MASK`, the 1.0 masks `YR_RECORD_ALL_MASK_1_0` and
  `YR_DEVICE_ALL_MASK_1_0`, and `yr_record_level_is_dbm()`. A
  SELF_CALIBRATED plugin of 1.1 sets both flags on every device and record
  (a 1.0 one is dBm by its capability). Hosts of 1.0 refuse 1.1 plugins
  (newer minor version); 1.1 hosts load 1.0 plugins.
- The record validator (`radio.hpp`) checks the flags against the
  plugin's API version and LEVEL_DBM on SELF_CALIBRATED records; the
  conformance kit checks the device flags of the plugin's version, LEVEL_DBM
  on SELF_CALIBRATED devices, and that every record's LEVEL_DBM matches the
  device entry (fault `level_dbm` of the fake plugin).
- Record files stay version 1: new flag name `level_dbm` for records and
  devices (readers ignore flag names they do not know); `plugin.api_version`
  is `"1.1"`. The terminal plugin, `decode-iq --calibrated` and
  `yantra-gen`'s ground truth records carry `level_dbm`.
- Documentation of `get_floor_plan` names every bundled example id
  (`fraunces-N`, `mak-beer-house-N`, `mak-haus-claeson-N`,
  `mak-villa-bunzl-N`) and the scene's plan cell ids (RAD-18).

#### Added

- Generator (RAD-22, RAD-70, RAD-74): a scenario cell may belong to
  another network (`mcc`, `mnc` on the cell); a neighbour of another
  network is announced in D-NWRK-BROADCAST with its own MCC and MNC, and a
  neighbour entry may plant an MCC/MNC (`mcc`, `mnc` on the entry); the
  ground truth records carry `neighbour_mcc`/`neighbour_mnc` when sent. A
  plan cell (scene) of another network keeps its MCC and MNC. New bundled
  scenario `network_border` (cells of 901-1 and 901-2 announcing each
  other).
- Cross SDK (RAD-139): `tools/sdk/yantra_sdk.py make` builds a
  git-ignored SDK folder (`sdk/` or `$YANTRA_SDK_DIR`) from pinned,
  signature-checked downloads: LLVM 23.1.5 (clang, lld, llvm-rc,
  llvm-objdump, compiler-rt), the Windows x64 UCRT sysroot from MSYS2
  UCRT64 packages (mingw-w64 14.0.0, GCC 16.2.0 runtime) and Rust 1.93.1
  for x86_64-unknown-linux-gnu and x86_64-pc-windows-gnu; `check`
  compares every lock file with what is unpacked. CMake toolchain files
  for Linux (clang) and Windows x64 (`x86_64-w64-windows-gnu`, static GCC
  runtime, winpthreads in every link, llvm-rc); `tools/sdk/wsl_run.sh`
  runs a Windows program from WSL2. ctests `sdk_check` and
  `sdk_windows_smoke` (a C++20 + Rust program cross-compiled, its DLL
  imports checked, run on Windows through WSL interop).
- Windows x64 (RAD-131, RAD-132, RAD-133, RAD-136): the whole tree is
  cross-compiled with the cross SDK (presets `windows`, `windows-scalar`,
  `ci-windows`; tree `build-win`): the plugins as `.dll` exporting only
  `yantra_radio_entry`, `yantra-radio-host.exe`, `yantra-gen.exe`,
  `yantra-hackrf.exe`, every test, the Rust code for
  `x86_64-pc-windows-gnu`, the bundled libraries static from the same
  sources (also as a prefix: `build_deps.sh` with the Windows toolchain
  file). ctest runs the `.exe` on Windows through WSL interop
  (`tools/sdk/wsl_run.sh --in-place`); Linux-only tests are reported as
  skipped with their reason (`yantra_skip_test()`). The completeness
  check reads llvm-objdump's PE output and checks programs too
  (`--programs`; tests `completeness_programs`,
  `completeness_programs_shipped`); `msvcrt.dll` and the GCC runtime
  DLLs are refused. Windows golden files for the generator tests
  (MinGW-w64's math functions round some results differently). Release
  package `yantra-radios-<version>-windows-x86_64.zip`
  (`package_release.sh`, `make_release.sh`). CI: `windows-cross` builds
  the tree on Linux, `windows-x86-64-tests` runs its tests natively on a
  Windows runner (`tools/ci/run_windows_tests.py`); `windows-check.yml`
  builds natively with MSYS2 UCRT64's gcc; the release job cross-compiles.
  The terminal plugin's serial ports on Windows: see below.
- Terminal plugin 0.2.0 (RAD-80, RAD-04): one serial-port interface with
  a POSIX implementation (termios) and a Windows one (the Win32
  communications API: CreateFileW on `\\.\COMn` without sharing,
  SetCommState, SetCommTimeouts, overlapped I/O with timed waits). The
  port list on Windows comes from the SetupAPI (COM ports with friendly
  name, USB ids and serial) and the other `COMn` names; on Linux the USB
  ids and serial come from sysfs. Enumerate shows them in
  `hardware_info` and `serial`, and IN_USE with the reason for a port
  another program holds (Linux: the plugin and other programs lock the
  port with flock; Windows: no sharing) until it is free again. Hot-plug
  on both platforms. `YANTRA_PEI_PORTS` is `;`-separated on Windows.
- `yantra-pei-sim` on Windows: the simulated terminal on a named pipe
  (`--link \\.\pipe\NAME`), opened by the plugin through the same Win32
  code; new option `--control FILE` (unplug marker). `test_pei` and
  `conformance_pei` run on Windows (they were skipped there), with new
  tests of the port layer and of IN_USE on both platforms.
- PDF manual (RAD-97): `ninja docs-manual` builds
  `yantra-radios-manual-<version>.pdf` (the plugin pages, the record
  schemas, the guide and its tutorial, with a new overview chapter) with
  pandoc and WeasyPrint: title page, contents with page numbers, numbered
  sections, bookmarks, working links, metadata, embedded fonts; its checks
  fail the build on warnings, missing pictures, unresolved links or
  bookmarks, text past the margin and fonts not embedded (ctests
  `radios_manual`, `radios_manual_faults`). Every release ships it
  (`make_release.sh`, `release.yml`, and in the packages under
  `share/doc/yantra-radios`).

#### Changed

- Simulator 0.3.0 (RAD-61, RAD-94): new setting `level_unit` ("Levels"):
  `dbm` "dBm (calibrated)" (default) delivers calibrated dBm with the
  flag LEVEL_DBM on the device and every record (radio API 1.1); `dbfs`
  "dBFS (like an SDR)" behaves as before. A unit change takes effect with
  the next record, without restarting the simulation. When the plan's
  scene has cells, only those cells are simulated and the scenario's own
  are dropped (before: added to the default scenario); a bundled example
  plan with its own scenario's cells keeps that scenario whole. The
  setting `network` now also applies to plan cells.

- clang of the cross SDK is the default compiler (RAD-03, RAD-139): the
  presets `default`, `scalar`, `ci`, `ci-scalar`, `asan-ubsan`, `tsan` and
  `fuzz` use `tools/sdk/toolchain-x86_64-linux-gnu-clang.cmake` (with the
  SDK's lld, Rust and llvm-symbolizer). The system gcc keeps a second tree:
  presets `gcc`, `gcc-scalar` (`build-gcc`, `build-gcc-scalar`) and
  `ci-gcc`. CI builds and tests both on Linux (`linux-x86-64` with the SDK
  cached per lock-file version, `linux-x86-64-gcc`); `ci-full.yml` runs
  the sanitizer, scalar and fuzz trees with clang; the workflow YAML is
  checked with PyYAML. `tools/ci/run_linux.sh` takes
  `YANTRA_CI_COMPILER=clang|gcc`; `tools/deps/build_deps.sh` rebuilds when
  the compiler changes. No behaviour change.
- The Windows tree's ctest runs natively on Windows (RAD-131, RAD-133;
  BUILD.md "Windows x64"): the presets have no emulator; every test
  command is a `.exe`, `cmake` or `python3`, so the tree runs in a Windows
  test VM that mirrors it to the same paths. The doctest cases of a
  cross-compiled program are listed when ctest runs on Windows
  (`doctest_discover.py --at-test-time`; the build writes one
  `<group>/all` test). The command-line tests that used `sh -c` and
  `with_remote_server.sh` use Python drivers on every platform
  (`tests/test_command.py`, `decode_iq_sigmf.py`, `fixtures_selftest.py`,
  `install_check.py`); the conformance tests unplug and replug with
  `cmake -E touch/rm`. Build-machine checks (SDK, lint, cargo, Doxygen,
  dependency and release scripts) carry the label `build_host`: skipped
  on Windows, run with `ctest -L build_host` on Linux;
  `fuzz_proto_rust` is skipped in the Windows tree. WSL2 interop stays
  optional (`-DYANTRA_WINDOWS_TEST_RUNNER=wsl`; ignored with a warning
  without WSL).
- The completeness check reads the PE import and export tables itself
  (`--objdump` stays optional) and takes the build machine's folders
  with `--build-folder` when it runs elsewhere (RAD-133).
- The cross SDK and its cache may be links to folders outside the tree
  (git-ignored either way; BUILD.md); the SDK's folder is mapped to
  `sdk/` in debug information (RAD-139).

#### Fixed

- Builds without warnings under clang: a C plugin no longer gets
  `-static-libstdc++`; a test expression clang read as a declaration.
- Windows: the GSMTAP output starts Winsock before resolving its address;
  `yantra-gen` finds its scenarios next to the `.exe`; the conformance
  kit's cleanup check counts threads on Windows too; plugins load with
  `LoadLibraryExW` from their own folder first.
- Tests: paths written into JSON text use forward slashes; the generator
  tests link the bundled nlohmann-json (they used the system's headers).
- Windows: `yantra-radio-host conformance --unplug/--replug` commands
  with a quoted program path work (`cmd.exe` quoting).
- Windows: `yantra-gen` and the test programs turn the UCRT's
  CPU-dependent FMA3 math paths off (`_set_FMA3_enable(0)`), so the
  generator's output and its golden digests are the same on every
  Windows machine; the Windows golden files now come from the test VM
  (the `cells_cs16_impaired` digest equals the Linux one).
- Tests: `yantra_gen_stdout_endless` drops the generator's standard error
  after the reader closes the pipe (on Windows it reports the closed pipe,
  where Linux ends it silently by SIGPIPE).

- Terminal plugin on Windows: its timed waits used an absolute
  monotonic time, which winpthreads compares with the wall clock, so
  they returned at once (the watcher and the poll loop spun); they wait
  relative now.
- The `release` target of the Windows tree packages the `.zip`
  (platform `windows-x86_64`); it used the Linux platform name.

#### Removed

- The real-signal fixture framework (`tests/real_fixtures`,
  `tests/fixtures/real`, ctests `real_fixtures` and
  `real_fixtures_selftest`): its requirement was removed from the spec
  (no real recordings are available); the real-world checks stay in
  REAL_WORLD_CHECKLIST.md and DEVICES.md. Likewise the independent decoder
  cross-check and the terminal's call control are no longer requirements:
  the terminal plugin stays receive-only.

### [0.2.7] - 2026-11-09

A patch release (tag v0.2.7, RAD-98) from the release check of Yantra's
phase 12: the release files name no folder of the machine that built
them, and the documentation is current again. No API, format or
behaviour change: radio API 1.0, record files 1, scene JSON 1.1 and
scenario files 1 as in 0.2.0; plugin versions as in 0.2.6.

#### Radio API

- **Radio API 1.0**: no change.

#### Fixed

- Plugins and tools (RAD-133, RAD-137): the files of the release held
  the builder's home folder, in debug information, in libosmocore's
  `__FILE__` and in the Rust crate. Source paths are now relative
  (`-ffile-prefix-map`, `--remap-path-prefix`). The Rust library has its
  own cargo target folder, `<build>/cargo-lib`.

#### Documentation

- README: screenshots of the radio windows Yantra generates (RAD-135).
- DEVICES.md: the test rows measured again on these plugins, with their
  versions; the hardware rows stay open (RAD-95).
- ARCHITECTURE.md: a read reaches the engine in pieces (0.2.2).
  SCENARIO_FORMAT.md: a host's scene cell replaces a scenario cell on its
  carrier (0.2.3). A file path and an example version corrected.

#### Tests

- New: the completeness check has the finding `build_paths`, so a plugin
  naming the source tree or the builder's home folder fails it (ctest
  `completeness_build_paths` proves the finding is raised);
  `package_release.sh` looks for the same folders in every packaged file.
  The asan-ubsan tree is left out of both: gcc's AddressSanitizer keeps
  each source file's full path, and that tree is never shipped.

### [0.2.6] - 2026-11-06

A patch release (tag v0.2.6, RAD-98) that takes back the simulator
change of 0.2.5. No API or format change: radio API 1.0, record files 1,
scene JSON 1.1 and scenario files 1 as in 0.2.0.

#### Radio API

- **Radio API 1.0**: no change.

#### Fixed

- Simulator plugin 0.2.4 (RAD-61, RAD-19, RAD-27): 0.2.5 stamped the
  records of a slowly simulated interval near the interval's end, but
  their levels are calculated for the position read at its start
  (sim_position): a record claimed a later time than its position's, and
  Yantra's walk test saw the simulated position lag the walker by more
  than the record's age allows. The time base moves with the lag only at
  the interval start again (as in 0.2.4): on a computer slower than real
  time the records arrive late, by up to an interval's processing time,
  and the host flags them late.

#### Tests

- The 0.2.5 test (records reach the host soon after their time) is
  replaced by "simulator: slower than real time, a record's time is when
  its position was taken": at ten times real time the cursor jumps
  between two points after every second record, and every record's time
  lies within the cursor's stay at its own position. It fails with the
  0.2.5 simulator.

### [0.2.5] - 2026-11-06

A patch release (tag v0.2.5, RAD-98): the simulator's records keep the
host's time when the computer simulates slower than real time. No API
or format change: radio API 1.0, record files 1, scene JSON 1.1 and
scenario files 1 as in 0.2.0.

#### Radio API

- **Radio API 1.0**: no change.

#### Fixed

- Simulator plugin 0.2.3 (RAD-61, RAD-27): a computer slower than real
  time (sanitizer builds, a loaded machine, a high playback speed) moved
  the simulation's time base only when a record interval started, so
  every record carried a time from the start of its interval and reached
  the host the whole interval's processing time later. Yantra flagged
  such records late and stale, and once an interval took about 5 s to
  simulate, its measurement cycles lost the serving carrier between two
  records (its 10 s drop limit counts from the record time) although
  the stream still delivered. The lag now moves the time base before
  every block; the interval being simulated moves with it, so its
  records keep their position and ground truth (RAD-19). In real time
  nothing changes.

#### Tests

- New: "simulator: slower than real time, the records keep the host's
  time and their position" runs ten times real time at 2 Msps: a record
  reaches the host at most a quarter of an interval's processing time
  (plus 0.2 s) after its time, in time order and with its sim_position.
  It fails before the fix (delays of the whole interval).

### [0.2.4] - 2026-11-05

A patch release (tag v0.2.4, RAD-98): the TETRA terminal plugin no
longer takes the late reply of a stopped session as the answer to the
next session's first command, and the SDR tests that failed only under
load have their cause fixed. No API or format change: radio API 1.0,
record files 1, scene JSON 1.1 and scenario files 1 as in 0.2.0.

#### Radio API

- **Radio API 1.0**: no change.

#### Fixed

- TETRA terminal plugin pei 0.1.1 (RAD-80, RAD-46, TRM-01): a stream
  stopped while a command was out left the terminal's reply on the line.
  When it came after the next session had flushed the port and sent its
  first command, it was taken as that command's answer: a self-test
  reported "The terminal answers AT commands" as passed for a terminal
  that did not answer. A session stopped with a command out now notes
  when that reply is due, and the next session (stream or self-test)
  reads it away before its first command.

#### Tests

- The soapysdr and hackrf cases stream from the fake SoapySDR module
  unpaced, as fast as the CPU allows; their limit was wall-clock time
  and a loaded machine (a full parallel ASan run) stopped them before
  their records came. They now stop after a number of seconds of signal,
  read from the record times, with a stall guard for a stream that
  hangs (`tests/stream_guard.hpp`; RAD-90, RAD-92).
- yantra-hackrf "throughput per sample rate", subcase "an overflow
  fails": the fake now injects its overflow at the second read instead of
  after 0.1 s of signal, which the unpaced stream under ASan did not
  always reach within the command's wall-clock duration under load
  (RAD-66, RAD-24).
- New: the stream guard stops after its signal and reports a stall; a
  terminal's late reply to a stopped stream does not pass the next
  self-test's at_reply item. The latter fails before the fix.

### [0.2.3] - 2026-11-05

A patch release (tag v0.2.3, RAD-98): an own plan's cell on a carrier
the simulator's default scenario already uses is simulated from the
plan's scene. No API or format change: radio API 1.0, record files 1,
scene JSON 1.1 and scenario files 1 as in 0.2.0.

#### Radio API

- **Radio API 1.0**: no change.

#### Fixed

- Simulator plugin 0.2.2 and yantra-gen (RAD-61, RAD-73): a cell of the
  host's scene on a carrier of the default scenario (390.0 or 390.1 MHz)
  was transmitted next to the scenario's cell on the same carrier; the
  scenario's fixed-level cell (D0, -60 dBm) won the decoder and the
  records carried its truth (cell_id D0, walls 0) instead of the plan's
  multi-wall level. `gen::add_scene_cells()` checked ids only. A scene
  cell on the carrier of a scenario cell the scene does not name now
  replaces that cell: it keeps the old cell's place and neighbour cell
  identifier, the other cells announce it instead, and the old cell's
  events and scenario-floor antennas go. Carriers 6.25 or 12.5 kHz apart
  stay different carriers (RADIO_PLUGINS.md).

#### Tests

- New: add_scene_cells replaces a scenario cell on the same carrier
  (neighbours, cell number, events, floor antennas; a cell 6.25 kHz away
  is kept); the simulator with an own plan whose cell is on D0's carrier
  gives that cell's identity, walls 0 and 1 and the multi-wall level
  within 0.5 dB in front of and behind a wall. Both fail before the fix.

### [0.2.2] - 2026-11-05

A patch release (tag v0.2.2, RAD-98): the SDR plugins return from
stop_stream within the RAD-15 limit also on a slow or loaded machine,
and tests that failed only under load have their causes fixed. No API
or format change: radio API 1.0, record files 1, scene JSON 1.1 and
scenario files 1 as in 0.2.0.

#### Radio API

- **Radio API 1.0**: no change.

#### Fixed

- SDR plugins soapysdr 0.1.2 and hackrf 0.1.2 (shared SoapySDR layer;
  RAD-15, RAD-33): the stream thread handed a whole read (up to the
  module's MTU, 131072 samples) to the receiver in one call, and
  stop_stream waits for that thread; on a loaded machine that took
  seconds. The engine now takes a read in pieces of 16384 samples and
  the thread stops between two pieces.
- Fake SoapySDR module (tests, RAD-92): an unpaced read generated its
  whole request whatever its timeout (up to 15 s under ASan with
  parallel tests), so a stop_stream waiting for it missed the RAD-15
  limit (Yantra's unplug/replug test: 21 of 24 parallel ASan runs). A
  read now returns at its timeout with the samples generated so far, as
  a real module does.

#### Tests

- The HackRF records case checks the content of the serving cell's
  neighbour list instead of waiting more than 10 s of signal for its
  repeat (it reached its 600 s hang guard under ASan and load).
- New: the fake module's reads with timeout 0 return one piece each and
  together the same samples as one long read.
- perf_engine is RUN_SERIAL like the other time limits; CONTRIBUTING.md
  says why timing tests need an otherwise idle machine.

### [0.2.1] - 2026-11-04

A patch release (tag v0.2.1, RAD-98): the simulator measures every cell
of its scenario, and a given serving carrier gets one decoder. No API or
format change: radio API 1.0, record files 1, scene JSON 1.1 and
scenario files 1 as in 0.2.0.

#### Radio API

- **Radio API 1.0**: no change.

#### Fixed

- Simulator plugin 0.2.1 (RAD-61, RAD-102, RAD-112): the carrier search
  held only the middle 80 % of the capture, less half a channel at each
  edge, while the plugin offered plans the middle 80 %; cells near the
  edge of a scenario's capture were neither found nor decoded when pinned
  (failed_site gave records of N2 only, at the capture centre, also with
  N1 asked for as the serving carrier). The search now uses 90 % of the
  rate (the generated I/Q has no analogue anti-alias roll-off; as
  `decode-iq`), and `plan_ranges` and the plan check offer exactly the
  span the search holds (`usable_half_width()` in carrier_search.hpp).
- TETRA receiver, all TETRA plugins (simulator, soapysdr 0.1.1, hackrf
  0.1.1; RAD-32): a SERVING plan with a given carrier pinned that carrier
  twice (as a plan carrier and as the serving carrier), so two decoders
  followed it and each interval gave a second record of it with role
  "other". A frequency pinned twice now gets one decoder.
- `yantra-radio-host` 0.2.1 (same receiver; `decode-iq` was not affected:
  it pins a given serving carrier once).

#### Tests

- Every bundled scenario with two or more decodable cells (all but
  weak_signal, whose second cell is below the noise by design): each cell
  in the capture measured within 0.5 dB of the ground truth, the
  strongest serving; failed_site with N1 or N2 given as serving; one
  decoder per carrier in the search and with the fake SoapySDR module.

### [0.2.0] - 2026-11-04

Scene format 1.1 for Yantra's building model (tag v0.2.0, RAD-98). A new
minor release because a format changes (scene 1.1); the radio API stays
1.0, so plugins and hosts of 0.1.0 load unchanged.

#### Radio API

- **Radio API 1.0**: no change. `include/yantra/radio.h` only gains
  documentation of scene 1.1 next to `YR_SCENE_FORMAT`
  (`YR_SCENE_VERSION` stays 1, the major version).

#### Formats

- Scene JSON `yantra-radio-scene` **1.1** (compatible, RAD-36,
  docs/SCENE_FORMAT.md): `minor_version`; floor `storeys` (level and slab
  sum per other floor); wall `polyline` (segments of one wall polyline);
  wall `bands` (attenuation per frequency). 1.0 scenes are read as before
  and give the same levels; 1.0 readers read 1.1 scenes and ignore the new
  members. Readers read any minor of version 1 and refuse version 2. Not
  in 1.1: "EIRP unknown" and outdoor donor directions (the scenario's
  antenna EIRPs and outdoor site cover them, RAD-61, RAD-74).

#### Changed

- Multi-wall model (RAD-73, docs/CHANNEL_MODEL.md): per-band wall
  attenuation at the cell's carrier; per-floor levels and slabs from the
  scene's storeys.
- Simulator plugin 0.2.0 and `yantra-radio-host` 0.2.0 read scene 1.1
  (conformance kit run on the simulator with a 1.1 scene).

#### Fixed

- Multi-wall model: a path exactly through a vertex of a wall polyline at
  an angle counted the wall twice; with polyline ids (scene 1.1) it counts
  once per crossing point, as Yantra's building model does. Scenes without
  polyline ids keep the 1.0 rule, so all golden results are unchanged.
- Tests: the fake SoapySDR control files are written atomically (a rare
  overflow-count race under `ctest -j`).

### [0.1.0] - 2026-11-04

The first version, tag v0.1.0 (RAD-98). Linux x86-64 `.so` plugins
`hackrf`, `soapysdr`, `simulator` and `pei`, the tools `yantra-radio-host`,
`yantra-gen` and `yantra-hackrf`, the licence bundle and the complete
corresponding source. Not yet in this release: the Windows `.dll` build
(written, not run), radios measured on real hardware (DEVICES.md), real
recorded signal fixtures (RAD-121).

#### Radio API

- **Radio API 1.0** (new): the first version, defined in
  `include/yantra/radio.h` (RAD-10..49):
  - entry symbol `yantra_radio_entry` and the descriptor with id, version,
    licence, capabilities, technology id, record schemas and translations
    (RAD-12, RAD-13, RAD-48);
  - versioned structs with `struct_size` (RAD-10); status codes and
    `last_error` texts (RAD-16);
  - the host callbacks: log, clocks, record sink `push_record`,
    `stream_event`, `devices_changed`, `settings_changed`, and for SCENE
    plugins the context callbacks `get_floor_plan`, `get_cursor`,
    `get_walk_position` and `get_scene` with `on_context_event`
    (RAD-14, RAD-18, RAD-19);
  - record schemas of kind MEASUREMENT, SCAN and NEIGHBOUR with eight core
    fields at fixed indexes and optional field groups (RAD-20..27);
  - the common stream control: enumerate, open, close, device info, state,
    plan ranges, plans, start, stop, stream statistics (RAD-30..35);
  - device control described as data: settings, actions, presets,
    sensors, self-test (RAD-40..49).

#### Formats

- Record files `yantra-radio-records` version 1 (RAD-65,
  docs/RECORD_FILES.md).
- Scene JSON `yantra-radio-scene` version 1 (RAD-36,
  docs/SCENE_FORMAT.md).
  Compatible optional members for the multi-wall model (RAD-73): floor
  `slab_attenuation_db` and `storey_height_m`, wall class
  `reflection_loss_db`, antenna `floor_offset`.
- Scenario files `yantra-radios-scenario` version 1 (RAD-74,
  docs/SCENARIO_FORMAT.md), in data/scenarios.

#### Added

- Radio test host `yantra-radio-host` with emulated host callbacks and the
  conformance kit (RAD-91); every plugin gets a conformance test
  (`yantra_add_radio_plugin`).
- Example radio plugin `beacon` (RAD-64) and the plugin tutorial
  docs/RADIO_PLUGINS.md.
- TETRA test-signal generator library `yantra::gen` (gen/, RAD-70), first
  part: downlink channel coding (EN 300 392-2 clause 8), the broadcast
  PDUs (SYNC, SYSINFO, ACCESS-ASSIGN, MAC-RESOURCE, D-NWRK-BROADCAST with
  neighbours) and the slot schedule of a cell's main carrier, with golden
  vectors and osmo-tetra cross-checks (RAD-93). Test binaries take
  `--update` to rewrite golden files on purpose.
- Generator second part (RAD-70): synchronization and normal continuous
  downlink bursts (clause 9), pi/4-DQPSK modulator with root-raised-cosine
  shaping at any sample rate, `CellTransmitter` (one cell's continuous
  carrier with level, frequency offset and timing offset) and
  `MultiCarrierTransmitter` (several carriers in one span), with burst and
  IQ golden vectors and an osmo-tetra burst-sync round trip (RAD-93).
- Generator third part: the channel model (RAD-72: path loss, correlated
  shadowing, Rayleigh/Rician fading, TU50/HT200, Doppler, CFO and clock
  errors, AWGN from a noise figure, IQ imbalance, DC, clipping,
  quantisation, sample drops), the indoor multi-wall model on the scene
  (RAD-73, docs/CHANNEL_MODEL.md), scenario files with the DEMO-09
  scenarios and one per bundled example plan, and the scenario engine
  with ground truth per time step (RAD-74); statistics, formula and
  seeded golden-digest tests (RAD-93).
- `yantra-gen` (RAD-71): scenario file, example id or command-line cells
  to cf32/cs16/cs8/cu8 samples with SigMF metadata (`yantra:simulated`,
  capture segments at sample drops, annotations per cell and event), a
  ground-truth CSV, a cell CSV and a ground-truth record file (RAD-65,
  schemas `tetra_cell` and `tetra_neighbour`); impairment options of
  RAD-72; streaming to stdout; deterministic for a seed
  (docs/YANTRA_GEN.md). Install rules for yantra-gen and data/scenarios.
  Tests: CLI and outputs against the scenario engine, SigMF validity,
  seeded golden digests, and a round trip of its IQ files through
  osmo-tetra's receive side (RAD-93).
- Receiver library `yantra::receiver` (receiver/, RAD-100): FFT on
  pocketfft, window functions and spectrum, Kaiser and root-raised-cosine
  filter design, NCO, polyphase resampler, channelizer (25 kHz channels
  from a wideband capture), pi/4-DQPSK demodulator with carrier-offset and
  symbol-timing recovery, soft bits, MER and lock; SIMD kernels with a
  plain C++ fallback (`YANTRA_SIMD=OFF`); the TETRA parameter set, the
  training sequences and a bit-error-tolerant burst synchroniser
  (DEC-01). Tests on generator IQ (exact bits, BER, offset and clock
  ranges) and a benchmark (docs/RECEIVER_DSP.md).
- Static dependency builds (RAD-132, RAD-136): talloc 2.4.3 and the
  libosmocore 1.11.0 subset osmo-tetra needs, vendored unchanged from
  their release archives (`tools/deps/manifest.tsv` with versions and
  SHA-256, `tools/deps/verify_sources.py`) and built with CMake
  (`tools/deps/deps.cmake`, or `tools/deps/build_deps.sh <prefix>` and
  `-DYANTRA_DEPS_PREFIX`); osmo-tetra's lower layers as the static
  `yantra::osmo_tetra` (`cmake/OsmoTetra.cmake`, never `crypto/`); the
  dependency manifest compiled in (`yantra::receiver::bundled_libraries()`).
  The osmo-tetra cross-check tests no longer need the system libosmocore.
- TETRA decoder `yantra::tetra_decoder` (receiver/, RAD-101): lower MAC
  on osmo-tetra (burst split, descrambling, de-interleaving, Viterbi, CRC,
  RM(30,14) AACH decoding with up to 3 corrections), MAC-SYNC/D-MLE-SYNC,
  MAC-SYSINFO/D-MLE-SYSINFO, ACCESS-ASSIGN and MAC PDU classification as
  typed structs (DEC-02, DEC-05), the serving-cell parameter set (DEC-03),
  encryption state and encrypted block counts (DEC-10), the H-M-F-S time
  base from SYNC with UTC stamps (DEC-11), and the per-carrier
  `CarrierDecoder` (IQ to slots, cell events and per-period counters).
  Instances share no state. Tests with osmo-tetra's test PDUs, generator
  signals with noise and offsets, parallel carriers and a bundled scenario
  (RAD-123, docs/TETRA_DECODER.md).
- Rust protocol crate `yantra-proto` (receiver/rust, RAD-101, RAD-03):
  MAC PDUs, LLC, D-MLE-SYSINFO, D-MLE-SYNC, D-NWRK-BROADCAST with every
  neighbour element and MM broadcast PDUs, behind a fixed-width C ABI
  (FFI width and layout lint, RAD-90; cargo-deny in the lint); golden
  digests run by ctest (RAD-120). The decoder uses it for fragment
  reassembly (DEC-04), neighbour lists and re-select parameters (DEC-02,
  DEC-03) and channel assignment counts per carrier (DEC-06).
- Decoder quality windows (RAD-113): message erasure rate from the AACH,
  CRC error rates per logical channel, BER of CRC-good blocks, MER/EVM,
  M2M4 SNR and frequency error per `CarrierDecoder` report.
- TETRA carrier search `yantra::tetra_engine` (RAD-102, RAD-110, RAD-111):
  25 kHz raster with the four carrier offsets in the TETRA bands, energy
  candidates, one decoder per carrier, loss and re-acquisition, pinned
  carriers, retune, and a parallel executor hook.
- Shared TETRA record schemas `tetra_cell`, `tetra_neighbour` and
  `tetra_scan` (`yantra/radios/tetra_schemas.hpp`, docs/TETRA_RECORDS.md)
  with `SchemaView` (a SchemaDef as a `yr_schema`). yantra-gen's ground
  truth uses them; its schema ids changed from `tetra.cell` and
  `tetra.neighbour` (not valid plugin schema ids) and it adds
  `freq_error_ppm`, `c1_db` and the dBm values of the SYSINFO codes.
- TETRA measurement engine `MeasurementEngine` (yantra::tetra_engine,
  RAD-102, RAD-110..114): wideband IQ of a capture to `tetra_cell`,
  `tetra_neighbour` and `tetra_scan` records and SYNC_FOUND/SYNC_LOST,
  SERVING_CHANGED and SCAN_DONE events; per carrier the 25 kHz channel
  level (dBFS, or dBm with a calibration offset), the quality window, C1
  and its inputs, broadcast parameters, frame number and allocation
  counts; serving choice, neighbour roles from the serving cell's list,
  record time from the caller's time base, overload and non-finite
  samples, an optional worker pool (docs/MEASUREMENT.md).
- `yantra-radio-host decode-iq` (RAD-63): cf32, cs16, cs8 and cu8 raw
  files and SigMF recordings (capture segments as discontinuities,
  yantra-gen's calibration, clipping as OVERLOAD) through the measurement
  engine into a record file; `--plugin` is no longer needed for it.
  `capture-iq` stays a hook (needs the plugins' device layer). Tests
  compare decoded records of yantra-gen scenarios with the ground truth.
- `perf_engine` ctest (RAD-114, labels slow/perf/timing): four carriers
  in a 1 MS/s capture through the whole engine must take less than half
  of real time on one thread.
- GSMTAP output (RAD-115, DEC-12): `EngineConfig::gsmtap` and
  `decode-iq --gsmtap ADDR [--gsmtap-port PORT]` send the decoded
  broadcast blocks (BSCH, BNCH, AACH, broadcast D-NWRK-BROADCAST, scope
  RAD-17) as GSMTAP over UDP for Wireshark; off by default.
- Fuzz targets (RAD-96, docs/FUZZING.md) for TETRA PDUs, soft-bit bursts
  through the lower MAC and GSMTAP filter, IQ file content through the
  measurement engine, SigMF metadata, scenario, scene and record files:
  replayed with seeded mutations under ctest (label fuzz, seeds from the
  generator), libFuzzer builds with the `fuzz` preset and
  `tools/ci/fuzz.sh`; cargo-fuzz targets `tetra_pdu` and `tm_sdu` for the
  Rust crate (receiver/rust/fuzz).
- Benchmarks with CPU-calibrated limits (RAD-127, docs/BENCHMARKS.md):
  channelizer, demodulator, decoder and level meter; ctest bench_limits
  (labels slow, perf) fails on a regression beyond tests/bench/limits.csv.
- Real-signal fixture framework (RAD-121, BLOCKED: no recordings yet):
  tests/fixtures/real/<radio>/NAME.{sigmf-data,sigmf-meta,expected.json},
  checked by the ctest real_fixtures (yantra-check-fixtures through
  decode-iq); real_fixtures_selftest checks it on a yantra-gen recording.
- Real-world robustness checklist (RAD-124, docs/ROBUSTNESS.md) with the
  end-to-end tests of tests/robustness (radio, air, transmitter and
  program impairments through the measurement engine) and the level
  linearity tests (RAD-125: +/-0.5 dB from -5 to -100 dBFS, 1 Hz records
  without gaps over long runs).
- Level meter (RAD-112): RRC filter matched to the TETRA pulse on the
  carrier centred by the demodulator's estimate, instead of a 14 kHz
  Kaiser low-pass that let a carrier 25 kHz away through at -28 dB (a
  neighbour 40 dB stronger read 11.6 dB high). Noise alone now reads in
  a 19.7 kHz noise bandwidth.
- Sensitivity sweep `yantra-sens` (RAD-126, docs/SENSITIVITY.md): decode
  rates, CRC error rate, BER, lock time and MER against SNR for awgn,
  cfo2/20/50, offcentre, clock50, tu50, ht200, phasenoise and combined;
  quick tier per scenario under the ctest label `sens` (sens-quick
  workflow), full tier `sens_full` (labels slow, release), baselines in
  tests/sens/baseline.
- Simulator radio plugin `simulator` (RAD-61), shown as "Demo (simulated
  network)", with SIMULATED and SCENE: the generator's scenario engine
  makes IQ inside the plugin and the same measurement engine decodes it.
  The scenario comes from the example id of the plan shown (RAD-18) or a
  setting; levels follow the multi-wall model (RAD-73) at the cursor,
  walk or a fixed position, records carry `sim_position`; scene changes
  reload; without plan or position the scenario levels are used. Device
  settings, presets, sensors, self-test, practice calibration (a
  generator at a known level, true offset as a sensor), MS maximum power
  for C1 (default 30 dBm), real-time pacing with playback speed, and
  OVERFLOW/DROPPED when the sink is slow (RAD-24, RAD-25). The bundled
  scenarios are embedded in the plugin. Tests through the test host's
  emulated context callbacks (RAD-94).
- Shared TETRA plugin glue `yantra::tetra_plugin` (plugins/tetra_common):
  schema table, plan checks, record delivery counters, C1 with a known
  calibration, setting helpers and the GSMTAP settings, for the
  simulator and the SoapySDR plugins.
- `EngineConfig::playback_speed`: record times run this many times faster
  than the signal (the simulator's playback speed).
- `gen::parse_scenario(text, reader)`: scene files from a reader (the
  simulator's embedded scenarios).
- Tests (RAD-90, Yantra TST-01): the long doctest binaries run one ctest
  per test case (`yantra_doctest_tests()`, cmake/YantraTests.cmake; COST
  from tests/ctest_costs.txt), so `ctest -j` finishes in about a minute
  instead of three and a half. The long robustness suite and the full
  sensitivity tier are opt-in (label opt_in, `YANTRA_TESTS_OPT_IN=1`);
  the real-time checks carry the label timing, which the sanitizer
  presets and the ci-full sanitizer jobs leave out; their hang guards
  scale with YANTRA_TEST_TIME_SCALE (5).
- SDR dependencies (RAD-132, RAD-136): SoapySDR 0.8.1 with SoapyHackRF
  0.3.6/libhackrf 2026.01.3, SoapyRTLSDR 0.3.4/librtlsdr 2.0.3 and
  SoapyAirspy 0.2.1/libairspy 1.0.10 on libusb 1.0.30, vendored from
  their release archives and built statically into the plugins; the
  module loader is replaced so nothing is loaded from the system.
- Shared SoapySDR layer `yantra::soapy_plugin` (RAD-54) and the generic
  plugin `soapysdr` (RAD-62): enumeration with stable keys, settings and
  sensors generated from the module (RAD-51), external reference
  (RAD-53), diagnostics (RAD-52), overflow and drop events, hot-plug
  (RAD-35). A fake SoapySDR module for the tests (RAD-92); the conformance
  kit runs against it, unplug and replug included.
- `yantra-radio-host capture-iq` (RAD-63): SDR capture to SigMF in four
  formats with a rolling buffer and capture segments at drops.
- HackRF plugin `hackrf` (RAD-60) on the SoapySDR layer's device
  profiles: 2-20 Msps, LNA/VGA gains and the RF amplifier switch, antenna
  port power (off at every open and replug, never in a preset), offset
  tuning, presets, sensors and self-test items, band scans wider than
  the span (RAD-23) and C1 from a calibration offset (RAD-22). All
  SoapySDR plugins gain clipping (OVERLOAD), saturation warning, noise
  floor, receiver load, tuned frequency and build info sensors.
- `yantra-hackrf` (RAD-66): HackRF One test tool through the plugin
  (list, info, selftest, settings, measure, capture, noise, level, ppm,
  throughput, replug, report), text or JSON, exit codes for scripts,
  docs/YANTRA_HACKRF.md.
- Install rules (RAD-99): plugins, tools, udev rules and the RTL-SDR
  DVB-T blacklist (udev/), the licence bundle (RAD-137) and the docs,
  tested by an install into a DESTDIR.
- TETRA terminal plugin `pei` (RAD-80, TRM-01, TRM-04), in plain C11: a
  terminal in TMO read over its PEI (AT commands, EN 300 392-5) on a
  serial port (POSIX termios; Windows DEFERRED): serving and neighbour
  cell records in dBm with C1 from the terminal's cell lists (vendor
  commands set as settings, the documented example format by default),
  AT+CSQ and AT+CREG, the radio info (AT+GMI/GMM/GMR/GSN) as sensors, the
  AT command log, hot-plug of the port (RAD-35) and only reading commands
  (RAD-17). Tested against `yantra-pei-sim`, a simulated terminal on a
  pseudo-terminal answering from a generator scenario (errors, timeouts,
  garbage, unplug/replug), with the conformance kit and the fuzz target
  `pei` for the AT parser (RAD-96).
- SoapyRemote 0.5.3 (RAD-50, network radios) built into the SDR plugins:
  the generic plugin lists the devices of the SoapySDRServers named in
  `YANTRA_SOAPY_REMOTE` (host[:port], comma-separated) as
  `remote:<serial>`; the module's multicast discovery never runs in a
  plugin. Tested end to end with SoapyRemote's server side on the
  loopback serving the fake module (records match the generator's
  truth) and with the conformance kit. SoapyBladeRF is not built: libbladeRF
  needs the ADI no-OS AD9361 driver, whose licence is not GPL-3.0
  compatible (THIRD_PARTY.md).
- Documentation (RAD-97, RAD-135): ARCHITECTURE.md (layers, data flow,
  threads), BUILD.md (Linux, MSYS2 UCRT64, static dependency builds,
  completeness checks, release), DEVICES.md (RAD-95: the test devices
  measured, the real radios open), CONTRIBUTORS.md, docs/IQ-FORMATS.md,
  one page per plugin in docs/plugins/ (devices, settings, presets,
  sensors, self-test, known issues), the record schemas with the source
  of every field (message and EN 300 392-2 clause, or how it is measured)
  incl. the terminal schema, and the robustness checklist moved to
  REAL_WORLD_CHECKLIST.md. Docs checks: `doc_links` (relative links and
  anchors) and `plugin_docs` (the plugin pages' generated parts and the
  schema tables against the built plugins).
- Licence bundle (RAD-137) made from tools/deps/manifest.tsv by
  `tools/ci/licence_bundle.py` (checked against third_party/, LICENSES/
  and THIRD_PARTY.md, ctest `licence_bundle`), with the copyright notices
  of the libraries whose licence is only in their source headers, the Rust
  standard library and the index BUNDLE.md; installed to
  share/licenses/yantra-radios and in every release package.
- Release (RAD-98, RAD-134, RAD-138): `ninja release`
  (`tools/ci/make_release.sh`) writes build/release/v<version>/ with the
  plugin package (the install tree), the corresponding source with the
  upstream release archives of the bundled libraries and their SHA-256,
  the release notes and SHA256SUMS; the packaged plugins pass the
  completeness check. release.yml does the same per platform on a tag.
