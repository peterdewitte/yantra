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

### Radio plugins

- 2026-11-11: pinned to yantra-radios **v0.3.0** (commit 6fac3ee): radio
  API 1.1, Windows x64 plugins, the terminal plugin on Windows serial
  ports, the PDF plugin manual. Yantra loads radio API 1.1 plugins.
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

### Reports

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
