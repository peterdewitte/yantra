<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Changelog

What changed in Yantra per release, for users (DOC-16), with real dates.
Changes of the plugin API are in
[docs/PLUGIN_API_CHANGELOG.md](docs/PLUGIN_API_CHANGELOG.md). The radio
plugins come from yantra-radios (git submodule `third_party/yantra-radios`,
PLG-26); every move of its pinned release is recorded here (RAD-98), its
own changes are in its CHANGELOG.md.

## [Unreleased]

A simpler indoor workflow (specification 0.4): the project window has a
Walk and a Map tab, the coverage map is interpolated from the measured
points only, and the demo uses fixed scenes. The version stays 0.2.0
until the next release. Not run for this state: the sanitizer trees and
the Windows tests (the Windows tree is cross-built and packaged).

### Added

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

### Documentation

- The user manual, the quick starts (English, Dutch, French, German), the
  README and the developer and file-format documents describe the Walk
  and Map workflow, the Window menu, the Map tab's Advanced dropdown,
  File > Report, the IDW coverage map, the fixed demo scenes, the opening
  of older projects and the green demo explanations (DOC-02, DOC-02a).
  The manual's chapter 1 names the Windows packages, the WinUSB driver
  and the Windows folders; the PDF manual's index has the new terms.
- Screenshots, the indoor-workflow GIF and WebM show the new project
  window (SCR-03, SCR-06).

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
