<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# YANTRA - REQUIREMENTS SPECIFICATION

- **Version:** 0.6 (draft)
- **Date:** 2026-11-20
- **Status:** Derived from an AI-generated functional analysis; not yet reviewed

Changes in 0.6 (owner, 2026-11-20): one repository. yantra-radios is merged into the Yantra repository (PLG-29, replaces PLG-26 and the submodule): both histories kept, a plugin-first tree that shows the plugin architecture (radio-api/, plugins/, radio-common/, core/, app/), one product version (next 0.3.0) while the radio API keeps its own ABI version (1.2). The radio boundary is unchanged and now enforced inside the one build (PLG-30). [requirements_radios.md](requirements_radios.md) 0.4 follows.

Changes in 0.5 (owner, 2026-11-17 evening, after testing 0.4.1): new DEMO-16 (Fraunces Tavern has a weak inner area, so its demo report is a FAIL), UI-22 (popup when a report is ready, with Open), UI-23 (one "Plan and properties" button opens the import window), IND-47 ("About building" first tab: address with an OpenStreetMap map, building photo opened or taken with a camera, surveyor, survey date, notes), IND-48 (notes on the plan by long click: picture, text, audio), UI-24 (Tools > Spectrum: TETRA band spectrum and waterfall from radios with the SPECTRUM capability, radio API 1.2), TST-64 (packages workflow: clang for Linux and Windows, .deb and setup.exe). 16.2: camera capture with Yantra's own code over the operating system's interfaces (V4L2 on Linux, Media Foundation on Windows), no new library; OpenStreetMap tiles and Nominatim geocoding through libcurl.

Changes in 0.4.1 (owner, 2026-11-17, after testing 0.4): new UI-20 (plan import window layout: Info button, (?) help on trim options, "Add as floor" popup in the trim section, floors list above the preview, Scale and Alignment collapsed) and UI-21 (Walk tab: radio in use with "Select...", recording collapsed, walk test kind as radio buttons with (?) help).

Changes in 0.4 (owner requests of 2026-11-17, after the 0.2.0 build):

- Simpler indoor workflow: no walls, no antennas and no ray tracing in Yantra; the only measuring mode is the walk test (with the mouse survey in demo mode), and the coverage map is an inverse-distance-weighted (IDW) interpolation between the measured points (IND-41, IND-43, IND-45). Removed IDs, not to be reused: IND-11, IND-12, IND-40, IND-42, IND-44, TST-36 (replaced by TST-53). Their code, file-format parts, tests and docs are removed from Yantra; old project files open and drop their walls and antennas with a note (IND-46).
- Project window tabs: only Walk and Map (no Layers, Edit, Survey or Post tab). The live survey values and the recording controls are in the Walk tab; the other survey panels open from the Window menu (UI-18). The Map tab shows the layer switches and holds the post-processing settings in an "Advanced" dropdown (UI-19). The report is made from File > Report (UI-14).
- The simulator gets its signal levels from fixed demo scenes shipped with the demo data (DEMO-05, DEMO-12, DEMO-13, PLG-27); Yantra no longer builds scenes from a building model. The radio API is unchanged and specified in api_definition.md.
- Guided tour: shows where the MAK plans were downloaded (DEMO-11); every demo explanation (tour steps, demo hints) has a green background (DEMO-15).

Changes in 0.3 (owner decisions of 2026-11-10, after the 0.1.0 build):

- Compiler and platforms: clang from one pinned LLVM release is the default compiler on Linux and Windows (gcc stays as a second Linux tree); Windows x64 is cross-compiled on Linux with the cross SDK of yantra-radios (RAD-139) and Yantra's own lock file of MSYS2 UCRT64 packages; the Windows tests run on Windows, the native MSYS2 build stays as a second check (HW-06, OA-02, NFR-09, TST-02, DOC-11, 16.2). Windows is no longer deferred.
- The simulator reports levels in dBm by default (RAD-61); the mouse survey no longer applies an automatic practice calibration (DEMO-14).
- Project files store plan-source and recording paths relative to the project file when possible (IND-01).
- The Radio menu always lists every loaded radio plugin (PLG-16).
- Plugin API 1.2: neighbour entries carry their own LA/MCC/MNC (PLG-05).
- The submodule URL is the absolute yantra-radio-plugins URL (PLG-26; no submodule since 0.6, PLG-29).
- Report header "Share of measured points (%)" (IND-43).
- The manual stays English only for 0.1.x; translations get a native-speaker review before a public release (DOC-06).
- New GUI test: no text overflows its control or window, in every language, layout, theme and size (TST-52; TST-50's layout check is now this automatic test).
- PyYAML is allowed as a CI-only tool (16.2, TST-02).
- The PDF user manual is a deliverable of its own: layout, contents, shipping in every package, Help > User manual (PDF) and checks (new DOC-02a; DOC-20, TST-52).
- Removed IDs, not to be reused: DB-06 (alias database, out of scope, 16.5), TST-13 and TST-14 (with RAD-121 and RAD-122), and in yantra-radios TRM-02 and TRM-03.

Changes in 0.2 (owner requests of 2026-11-01):

- Indoor coverage is the main purpose (section 0, 1.1).
- Indoor projects with plan import, trimming, several floors per source and several sources per project (new section 21, IND-xx).
- Report points equal project-window points (IND-30); ray-tracing interpolation (IND-40..45); curtat.be reference and logo (BRD-xx).
- Menus: File > New indoor project, About, Help > Reset all settings (UI-14..17).
- Guided tour with three example buildings from the MAK collection (DEMO-11..13).
- Radios are plugins (HackRF, Demo, generic SoapySDR; section 20, PLG-02, PLG-16..19); terminal mode and the probe/remote API too (PLG-23/24).
- Lessons learned (HANDOFF.md): build from scratch (section 0); radio plugins run live in the app (PLG-16); HackRF tested against a fake SoapyHackRF (PLG-17); exporter plugins in batch export (PLG-02); committed sample indoor report (DEMO-06); Inter embedded in PDF reports (EXP-05); a real source URL before release (NFR-09); X11 preferred under WSL2 (NFR-10); ffmpeg and oxipng allowed as dev-only tools, WeasyPrint listed (16.2).
- Yantra's own logo (BRD-05..07, owner request of 2026-11-03).
- Indoor only (owner request of 2026-11-03): no outdoor drive tests, no GPS and no geographic maps (1.2). Removed IDs, not to be reused: HW-04, UI-06, UI-07, GEO-01, GEO-03, GEO-04, EXP-02, OA-15, PLG-20, PLG-21, PLG-22, DEMO-04. gpsd, PROJ, miniz and shapelib are no longer used (16.2).
- Radio project split (owner request of 2026-11-03): every input device (HackRF, simulator, generic SoapySDR, IQ file player, TETRA terminal) is a plugin of the separate project yantra-radios, specified in [requirements_radios.md](requirements_radios.md) (RAD-xx). Yantra loads them through the radio API and builds their windows itself (PLG-16, PLG-25..27). Moved out: HW-01a, HW-01c, HW-05, PLG-17, PLG-18, PLG-19, PLG-23, section 9 (TRM-01..04), TST-42, DOC-15 and the generator code of TST-10.
- Radios deliver measurements only (owner request of 2026-11-03): a radio plugin decodes its own IQ samples and gives Yantra measurement records (level, carrier, MCC/MNC, LA, colour code; optionally quality, C1, broadcast parameters and neighbours). No IQ samples and no PDUs reach Yantra. Moved to yantra-radios: HW-02, HW-03, section 4 (DEC-01..12), the measurement parts of section 3, OA-11..14, the decoder tests (TST-11, TST-13..15, TST-20..22, TST-30, TST-40) and osmo-tetra. Dropped from Yantra: UI-05 (PDU list), REC-04 (raw IQ capture), IQ file playback and the spectrum/waterfall debug view.

## 0. PURPOSE AND SOURCES

Name: Yantra - "Yet Another Network Testing and Radio Analyser". The name is used for the application, the executable (yantra / yantra.exe), the repository, the installer, the documentation and the screenshots. The generator CLI (in yantra-radios) is yantra-gen. Settings and project files use the "yantra" prefix (e.g. \*.yantra project files, %APPDATA%/Yantra, \~/.config/yantra).

Folders: requirements and code are kept apart.

- Requirements project: a separate requirements project. It holds this specification, the research behind it and the AI-assistant files (.claude, memory). All work on the requirements is done here.
- Code project: the Yantra repository, a git repository started with "git init". It holds the source code, tests, documentation, build files and the demo/ reference data that ship with Yantra.
- Radio parts (since 0.6 in the same repository, PLG-29): the radio API, all radio plugins (input devices) with the SDR signal processing and TETRA decoder, and the TETRA test-signal generator, specified in [requirements_radios.md](requirements_radios.md) (RAD-xx). The dependency goes one way only: the application may use radio-api/ and load plugins at run time; it never uses plugin or radio-common code (PLG-30). The former yantra-radios repository is kept unchanged as a read-only archive.
- Yantra is built fully from scratch from this specification, in an empty Yantra repository. There is no reference code base: all code is written anew for this specification.
- No requirements file and no .claude folder or other AI-assistant settings are stored in the code project or committed to its repository. Its .gitignore excludes .claude/ and CLAUDE.md as a safeguard. Code, commits and documentation in the code project refer to requirement IDs (e.g. "HW-01", "PLG-03") instead of copying requirement text.

Yantra is an open-source TETRA coverage survey and air-interface monitoring tool. Its main purpose is INDOOR coverage: measuring and reporting TETRA coverage inside buildings (e.g. for DAS and indoor-coverage acceptance), on floor plans, floor by floor. It is an indoor-only tool: there are no outdoor drive tests, no GPS and no geographic maps (1.2). It is built on a software-defined radio (SDR).

Sources:

- [FUN] AI-generated functional analysis of a TETRA air-interface monitoring, recording and coverage survey tool: the functions such a tool offers.

- [OA] OnAir (GPL-3.0): an open-source multi-mode SDR receiver. Yantra uses the same technology stack, third-party libraries and public protocol standards, but none of OnAir's own libraries or code. See section 16.

- [CUR] curtat.be (Curtat BV): Yantra is made by curtat.be as a reference for building new programs with Claude, Anthropic's AI assistant. The About dialog and the reports carry this reference (BRD-01..04).

- [MAK] MAK - Museum of Applied Arts, Vienna, online collection: three floor-plan sheets by Josef Frank (and Oskar Wlach) used as example buildings in the guided tour (DEMO-11..13). Licence CC BY-NC-ND 4.0 ("Werknutzungsrecht"), so they are NOT stored in the repository or packages and NOT adapted; they are downloaded on first use:
  - Beer house, Wenzgasse 12, Vienna 13 (1929-30), inv. KI 23126-36: https://sammlung.mak.at/en/collect/copy-of-the-floor-plans-for-the-individual-floors-and-for-the-cross-section-of-the-beer-house-at-wenzgasse-12-viennas-13th-district-by-josef-frank-and-oskar-wlach_332162
  - Haus Claeson, Falsterbo (1927), inv. KI 23497-359: https://sammlung.mak.at/en/collect/kopie-des-originalplans-mit-querschnitten-und-grundrissen-der-einzelnen-etagen-fuer-das-haus-claeson-in-falsterbo_430870
  - Villa Hugo Bunzl, Chimanistrasse 18, Vienna 19 (1936), inv. KI 23126-41: https://sammlung.mak.at/en/collect/copy-of-the-original-floor-plans-with-legend-for-the-individual-floors-as-well-as-of-the-cross-section-with-pencil-sketches-and-markings-for-the-villa-for-hugo-bunzl-at-chimanistrasse-18-vienna_332434

Each requirement carries a source tag: [FUN], [OA], [CUR] or [NEW]. [NEW] marks requirements that follow from the SDR approach, from common survey practice, or fill a gap in the functional analysis.

Product: Yantra is a desktop application for Windows and Linux, licensed GPL-3.0. It uses the same third-party components and protocols as OnAir, and is implemented independently of OnAir's code (section 16).

Priorities: M = must (MVP), S = should, C = could (later).

Key architectural difference:

- Terminal-based survey tools get their measurements from a TETRA terminal (a radio in a test mode). Yantra instead uses a passive SDR receiver that decodes the downlink. As a result:
  - \+ it can measure every carrier in the band, not only the three "foreground" cells a radio reports;
  - \+ it needs no subscription, ISSI or radio codeplug;
  - \- it cannot see uplink PDUs, the radio's own call state, the radio's own C1/RDC decisions, or perform calls.

  Features that need a terminal are an optional radio plugin of yantra-radios ([requirements_radios.md](requirements_radios.md) section 9, TRM-01 and TRM-04).

## 1. SCOPE

### 1.1 In scope

- Downlink reception and decoding of TETRA V+D (TMO), ETSI EN 300 392-2, done inside the radio plugins of yantra-radios ([requirements_radios.md](requirements_radios.md)).
- Indoor coverage projects (main purpose): buildings with several floors, floor plans from PDF or image files, walk tests, interpolation between the measured points and acceptance reports (section 21).
- Cell/network parameter display, neighbour-list analysis and verification.
- Recording, replay, export, floor-plan visualisation and reporting.

### 1.2 Out of scope

- Transmitting on any TETRA frequency.
- Decryption of air-interface encryption (TEA1-4) or end-to-end encryption. Yantra only indicates the encryption state.
- Voice decoding/playback of third-party traffic (see 11.3).
- Certification-grade RF conformance testing.
- Outdoor drive tests, GPS or any other positioning receiver, geographic coordinates, map tiles and geographic map views. Every measured point has a plan position on a floor (section 21), never a geographic one.

## 2. HARDWARE AND PLATFORM

**HW-01** M [NEW] — Every input device is a radio plugin of yantra-radios ([requirements_radios.md](requirements_radios.md)): HackRF, the simulator, generic SoapySDR and the TETRA terminal. Each plugin processes its own IQ samples (demodulation, decoding, level and quality measurement) and gives Yantra measurement records only (RAD-20..26). Yantra loads them through the radio API (PLG-25) and builds their windows itself (PLG-16). Yantra has no radio-, SDR-, SoapySDR-, vendor- or IQ-processing code and does not link SoapySDR or osmo-tetra.

**HW-01b** M [NEW] — Calibration (RF-02) is keyed to driver + serial + the settings the plugin marks CALIBRATION (RAD-40: antenna, gains, amplifier), because absolute levels are not comparable between radios and modules. Without a calibration entry, the levels of a radio that reports dBFS are shown as dBFS and labelled "uncalibrated"; a radio or device that reports dBm (RAD-21) needs no entry.

**HW-02, HW-03** (moved to yantra-radios: RAD-110, RAD-111.)

**HW-06** M [OA] — Desktop application for Windows 10/11 64-bit and Linux x86-64, using the toolchain of OA-02: CMake, C++20 and Rust, built with clang for both platforms; the Windows build is cross-compiled on Linux and uses MSYS2 UCRT64 packages. S: Linux arm64. C: Raspberry Pi 4/5-class computer for an unattended fixed probe. macOS is not a target, but is not deliberately broken either.

**HW-09** M [NEW] — Antenna correction: a configurable antenna gain, cable loss or attenuator value (dB) is applied to every level reading and stored with the data.

## 3. RF MEASUREMENT AND CALIBRATION

The radio plugins measure ([requirements_radios.md](requirements_radios.md) sections 3 and 12); Yantra shows, calibrates, records and analyses the measurement records they deliver (RAD-20..26). Levels arrive in dBFS unless the radio is calibrated itself (SELF_CALIBRATED) or the device reports dBm (e.g. the simulator, RAD-21, RAD-61); calibration (RF-02) applies to dBFS levels only.

**RF-01** M [FUN] — Shows and records the level of the serving/selected carrier, delivered by the radio at least once per second (RAD-21, RAD-112).

**RF-02** M [NEW] — Level calibration: a per-device, per-frequency offset table, made with a signal generator or a reference radio, that converts the radio's dBFS levels to dBm. Yantra applies it, with the antenna correction (HW-09), to every level record. The calibration date and device serial are stored with each recording.

**RF-03** M [NEW] — Shows and records the level of every neighbour cell announced by the serving cell, when the radio delivers it (RAD-22, RAD-112).

**RF-04** S [NEW] — Scanner view: the per-channel levels of a band scan delivered by the radio (RAD-23), with the channels where the radio found a TETRA carrier and their MCC/MNC/LA/colour code marked.

**RF-05** M [FUN] — Shows and records the downlink quality metrics the radio delivers (RAD-22, RAD-113): MER, slot CRC error rate, BER estimate.

**RF-06** S [NEW] — Estimated C1 path-loss parameter per cell: taken from the record when the radio delivers it, otherwise calculated by Yantra from the calibrated level and the cell's broadcast MIN_RX_ACCESS_LEVEL / MS_TXPWR_MAX_CELL (ETSI EN 300 392-2 clause 23), with a configurable MS class power (default 30 dBm portable / 35 dBm mobile).

**RF-07** C [NEW] — Shows modulation quality (EVM/MER in dB, frequency error) when the radio delivers it (RAD-22, RAD-113).

**RF-08** M [NEW] — Sample flags: each measurement carries the validity flags of its record (no sync, overload/clipping, uncalibrated, simulated; RAD-21), which every view, recording, export and report keeps.

## 4. PROTOCOL DECODING (MOVED)

Decoding is done inside the radio plugins ([requirements_radios.md](requirements_radios.md) section 13: DEC-01..12 keep their IDs there). Yantra receives only the record fields of RAD-21 and RAD-22 (cell identity, levels, quality, C1, serving-cell broadcast parameters with the security class, neighbour lists, frame numbers); no PDUs, slots or IQ samples. The serving-cell panel (UI-02) shows the DEC-03 parameters and the encryption state per cell (DEC-10) from these records.

## 5. LIVE DISPLAY (UI)

**UI-01** M [FUN] — Main survey view with large values: RSSI (dBm), MER/BER, LA, MCC/MNC, channel/frequency, cell name (from database), current floor and sample counter.

**UI-02** M [FUN] — Serving Cell panel (DEC-03).

**UI-03** M [FUN] — Neighbour/Adjacent Cells panel: name, LA, channel, frequency, measured RSSI, estimated C1, broadcast parameters. Measured-but-unlisted cells and listed-but-not-received cells are shown in different colours.

**UI-04** M [FUN] — RSSI-versus-time graph (and C1-versus-time graph) of the serving cell plus up to 31 neighbours, with per-cell colours from the cell database. Optional threshold lines (FRT, SRT, min RX access) and an MER trace. A cursor on the graph synchronises all other panels to that time.

**UI-05** (dropped: no PDUs reach Yantra, RAD-26.)

**UI-08** S [FUN] — Status bar: free disk space, battery/power state, radio device state, recording state, records dropped (warning, RAD-24).

**UI-09** S [FUN] — Audible events through text-to-speech or tones: RSSI/C value spoken at an interval, loss of sync, cell reselection, low battery.

**UI-10** S [FUN] — RSSI alert: up to N cells with thresholds, which raise a popup or sound when a cell drops below its threshold.

**UI-11** S [FUN] — Statistics panel: number of serving-cell changes, sync losses, time in/out of coverage, % samples above threshold.

**UI-12** S [FUN] — UI language files (at least EN, NL, FR, DE).

**UI-13** M [OA] — Native desktop UI written for Yantra with the same libraries as OnAir: Dear ImGui, GLFW and OpenGL 3, plus Yantra's own plotting on ImGui draw lists. It has a touch layout for 7-12" Windows/Linux tablets (>= 48 px targets), dark, light and high-contrast themes for dark rooms and bright sites, and a layout that adapts to small windows and display scaling.

**UI-14** M [NEW] — Menu bar. File: New indoor project (IND-01), Open project, Save, Save as, Recent projects, Add floor (IND-10), Close project, Export, Report, About (UI-15), Exit. Enabled plugins add their own entries (e.g. radio windows from PLG-16, the remote-probes view from PLG-24). Help: Tour (DEMO-11), User manual, User manual (PDF) (DOC-02a), Quick starts, Reset all settings (UI-16), About (UI-15).

**UI-15** M [CUR] — About dialog, opened from File > About and Help > About: name, version, build date, licence (GPL-3.0) with the AGPL source link (NFR-01), the Yantra logo (BRD-06), the curtat.be logo and a link to https://curtat.be, and the statement that Yantra is made by curtat.be as a reference on building new programs with Claude (Anthropic's AI assistant). Also the third-party credits (THIRD_PARTY.md), the data credits (Library of Congress, MAK) and the loaded plugins with their versions and licences (PLG-09).

**UI-16** M [NEW] — Help > Reset all settings: a confirmation dialog lists what is reset (UI layout and theme, language, device and radio-plugin settings, thresholds, palettes, tooltip settings, enabled plugins, recent-file list, tour progress) and what is kept (projects, recordings, cell databases). Calibration tables are kept unless the user ticks "also reset calibration". The current settings are first saved as a time-stamped backup file, and the dialog says where. After the reset the app restarts its UI with defaults, as on first start. A command-line option (--reset-settings) does the same without the UI.

**UI-17** M [NEW] — The app opens in indoor mode: the start screen offers New indoor project, Open project, Recent projects and the Tour (DEMO-11). There is no outdoor mode (1.2).

**UI-18** M [NEW] — Project window tabs: only "Walk" and "Map" (no Layers, Edit, Survey or Post tab). The Walk tab is the only measuring mode: the walk test (GEO-02) and, with the simulator, the mouse survey (DEMO-14), with the live values of UI-01 (serving cell, level, quality, sample count), the recording controls (REC-01..03) and a compact level-versus-time graph of the serving cell (UI-04). The neighbour panel (UI-03), the scanner, the full graphs (UI-04), events (UI-09/10) and statistics (UI-11) open as separate windows from a Window menu.

**UI-20** M [NEW] — Plan import window layout (owner, 2026-11-17, 0.4.1): the information about the source image (file, page, size, resolution, and for a licence-restricted example plan its title, credit, licence, source link and notes, IND-03) is hidden behind an "Info" button next to the file name (owner, 2026-11-17 21:30); every trim option has a "(?)" help mark with a short explanation; the "Add as floor" button sits in the trim section and opens a popup for the floor's name and level height (IND-06); the "Floors in this plan" list is shown above the preview; the "Scale" and "Alignment" sections (IND-08, IND-09) are collapsed by default (an arrow opens them).

**UI-22** M [NEW] — When a report (EXP-05) is ready, a popup says so with "Open" (opens it in the system viewer), "Show in folder" and "Close" (owner, 2026-11-17, 0.5).

**UI-23** M [NEW] — The project window has one "Plan and properties" button that opens the plan import window (IND-05..09, UI-20) with the floor's properties (name, level, height); the separate "Properties" and "Plan and scale" buttons are removed (owner, 2026-11-17, 0.5).

**UI-24** M [NEW] — Tools > Spectrum (owner, 2026-11-17, 0.5): for a radio with the SPECTRUM capability (radios RAD-140: the simulator, HackRF and other SDR plugins; never real terminals) a window shows the TETRA band spectrum (power per frequency, dBFS or dBm as the radio delivers it) and a waterfall, updated once a second, with the serving and neighbour carriers marked. Default band 380-400 MHz, settable within the radio's plan ranges. Hidden when the radio has no SPECTRUM capability.

**UI-21** M [NEW] — Walk tab layout (owner, 2026-11-17, 0.4.1): it names the radio in use with a "Select..." button that opens Radio > Radios...; the recording controls (REC-01..03) are collapsed by default (an arrow opens them); the walk test kind is chosen with radio buttons ("bullets"), each with a "(?)" help mark, in the same style as the plan import window's trim options.

**UI-19** M [NEW] — The Map tab shows the interpolated map (IND-41, IND-43) with a "Show" row to switch the plan, the measured points and the map on and off (map with opacity), a button to (re)compute the map (IND-45), and an "Advanced" dropdown, closed by default, with the post-processing settings: the views (PP-02), the base-station fail simulator (PP-03), the level offset (PP-04), binning (PP-05), the interpolation settings (grid, IDW power, radius) and the acceptance profile (EXP-06). The report is made from File > Report (UI-14); there is no Report tab.

## 6. RECORDING AND REPLAY

**REC-01** M [FUN] — Modes: Monitoring (no file), Record, Quick-record (filename from date/time, one key).

**REC-02** M [FUN] — Recording file split by time (1-60 min) or by message count, with automatic index numbers (name_0000, name_0001, ...). A recording session can be continued.

**REC-03** M [FUN] — Recording file contents: header (tool version, SDR model/serial, calibration, antenna correction, cell DB in use, operator notes), measurement records with their plan positions, and markers. Open, documented format (e.g. JSON-lines or SQLite), optionally compressed.

**REC-04** (dropped: no IQ samples reach Yantra. IQ capture is a tool of yantra-radios, RAD-91.)

**REC-05** M [FUN] — Markers: numbered user markers; "new file" and "new file + pause" buttons, e.g. when moving to another floor or area. Markers are exported.

**REC-06** S [FUN] — Voice notes: a short microphone recording linked to a timestamp, played back at that point during replay.

**REC-07** M [FUN] — Replay: play, pause, stop, play-again, play a sequence of files in order, real-time or fast. Seek by slider, graph or a click on the floor plan. During replay all panels show the state at the selected instant.

**REC-08** S [FUN] — Cut and save part of a file (start to cursor / cursor to end).

**REC-09** S [FUN] — Loop "monitoring temp file" (ring buffer).

**REC-10** M [NEW] — Crash-safe writing: no more than 1 s of data lost on power loss.

**REC-11** S [FUN] — Prevents an accidental application exit while recording.

## 7. INDOOR WALK TESTS

**GEO-02** M [FUN] — Walk test on a floor of an indoor project (section 21) using "point and click": click the start point, walk, click the end point. The samples are spread evenly along the segment. Also offers "continuous" mode and "delete last segment".

**GEO-05** M [NEW] — Manual test mode: measure at chosen points, e.g. rooms, and average N seconds per point.

**GEO-06** M [NEW] — Multi-floor projects: a building has floors, each floor has a plan and measurement sets. Detailed in section 21 (IND-xx).

## 8. CELL DATABASE AND NETWORK ANALYSIS

**DB-01** M [FUN] — Site/cell database. Each cell has: LA, main carrier, colour code, site name, sector/azimuth, graph colour, reselection parameters (FRT, FRH, SRT, SRH, min RX, max TX, access parameter), subscriber class, neighbour list (up to 31 entries as LA/channel), notes. Cells can be identified by LA, by channel, or by both.

**DB-02** M [FUN] — Import/export of the database as CSV, with LA in hex or decimal. A database editor is built in.

**DB-03** S [FUN] — Auto-learn: fills or extends the database from received broadcasts.

**DB-04** M [FUN] — BS parameter verification: compares received broadcast parameters and neighbour lists with the database, colour-codes differences (match / new / missing), and writes a difference log.

**DB-05** M [NEW] — Cell cross-check: finds neighbour relations that are not mutual (BS1 lists BS2, but BS2 does not list BS1) across all cells seen in a survey or project.

**DB-06** (removed in 0.3: subscriber addresses are out of scope, 16.5; not to be reused.)

**DB-07** S [FUN] — Tools: TETRA channel \<-> frequency calculator (band, offset, duplex, reverse operation), subscriber-class bitmask calculator.

**DB-08** S [FUN] — Cell reselection simulator: given serving and neighbour parameters and simulated RSSI levels, shows the serving-cell state (exclusive / superior / usable / improvable / relinquishable) and which reselection would occur.

## 9. TERMINAL MODE (MOVED)

Terminal mode (TRM-01, TRM-04) is a radio plugin of yantra-radios ([requirements_radios.md](requirements_radios.md) section 9, RAD-80). Its measurements reach Yantra as records (RAD-21) and are shown, recorded and exported like every other measurement.

## 10. EXPORT AND REPORTING

**EXP-01** M [FUN] — CSV export with selectable columns and separator: time (UTC/local), floor, x, y (plan position in metres), LA, channel, RSSI, C1, MER/BER, serving-cell-change flag, up to N neighbours (LA, channel, RSSI, C1), marker, antenna correction. Invalid values are written as -999.

**EXP-03** S [FUN] — DXF export per floor for CAD: measured points in plan coordinates (metres).

**EXP-04** M [FUN] — Batch export of all recordings in a folder tree, merged or separate.

**EXP-05** M [NEW] — Coverage report (PDF/HTML): floor plan with measured points and legend, % of samples above a configurable threshold (good/bad analysis), histogram/CDF of RSSI, per-cell statistics, serving-cell changes, measurement metadata (equipment, calibration, antenna, date, operator). For an indoor project: one section per floor with the floor plan, every measured point exactly as in the project window (IND-30), the interpolated coverage map (IND-41, IND-43) and the floor statistics, plus a building summary. Carries the curtat.be reference and logo (BRD-02). PDF reports embed the Inter font (SIL OFL allows embedding, subset only), so every character of names and notes prints correctly; JetBrains Mono for numeric tables.

**EXP-06** S [NEW] — Configurable acceptance profiles, e.g. "X% of area/samples >= Y dBm" for an operator's indoor-coverage acceptance criteria (such as ASTRID/DAS acceptance). Thresholds and percentages are user-defined.

**EXP-07** S [NEW] — Data sharing: send the export through e-mail/SMTP (own server), or through a shared folder/HTTP upload when the survey finishes. Sending over the internet is optional.

**EXP-08** S [FUN] — Save a floor-plan image with measured points and legend (PNG/JPG).

## 11. POST-PROCESSING FUNCTIONS

**PP-01** M [NEW] — Desktop/web viewer that loads one or more recordings and shows each data point. Hovering a point shows its plan position, strongest cell, serving cell and all neighbour cells.

**PP-02** M [NEW] — Views: RSSI (dBm), MER/BER (%), good/bad against a threshold, serving-cell changes, coverage rate (%) of the tested area.

**PP-03** S [NEW] — Base-station fail simulator: switch off one or more sites and recompute "best server" and remaining coverage from the measured neighbour levels.

**PP-04** S [NEW] — Level offset simulation: apply +/- dB (e.g. building penetration or body loss) and recompute coverage.

**PP-05** S [NEW] — Binning: aggregate samples into a grid on the floor (e.g. 1 m / 2 m squares) and take the median/percentile per bin.

### 11.3 Legal note

Monitoring third-party radio traffic, and disclosing what was received, is regulated in many countries (in Belgium, e.g. by the Electronic Communications Act). Yantra targets operators, integrators and site surveyors who are authorised to measure the network. The default build therefore records broadcast/control information and signal levels, but no traffic content.

## 12. AUTOMATION AND INTEGRATION

**API-01** S [FUN] — Command-line control: start/stop record, monitoring, play, sound on/off, quit.

**API-02** S [FUN] — Probe plugin (PLG-24). Network remote control: a remote probe streams a filtered live view (sync, sysinfo, neighbour broadcast, levels) to a central viewer and records in full locally. Uses a REST/WebSocket API with authentication.

**API-03** C [NEW] — Probe plugin (PLG-24). Unattended fixed-probe mode: a long-term logger that rotates files and sends periodic health and coverage summaries.

**API-04** M [NEW] — Plugin interface for other radio technologies, radio sources and exporters. See section 20.

## 13. NON-FUNCTIONAL REQUIREMENTS

**NFR-01** M — Licence: GPL-3.0. SPDX headers in every file, a REUSE.toml, and a THIRD_PARTY file with the notices of the bundled libraries. No dongle or licence key. The TETRA radio plugins of yantra-radios link osmo-tetra (AGPL-3.0) and run inside Yantra; GPL-3.0 section 13 allows this combination. Its parts keep their AGPL terms, so the app offers the complete corresponding source to anyone who uses it over a network (remote API, API-02): a source link in About and in the API status reply. osmo-tetra's crypto/ directory is not built or used. Fonts under the SIL OFL 1.1 (Inter, JetBrains Mono) are shipped as separate font files next to the executable and loaded at run time. They are never compiled into the binary (no embedded C arrays). Data is licensed apart from code and listed in THIRD_PARTY with its source and licence (floor plans). Data whose licence does not allow redistribution or adaptation (e.g. the MAK plans, CC BY-NC-ND 4.0) is never put in the repository, packages or screenshots; it is downloaded by the user's app on first use (DEMO-12). The curtat.be logo is a trademark of Curtat BV, not under GPL-3.0; it has its own REUSE entry (BRD-04). Yantra's own logo is GPL-3.0-or-later (BRD-07).

**NFR-02** M — Performance: handles the records of the shipped radio plugins at their full rate together with a 1 Hz measurement cycle on a Raspberry Pi 4 or a 5-year-old laptop, with no data loss during a continuous walk test. Real-time decoding is required of the plugins (RAD-114).

**NFR-03** M — Accuracy: after calibration, absolute level accuracy is within +/-2 dB for -110...-40 dBm with a mid-range SDR (measured with the radio plugins, RAD-125). RTL-SDR results are labelled "indicative".

**NFR-04** M — Robustness: survives SDR unplug/replug without restart, and reconnects automatically.

**NFR-05** M — Open formats: every stored file format is documented in the repository.

**NFR-06** S — Configuration: profiles saved to and loaded from files (devices, DB, thresholds, palettes, UI layout).

**NFR-07** S — Self-interference: documentation and a UI warning about laptop/USB RF noise; the antenna should be kept away from the PC.

**NFR-08** M — Testability: tests with the simulator radio plugin and with recorded measurement files and their expected output. Detailed in section 17 (TST-xx); decoder tests are in yantra-radios (section 14 there).

**NFR-09** M — Packaging, with Yantra's own scripts: a Windows portable zip and NSIS setup .exe with all DLLs bundled (WinUSB through Zadig for HackRF/RTL-SDR/Airspy), and a Linux .deb with udev rules and the RTL-SDR DVB driver blacklist. S: AppImage. C: Docker image for the probe (API-03). The Windows zip and setup .exe are made on Linux from the cross build (OA-02, makensis); their contents pass the DLL import check (only Windows system DLLs, the UCRT and the shipped DLLs) and the packaged app's tests run on Windows (TST-02). The AGPL source link (NFR-01) and the package's source field come from one build setting, YANTRA_SOURCE_URL, default https://github.com/peterdewitte/yantra (repository https://github.com/peterdewitte/yantra.git). Package homepage https://curtat.be; maintainer "Curtat BV \<info@curtat.be>". The release check fails if YANTRA_SOURCE_URL is empty. Nothing is pushed to the repository by the build.

**NFR-10** M — Window system: on Linux the app starts under Wayland or X11; under WSL2 it prefers X11 by itself (GLFW's Wayland/libdecor path can hang before the window appears), with a setting and command-line option to override. A launch test checks that the first frame appears.

## 14. SUGGESTED BUILDING BLOCKS

Third-party libraries: see section 16.2 (the same set OnAir uses). Others:

- yantra-radios: the radio plugins, decoder and generator (16.2, PLG-26), with osmo-tetra inside.

Excluded:

- No proprietary survey software, components, file formats, databases, drivers or dongles may be used, read, imported or interfaced with. The [FUN] tags refer to the functional analysis only; Yantra is implemented independently.

## 15. MVP CUT (PHASE 1)

Indoor first. Core and indoor: HW-01, HW-01b, HW-09, RF-01..03, RF-05, RF-08, UI-01..04, UI-13..17, REC-01..03, REC-05, REC-07, REC-10, GEO-02, GEO-05, GEO-06, IND-01..45 (except S/C items), BRD-01..07, DB-01, DB-02, DB-04, DB-05, EXP-01, EXP-04, EXP-05, EXP-06, PP-01, PP-02, NFR-01..05, NFR-08, NFR-10. Radios: the yantra-radios MVP ([requirements_radios.md](requirements_radios.md)), loaded through PLG-16 and PLG-25..27.

## 16. IMPLEMENTATION PLATFORM: SAME STACK AS ONAIR, OWN CODE

Yantra is a separate GPL-3.0 application for Windows and Linux. It uses the same third-party components and public protocol standards as OnAir, but it does NOT take OnAir's own libraries or code: not dect2core, not libonair_rs or any onair_\* Rust crate, and no OnAir source files, copied or modified. All survey and UI code is written for Yantra; the receiver and decoder code is written for yantra-radios.

### 16.1 Code structure

**OA-01** M — Own repository, with no build or run-time dependency on OnAir. The layout separates a receiver/survey library with no UI (core/), the desktop application (app/), command-line tools (tools/) and tests (tests/), so the library can also be used headless (API-03). Plugins live in plugins/: the TETRA technology plugin (PLG-01), the example technology plugin (PLG-15) and the feature plugins (e.g. the probe, PLG-24). The public plugin header is include/yantra/plugin.h. Radio plugins and their header (yantra/radio.h) come from the yantra-radios submodule in third_party/yantra-radios (PLG-26).

**OA-02** M — Toolchain: C++20 and CMake >= 3.20 with Ninja. Rust (cargo) may be used for file-format layers, with a C ABI to the C++ code.

- Compiler: clang from one pinned LLVM release, the same for Linux and Windows, from the cross SDK of yantra-radios (RAD-139) that Yantra uses through its submodule. clang is the default; gcc is kept as a second Linux build tree. Every tree builds without warnings.
- C++ standard library: libstdc++ on both platforms (Linux: the system's; Windows: the MSYS2 UCRT64 GCC runtime).
- Windows x64 (UCRT) is cross-compiled on Linux. Yantra's own lock file pins the MSYS2 UCRT64 binary packages of its libraries (16.2) by SHA-256; the app ships their DLLs (NFR-09). PDFium uses its pinned Windows release. A native MSYS2 UCRT64 build (gcc) keeps working as a second check.
- Nothing is installed in the build system: the SDK and the packages are downloaded, checked and unpacked into a git-ignored folder.

CI on Linux and Windows runs ctest on every change (TST-02).

**OA-03** M — Threading model: one measurement thread per radio that never waits for the UI; the UI reads published telemetry snapshots; disk writes go through a bounded queue on a separate writer thread. Every thread is named.

**OA-04** M — No OnAir code may be copied into Yantra, also not under the shared GPL-3.0 licence. OnAir may only be used to cross-check that Yantra's measurement output agrees (as a test oracle). It is not a source of code.

### 16.2 Third-party libraries (the same ones OnAir uses)

```text
Library               Licence          Used for
--------------------  ---------------  -------------------------------------
Dear ImGui (+ stb)    MIT              UI (UI-01..13), vendored
GLFW                  zlib             window and input
OpenGL 3              -                rendering
Inter, JetBrains Mono SIL OFL 1.1      UI fonts (text, numeric readouts)
miniaudio             PD / MIT-0       audible alerts, voice notes (UI-09,
                                       REC-06)
SQLite                Public domain    cell database (DB-01), project and
                                       survey store
stb_image(_write)     MIT / PD         floor plans (every format stb_image
                                       reads, IND-02) and report images
OpenSSL (optional)    Apache-2.0       HTTPS/wss for the remote API
Added for Yantra (not in OnAir):
yantra-radios         GPL-3.0-or-later radio API header, radio plugins with
                                       the SDR processing and TETRA decoder,
                                       TETRA test-signal generator
                                       (submodule, PLG-26). SoapySDR, its
                                       modules, osmo-tetra, libosmocore and
                                       pocketfft are built into the plugins
                                       (RAD-04, RAD-132)
nlohmann/json         MIT              remote API, JSON-lines recordings
                                       (REC-03), simulator scenes (RAD-36)
zstd                  BSD-3            optional recording compression (REC-03)
CivetWeb              MIT              HTTP + WebSocket server for the remote
                                       API (API-02)
libcurl               curl (MIT-style) HTTP client with TLS: upload
                                       (EXP-07) and the MAK download
                                       (DEMO-12)
libharu               zlib/libpng      PDF coverage and acceptance reports
                                       (EXP-05)
PDFium                BSD-3/Apache-2.0 floor plans from PDF files (IND-02):
                                       page list, thumbnails, rendering at a
                                       chosen resolution; vendored pinned
                                       release
doctest               MIT              C++ unit tests under ctest (TST-01)
```

DXF (EXP-03) is written by Yantra's own code; translations (UI-12) use Yantra's own string tables. Not used (never linked): FFmpeg libraries, codec2, whisper, Lua, RNNoise and other OnAir dependencies that have no role in Yantra. The ffmpeg program is allowed as a dev-only tool (below).

Build, test and packaging tools (never linked into Yantra, so they do not affect its licence): Doxygen (DOC-17), pandoc (manual to HTML/PDF, DOC-02/20), libFuzzer and cargo-fuzz (TST-12), cargo-deny (licence check of Rust crates, NFR-01), reuse (REUSE compliance, NFR-01), Mesa llvmpipe and Xvfb (CI rendering and off-screen screenshots, TST-04, SCR-01), NSIS, dpkg, linuxdeploy / appimagetool (packaging, NFR-09), the cross SDK (pinned LLVM clang/lld, MSYS2 UCRT64 packages; yantra-radios RAD-139), PyYAML (CI only: the YAML lint, TST-02), WeasyPrint (pandoc's PDF engine, DOC-20), ffmpeg (program only: GIF/WebM captures, SCR-06), oxipng (lossless PNG size reduction of docs/img and docs/samples, SCR-03). Every library and tool is checked against its own LICENSE file when it is added to THIRD_PARTY.md (DOC-14).

### 16.3 Protocols and formats (public standards, implemented from the spec)

- ETSI EN 300 392-2 (TETRA V+D air interface): the meaning of the record fields, C1 and cell reselection (clause 23). The air interface itself is implemented in yantra-radios.
- CSV and DXF.
- Remote API: HTTP + WebSocket, JSON, localhost by default, bearer token for LAN access (API-01/02).

### 16.4 Components to build (new Yantra code)

**OA-10** M — Radio host (PLG-25): loading radio plugins through the radio API, the record sink with drop counting, the common stream control and unplug/replug handling (NFR-04).

**OA-11, OA-12, OA-14** (moved to yantra-radios: DSP RAD-100, decoder RAD-101, quality metrics RAD-113.)

**OA-13** M — Measurement pipeline: applies the calibration table and antenna correction (RF-02, HW-09) to every level record, keeps the 1 Hz measurement cycle per carrier, and feeds the views, recording, walk test and interpolation.

**OA-16** M — Survey modules: indoor project model and plan import (section 21), recording format and replay, floor-plan walk test, point interpolation (IDW, IND-41), coverage statistics, cell database, parameter verification, reports (sections 6-11, 21).

**OA-17** M — Desktop app: ImGui shell, menus (UI-14..17), panels, RSSI/C1 plots, project window with floor tabs, plan import window, touch layout, themes, i18n (EN, NL, FR, DE).

### 16.5 Scope boundary

Yantra handles only what every base station broadcasts in the clear, matching 1.2 and 11.3. The radio plugins decode only that and give Yantra measurement records (RAD-17, RAD-26); short data (SDS), subscriber addresses, call content, voice and PDUs never reach Yantra. Therefore DEC-07 (LIP) is OUT OF SCOPE and DB-06 (alias database) is removed.

### 16.6 MVP additions

OA-01..04, OA-10, OA-13, OA-16, OA-17, NFR-09.

## 17. TEST REQUIREMENTS

These follow OnAir's test practice (CONTRIBUTING.md, REAL_WORLD_CHECKLIST.md, the CI workflows and the sensitivity sweep), applied to Yantra's own code. They are test practices, not reused code (OA-04).

### 17.1 Test framework and CI

**TST-01** M [OA] — Every test runs under ctest. Tests are in tests/\<module>/, one folder per core module, and are picked up by a glob. Tests are labelled fast, slow and timing. A full run needs no radio.

**TST-02** M [OA] — CI on every pull request: Linux x86-64 (clang; gcc as a second job) and Windows x64 (cross-compiled with clang on a Linux runner, OA-02; the tests run on a Windows runner) build the app, tools and tests, then run the fast tests (ctest -LE slow, timeout 600 s). Timing-sensitive tests are retried once. The native MSYS2 UCRT64 build runs as a second Windows check (nightly, TST-03). Locally, the Windows tests run from Linux under WSL2 through Windows interop (CMAKE_CROSSCOMPILING_EMULATOR). A lint job checks shell, YAML (PyYAML) and Python syntax and the widths of any Rust\<->C++ FFI structs.

**TST-64** M [NEW] — Packages workflow (owner, 2026-11-17, 0.5): a GitHub Actions workflow builds everything with clang from the cross SDK for Linux x86-64 and Windows x64 (cross-compiled), runs the fast tests, and makes the .deb and the setup .exe (NFR-09) as workflow artifacts (and release assets on a tag). The workflows are kept valid (YAML lint) although the repositories have no remote yet.

**TST-03** M [OA] — Full CI (nightly and before a release): adds Linux arm64 and a scalar build without SIMD (the plain C++ fallback must give the same results). The slow tests run on schedule.

**TST-04** S [OA] — The app is started in CI under OpenGL 3.1 software rendering (Mesa llvmpipe) and a screenshot is saved as an artifact. This proves the UI runs on Raspberry Pi-class GPUs and on machines without a GPU.

**TST-05** S [OA] — Developer builds with AddressSanitizer + UBSan and with ThreadSanitizer. They run the test suite before each release.

### 17.2 Tests without a radio

**TST-10** M [OA] — Tests without a radio use the simulator radio plugin (RAD-61) through the radio API and recorded measurement files written by the radio test host from the generator's scenarios (RAD-71, RAD-91), so no hardware and no IQ is needed. Decoder tests are in yantra-radios (section 14 there); Yantra never links the generator or the decoder.

**TST-11** (moved to yantra-radios, RAD-120.)

**TST-12** M [OA] — A seeded fuzz/mutation target for every parser (project and recording files, CSV import, scene export, the record sink). It runs in the sanitizer builds and must not crash, hang or allocate without bound.

**TST-13, TST-14** (removed in 0.3 with RAD-121 and RAD-122; not to be reused.)

**TST-15** (moved to yantra-radios, RAD-123.)

### 17.3 Real-world robustness checklist (each item has a test or a documented reason why it cannot happen)

**TST-20, TST-21, TST-22** (moved to yantra-radios, RAD-124.)

**TST-23** M [OA] — Program: radio plugins that push records late, in bursts or at a rate Yantra cannot keep up with (drop cleanly and count, never fall apart); unplug and replug; a plugin that fails or hangs a control call; NaN/Inf and out-of-range values in records; truncated and corrupted recording and project files.

### 17.4 Measurement accuracy

**TST-30** M [NEW] — Calibration: the calibration table and antenna correction are applied exactly (to 0.01 dB) to level records over the frequency range, with interpolation between table points as documented; the calibration wizard (RF-02) against the simulator's simulated signal generator recovers the true offset. Level linearity of the radios is tested in yantra-radios (RAD-125).

**TST-31** M [NEW] — Measurement cadence: >= 1 Hz level samples with timestamps and plan positions, with no gaps in a long replayed walk test (RF-01, NFR-02).

**TST-32** M [NEW] — Survey maths: tests for walk-test sample placement, coverage percentage, binning and percentiles, C1 calculation, the reselection simulator states, and neighbour cross-check (DB-05), with hand-calculated expected values.

**TST-33** M [NEW] — Export/import round trips (CSV, DXF, cell database CSV, project file). Plan positions survive every round trip unchanged.

**TST-34** M [NEW] — Report point identity (IND-30): for generated projects with many floors and dense walks, the report's point list per floor (count, order, plan position, value) equals the project window's list exactly. A test fails on any thinning or re-sampling.

**TST-35** M [NEW] — Indoor project model: crop rectangles and polygons, rotation, several floors from one source, several sources, floor reordering, scale calibration and missing-source handling survive a save/load round trip. Plan import is tested with multi-page PDFs and every supported image format.

**TST-36** (removed in 0.4 with the ray tracer, IND-40; not to be reused.)

**TST-53** M [NEW] — Point interpolation (IND-41): IDW cases against hand-calculated grid values (one, two and many points, coincident points, power and radius limits); the map equals the measured value at every measured point's grid cell; the confidence mask (IND-43) fades with distance as specified; results are the same with and without SIMD and do not depend on thread count.

**TST-37** M [NEW] — Reset all settings (UI-16): defaults restored, backup written, projects, recordings and (unless chosen) calibration untouched.

### 17.5 Sensitivity and performance

**TST-40** (moved to yantra-radios, RAD-126.)

**TST-41** S [OA] — Benchmarks (tests/bench) for the record pipeline and the interpolation, with a CPU calibration line so limits scale with the runner. A regression beyond the limit fails the slow label.

**TST-42** (moved to yantra-radios, RAD-95.)

### 17.6 UI and app

**TST-50** S [OA] — App tests for the non-drawing logic (state, settings, project model, file handling). The layout check of overlapping, cut-off and squashed controls is the automatic GUI test TST-52.

**TST-51** S [OA] — Remote API test: a socket client exercises every endpoint against the real engine fed by the simulator (authentication, LAN token, refused foreign Origin).

**TST-52** M [NEW] — GUI text-overflow test (ctest, label slow; on Linux and Windows): the app is started off-screen (Xvfb + Mesa llvmpipe on Linux, a hidden window on Windows) with the simulator and the demo project, and every window, panel, dialog, menu, popup, tab, tooltip, help marker and generated radio window (PLG-16; the simulator, example and HackRF plugins with the fake module) is drawn in turn by a scripted walk through the UI. It runs for:

- every UI language (EN, NL, FR, DE) and a pseudo-language that makes every string 40 % longer with accented characters, so new or longer translations are caught before they exist;
- the normal and the touch layout (UI-13), light and dark theme;
- the smallest supported window size (1280 x 720) and a large one, at UI scales 100 %, 150 % and 200 %.

For every text item drawn, the test records its text, its bounds and the rectangle of the control, column, table cell and window that holds it, and fails when text:

- runs past its control, table cell, column or window edge, or is clipped by it (also partly, e.g. a cut-off last letter);
- overlaps another text item or control;
- is wrapped or cut where the control does not allow it;
- is shortened, unless the control is marked as shortening with an ellipsis ("...") and shows the full text in a tooltip.

A failure names the window, the item id, the language, layout, theme, size and scale, and the overflow in pixels, and saves a screenshot with the item outlined. The same check runs on the generated reports (EXP-05): no text in a PDF or HTML report (titles, legends, table cells, captions) runs past its box or the page, in every language. Known exceptions are listed in one checked-in file, each with a reason; the list is empty for a release.

### 17.7 Contribution rules

**TST-60** M [OA] — A change to a measurement, calculation or file format must add or extend a test. Output may only change on purpose, and the commit message must say why. Every pull request says how it was tested, on which OS and with which radio.

**TST-61** M [NEW] — The work is split into many small, logical commits, one per coherent step (e.g. radio host, record pipeline, project model, plan import, interpolation, reports, tests, reference project, docs). Each commit builds and passes its tests, and its message says what changed and why.

**TST-62** M [NEW] — Commit dates are the real dates on which the work was done. They are never backdated or rewritten to suggest a longer or different development period. Work imported from elsewhere keeps its original dates and is marked as imported in the commit message.

**TST-63** M [NEW] — AI-assisted commits carry a Co-Authored-By trailer naming the assistant. The CHANGELOG (DOC-16) records the project start and milestones with their real dates.

### 17.8 MVP tests

TST-01, TST-02, TST-10, TST-12, TST-23, TST-30..37, TST-52, TST-60..63.

## 18. DOCUMENTATION AND SCREENSHOTS

Documentation is part of the product and is delivered with every release. A feature is not done until it is documented and its screenshots are current.

### 18.1 User documentation

**DOC-01** M [NEW] — README.md: what Yantra is, supported radios, install (Windows, Linux), quick start, screenshots of the main screens, licence, credits.

**DOC-02** M [FUN] — User manual (docs/manual/, Markdown, also built to PDF and shipped with the app and opened from Help): installation and drivers (Zadig/WinUSB, udev rules), first start, radio setup, calibration, live survey, indoor projects (plan import and trimming, floors, walk test, interpolation, reports), recording and replay, cell database and parameter verification, exports and reports, plugins (each shipped radio plugin with its generated settings, debug, calibration and device-info windows; the probe plugin), settings and reset, keyboard shortcuts, FAQ and troubleshooting. One chapter per section 2-12 and 21 of this specification.

**DOC-02a** M [NEW] — PDF user manual: the manual of DOC-02 is also one PDF file, yantra-manual-\<version>.pdf (A4, English for 0.1.x, DOC-06), made from the same Markdown by the docs build (pandoc and WeasyPrint, 16.2) and never edited by hand. It has:

- a title page with the Yantra logo (BRD-05), the version, the build date and the licence; a table of contents with page numbers; numbered chapters and sections that match the "See manual 7.2" references of the help texts (DOC-07);
- PDF bookmarks (outline) for every chapter and section, working internal links (contents, cross-references) and external links, and document metadata (title, author "Curtat BV", subject, language);
- the screenshots of docs/img (SCR-03) at print resolution, each with a numbered caption; tables that break across pages with their header repeated; page headers with the chapter title and footers with the page number and version;
- every font embedded (Inter, EXP-05), so it prints and reads the same everywhere; text that can be selected and searched (no text as images), and a PDF that is tagged for accessibility (PDF/UA where the engine supports it);
- an index of the main terms and of the UI windows and menus.

It ships in every package (the .deb under /usr/share/doc/yantra/, the Windows zip and setup .exe next to the program, NFR-09) and as its own release artefact. Help > User manual (PDF) opens the shipped PDF with the system's PDF viewer, also offline; Help > User manual opens the HTML version. Checks (DOC-20): the PDF builds without warnings, every bookmark and internal link resolves, every image is present, every font is embedded, the page count and contents match the Markdown chapters, and no text runs past the page margin or a table cell (TST-52).

**DOC-03** M [NEW] — Quick-start guides (one page each): "first indoor project (import a plan, trim floors, walk, report)", "indoor walk test with acceptance report", "calibrate a radio".

**DOC-04** M [NEW] — Measurement method note: what is measured and how (RSSI, C1, MER, sampling cadence, binning, coverage percentage, calibration and its uncertainty per radio type), so reports can be defended to a network operator.

**DOC-05** M [NEW] — Legal and scope note: receive-only, no decryption, what is and is not decoded (16.5), the user's responsibility under local law, and the AGPL source offer (NFR-01).

**DOC-06** S [FUN] — Translations of the quick starts: EN (source), NL, FR, DE, kept in step with the UI languages (UI-12). The manual stays English only for 0.1.x; its translations come later. Before a public release a native speaker reviews the UI texts and the translated quick starts of each language.

**DOC-07** M [OA] — In-app help, built into the UI as in OnAir:

- a) "(?)" help markers: a dimmed "(?)" after a control, setting or section heading whose meaning is not obvious. Hovering it (or a long press on touch screens, UI-13) shows a short explanation in plain language: what it does, when to use it, units and value range, the default, and what changes in the measurement. The text names the manual chapter for more (e.g. "See manual 7.2").
- b) Tooltips on every control: every button, toggle, menu item, field, column header and measured value has a hover text. Icon-only buttons always have one. Shortcut keys are shown in the tooltip as "[key]".
- c) Disabled controls stay hoverable and say why they are disabled and what to do (e.g. "Start the radio first", "Set the floor scale first").
- d) Measured values and decoded fields (RSSI, C1, MER, colour code, LA, reselection parameters, ...) explain themselves on hover: name, unit, how the radio measures it or which broadcast message it comes from (from the record schema, RAD-21), and the EN 300 392-2 clause where relevant.
- e) Each window has a help button that opens its chapter of the user manual (DOC-02), bundled with the app, so help works offline.
- f) Help texts are translated like all UI strings (UI-12) and use the same terms as the manual. They are kept in the code next to the control, not in a separate help file, so they change with it.
- g) Plugins (section 20) supply help texts and tooltips for their own fields and panels through the plugin API (PLG-05, PLG-06).
- h) Settings: tooltips can be turned off and the hover delay set; "(?)" markers always stay available.

### 18.2 Developer documentation

**DOC-10** M [OA] — ARCHITECTURE.md: layers (radio host, record pipeline, survey, app; the radio plugins and their decoder are described in yantra-radios), threads, data flow, folder layout.

**DOC-11** M [OA] — BUILD.md and CONTRIBUTING.md: build steps for Linux (clang, the default, and gcc), the Windows cross build with the SDK (OA-02) and the native MSYS2 UCRT64 build, running the Windows tests from WSL2, tests, CI, code style, how to add a test, contribution and licence rules (TST-60, NFR-01).

**DOC-12** M [NEW] — File formats (NFR-05): recording format, project file, cell database, calibration table, export columns. Each has a version number and an example.

**DOC-13** S [NEW] — Remote API reference (API-01/02): endpoints, JSON schemas, examples.

**DOC-14** M [OA] — THIRD_PARTY.md and REUSE.toml: every library, its licence and how it is used (16.2).

**DOC-15** (moved to yantra-radios, RAD-95: DEVICES.md lives there; Yantra's README and manual link to it.)

**DOC-16** M [OA] — CHANGELOG.md per release, written for users.

**DOC-17** M [NEW] — Plugin API documentation (section 20), published with every release (the radio API is documented in yantra-radios, RAD-97):

- Reference: every function, struct, enum and callback of the plugin header, generated from comments in the header (Doxygen), with units, value ranges, ownership and which thread calls it.
- Guide (PLUGINS.md): plugin kinds, life cycle (load, init, configure, start, process, stop, unload), threading rules, the telemetry data model and schemas, error handling, settings, logging, versioning and the licence rule (PLG-10).
- Tutorial: building the example plugin (PLG-15) step by step on Windows and Linux, from an empty folder to a plugin that shows up in the app, records, exports and passes the test kit.
- How the built-in TETRA plugin is put together, as the reference for a real technology plugin.

**DOC-18** M [NEW] — Plugin API changelog: every change per API version, marked as compatible or breaking, with deprecations and a migration note. The documentation of an API version stays online while plugins for it are supported.

### 18.3 Screenshots

**SCR-01** M [OA] — Screenshots are produced automatically by the app itself, not by hand. A command-line option starts the app with a given window, size, theme, language and input. It renders off-screen, works under software OpenGL in CI (as in TST-04), saves a PNG, then exits.

**SCR-02** M [OA] — Screenshots use the simulator radio plugin (RAD-61, TST-10) and demo projects/recordings with invented cell data, so no real network, location or radio is shown. Demo data uses fictitious MCC/MNC/LA values and a public sample floor plan (the public-domain Fraunces Tavern plan). The MAK plans (DEMO-12) are never used in screenshots, docs or any other shipped image.

**SCR-03** M [NEW] — Screenshot set, kept in docs/img/ with stable file names:

- main survey view (live values, serving cell, neighbours);
- RSSI/C1 graph and event list;
- start screen and File menu (UI-14, UI-17);
- plan import window with several floors trimmed from one sheet;
- indoor project window with floor tabs and a walk test with legend;
- Walk tab with the live values and the Map tab with the interpolated map and the open "Advanced" dropdown (UI-18/19);
- an indoor report page with the curtat.be reference;
- About dialog and the Reset all settings dialog;
- the generated radio windows (PLG-16) of the HackRF and the simulator (settings, debug, calibration, device info);
- scanner view;
- cell database and parameter verification;
- cell reselection simulator;
- export dialog and a generated coverage report (first page);
- radio and calibration settings;
- touch layout on a tablet-sized window.

Each in dark and light theme. The main survey view is also taken in NL, FR and DE for the translated manuals.

**SCR-04** M [NEW] — A script (tools/screenshots) regenerates the whole set in one run. CI runs it on every release branch and publishes the set as an artifact. A pull request that changes a screen regenerates the affected screenshots.

**SCR-05** S [NEW] — Visual regression: CI compares new screenshots with the committed ones and reports changes above a pixel-difference threshold. Expected changes are accepted by committing the new images.

**SCR-06** S [NEW] — Short animated captures (GIF/WebM, \< 10 s) of the main workflow (indoor walk test) for the README and website, made the same way from replayed demo recordings: a new indoor project from plan import to report.

**SCR-07** M [NEW] — The manual and README only reference images from docs/img/. A docs check in CI fails on missing images or images that nothing references.

### 18.4 Documentation tests

**DOC-20** M [NEW] — CI builds the manual (Markdown to HTML and PDF, with the PDF checks of DOC-02a), checks links and images (SCR-07), and checks that every UI string has a tooltip or a manual entry where DOC-07 requires one. A UI lint (tools/lint_ui) fails the build on: a control or icon button without a tooltip, a disabled control without a reason, a "(?)" text pointing to a manual chapter that does not exist, and help texts missing in one of the UI languages.

**DOC-21** S [NEW] — Code examples in the API and file-format documentation are run in CI.

### 18.5 MVP documentation

DOC-01..05, DOC-07, DOC-10..12, DOC-14, DOC-16..18, SCR-01..04, SCR-07, DOC-20.

## 19. DEMO MODE

**DEMO-01** M [NEW] — Demo mode runs the whole application without a radio. The demo source is the simulator radio plugin of yantra-radios (RAD-61). It is offered in the radio list as "Demo (simulated network)", on the first-run screen when no radio is found, and with a command-line option (--demo).

**DEMO-02** M [NEW] — Inside the simulator the TETRA test-signal generator (RAD-70) feeds the same demodulator and decoder as the SDR plugins (RAD-61), and its records reach Yantra through the same radio API as a real radio, so everything shown is really decoded, not faked.

**DEMO-03** M [NEW] — Simulated network: several fictitious sites with invented MCC/MNC/LA values, colour codes, neighbour lists and reselection parameters. It includes one deliberate neighbour-list mismatch (for DB-05) and one parameter differing from the demo cell database (for DB-04).

**DEMO-05** M [NEW] — Simulated indoor walk: a sample floor plan with a matching level pattern, so the point-and-click walk test (GEO-02) and the acceptance report (EXP-06) can be tried. The simulator asks Yantra which floor plan is shown, the floor's fixed demo scene and the mouse or walk position through the host callback API (PLG-27, RAD-18), and calculates the levels there with its multi-wall model (RAD-73): moving the mouse over the plan measures (the mouse survey, DEMO-14), and a walk test records the same way. The demo scene of each bundled example floor (sites, DAS antennas, walls) is shipped with the demo data as a simulator scene file (RAD-36); it is never shown or edited in Yantra. The demo project includes the interpolation (IND-41, IND-43, IND-45) of its walks, so it is shown in the demo without any setup.

**DEMO-14** M [NEW] — Mouse survey with the simulator: when the simulator is the active radio and a floor plan with a scale is shown, "Mouse survey" (default in demo mode and in the tour) lets the mouse stand in for the surveyor walking:

- a hint on the plan says "Move the mouse slowly over the plan to measure; click to interpolate", and a "move slower" warning appears when the cursor moves faster than the points can follow (configurable, default 2 m/s in plan scale);
- every simulator record becomes a measured point at the position it was calculated for (sim_position, RAD-19), not at the cursor position when it arrives, with at most one point per 0.25 m (configurable); a live trail shows the points coloured by level with the normal legend; points are flagged simulated (DEMO-07);
- a click on the plan starts the interpolation (IND-41, IND-43, IND-45) over the points of this floor in the background, with progress; the interpolated layer appears when it is done; moving on adds points, and the next click runs it again; Esc stops the survey;
- the points are stored as a walk recording of the floor (IND-20, REC-03), so statistics, replay, export and the report work on them like on a real walk test (IND-30);
- the simulator reports its levels in dBm by default (RAD-61), so the points are in dBm without a calibration entry. Yantra applies no automatic or simulated calibration. When the user sets the simulator to dBFS, the points are labelled "uncalibrated" until the user calibrates it with the calibration wizard (RF-02) against the simulated signal generator (practice calibration, RAD-61).

The mouse position goes to the simulator only through the callbacks (PLG-27). Tests: a scripted cursor path over the Fraunces plan gives points at the simulated positions with the simulator's levels, a click produces the interpolation, and the report shows the same points (point identity as in TST-34); in dBm mode the points carry the simulator's dBm levels with no calibration applied, in dBFS mode they are uncalibrated.

**DEMO-06** M [NEW] — A demo project with a ready-made recording, cell database and report, so replay, export and post-processing (sections 6, 10, 11) can be explored straight away. One sample indoor report (PDF and HTML) of the Fraunces demo project (at least 2 floors, measured points and interpolation, curtat.be branding, DEMO label) is committed in docs/samples/ and linked from the README. CI regenerates it and fails when it differs beyond a set tolerance; an intended change commits the new sample.

**DEMO-16** M [NEW] — Fraunces Tavern's demo scene has a weak inner area (owner, 2026-11-17, 0.5): the points measured there are below the acceptance threshold, so the Fraunces demo report's verdict is FAIL and names that area; the other example buildings stay PASS, so the demos show both outcomes.

**DEMO-07** M [NEW] — A permanent "DEMO - simulated data" banner is shown in the UI, and the same label appears on every export and report made in demo mode. Demo data can never be mistaken for a real measurement.

**DEMO-08** M [NEW] — Demo mode needs no internet: the demo floor plan is bundled. Only the MAK example buildings of the tour (DEMO-12) need a one-time download.

**DEMO-09** S [NEW] — Demo scenarios can be selected: indoor buildings with good and weak DAS coverage, a building with a serving-cell change between floors, network with a failed site (for PP-03), and a weak-signal or high-noise case. Each is a scenario file that the screenshot script (SCR-01..04), the user manual and the tests (TST-10, TST-31) also use.

**DEMO-10** S [NEW] — The remote API (API-01/02) works in demo mode, so integrations can be developed without hardware.

**DEMO-11** M [NEW] — Guided tour (Help > Tour, and offered on first start): a step-by-step overlay that points at the real controls and lets the user click through a full indoor workflow: New indoor project, import a plan sheet, trim several floors from it, add a floor from a second source, set the scale, measure in the Walk tab with the mouse survey (DEMO-14: the tour tells the user to move the mouse slowly over the plan and to click to start the interpolation), look at the measured points and the interpolated map in the Map tab (with its Advanced dropdown, UI-19), switch floors, and make the report (File > Report). After the MAK download (DEMO-12) a step shows where the plan images were saved: the full path of the cache folder, an "Open folder" button, and that they stay there for next time (or that the bundled Fraunces project is used when the download failed). Each step can be skipped, repeated or left; progress is remembered (and reset by UI-16). The tour runs in demo mode, so the DEMO banner (DEMO-07) is shown throughout.

**DEMO-12** M [MAK] — Three example buildings for the tour, one per MAK sheet listed in section 0 (Beer house Vienna, Haus Claeson Falsterbo, Villa Bunzl Vienna). Yantra ships only an example project description per building (no image): source URL, the expected SHA-256 of the image, the trim rectangles of each floor, floor names and levels, approximate scale (marked "approximate"), the simulator scene of each floor (sites, DAS antennas, walls; DEMO-05, used by the simulator only and never shown or edited in Yantra) and the demo walk routes. On first use, after the user agrees, the app downloads the unmodified image from MAK, checks the hash and keeps it in the user's cache folder. The licence (CC BY-NC-ND 4.0) and credit ("MAK - Museum of Applied Arts, Vienna", inventory number, designer) are shown with the plan and in its reports. The image is never altered on disk, re-saved or exported; trimming and overlays are applied at display time only. No data other than the plain download request is sent. If the download fails or the hash differs, the tour says so and uses the bundled Fraunces Tavern project instead.

**DEMO-13** M [NEW] — Each example building has measured data and interpolated data: per floor, simulated walk-test recordings from the simulator (RAD-61, with the floor's fixed demo scene, DEMO-05) and the interpolation (IND-41) computed from them, so the tour shows both. The recordings are made by yantra-gen (RAD-71) at build time and shipped (they contain no MAK image data), labelled as demo data.

**DEMO-15** M [NEW] — Every demo explanation has a green background: the tour's step boxes (DEMO-11), the mouse-survey hint (DEMO-14) and every other demo hint, in every theme (dark, light, high contrast) and layout, with a text colour that keeps a contrast ratio of at least 4.5:1 (checked by the UI lint, TST-52). The DEMO banner (DEMO-07) keeps its own colour.

MVP: DEMO-01..03, DEMO-05..08, DEMO-11..15.

## 20. PLUGIN ARCHITECTURE (TECHNOLOGIES, RADIOS, FEATURES)

Yantra starts with TETRA, but other radio technologies (e.g. DMR, P25, TETRA DMO, LTE/5G broadcast information) and other radios are to be added later without changing the core. Radios are plugins of the separate project yantra-radios with their own radio API ([requirements_radios.md](requirements_radios.md)); this section covers how Yantra hosts them (20.4) and the other plugin kinds. The core is therefore technology- and radio-neutral, and every technology, radio and optional feature plugs in through one documented API (DOC-17). The core alone is a complete indoor tool.

### 20.1 Architecture

**PLG-01** M [NEW] — The core (radio host, measurement pipeline, calibration, recording, cell database, indoor projects, walk test, interpolation, export, reports, UI shell) contains no TETRA-specific or radio-specific code. TETRA is implemented as the first technology plugin, each radio as a radio plugin of yantra-radios, and optional functions (e.g. the probe) as feature plugins, all using only the public plugin API. Any feature a plugin needs that the API lacks is added to the API, never as a shortcut into the core.

**PLG-02** M [NEW] — Plugin kinds:

- Technology plugin: one radio technology. Declares the schema of its records (cell identity, parameters, quality; PLG-05), its bands and channel raster (channel \<-> frequency), and the technology rules Yantra applies: C1, cell reselection, parameter verification and neighbour cross-check (RF-06, DB-04, DB-05, DB-08). It does no signal processing: the radio plugins measure and decode (RAD-20..26).
- Radio plugin (M): one radio or radio family, from yantra-radios, through the separate radio API (yantra/radio.h, own version, RAD-10..49), not through plugin.h. It processes its own IQ samples and delivers measurement records of a technology (RAD-20..26). Yantra hosts it (PLG-25) and builds its windows (PLG-16).
- Feature plugin (M): an optional function with its own views, menu entries, recording fields and exports. Built in: the probe (PLG-24).
- Exporter plugin (S): extra export and report formats (section 10), also offered in batch export (EXP-04).

**PLG-03** M [NEW] — Stable C ABI: one public header (include/yantra/plugin.h) with plain C structs, enums and callbacks, and a single entry point that exchanges the host and plugin function tables. Plugins can be written in C, C++ or Rust and built with any compiler, independent of the compiler used for Yantra. No C++ classes, STL types or exceptions cross the boundary; memory is freed by whoever allocated it.

**PLG-04** M [NEW] — Versioning: the plugin API has its own version (major.minor). Within a major version only compatible additions are made. Yantra refuses a plugin built for another major version, or for a newer minor version than it supports, with a clear message instead of a crash.

### 20.2 Data model and integration

**PLG-05** M [NEW] — Common data model: a technology plugin declares a schema for its cell identity and parameters, matching the record schema of the radio plugins for that technology (RAD-21) (field name, type, unit, label, which fields form the unique key, which are shown by default). Cell list, neighbour analysis, cell database, walk test, recording, replay, export and reports work from these schemas, so a new technology appears in all of them without changes to the core. Levels are always in dBm through the core's calibration (RF-01..03); quality metrics carry their name and unit. Link rule (plugin API 1.2, a compatible addition): a neighbour entry's LA, MCC and MNC are the entry's own values when the record gives them (RAD-22), else the broadcasting cell's; neighbour entries are linked to cells by these and the carrier.

**PLG-06** M [NEW] — A plugin may add its own UI panels, windows, dialogs and menu entries through a drawing and menu interface offered by the host (needed for PLG-24 and technology plugins). Radio plugins have no UI code: Yantra generates their windows (PLG-16). Plugin windows follow the app's theme, touch layout, translations and help rules (UI-12/13, DOC-07). The core views work without them.

**PLG-07** M [NEW] — Threading and host services: the host calls a plugin's processing functions on the measurement thread of its radio (OA-03); they must not block, sleep or call the UI. The host offers logging, settings storage and time stamps (radio time and system clock).

**PLG-08** M [NEW] — The scope boundary (16.5) applies to every plugin: only broadcast and system information, levels and quality. Plugins declare the data categories they produce; the host never records, shows or exports speech, message content or subscriber identities, and nothing is decrypted.

### 20.3 Loading, licensing and testing

**PLG-09** M [NEW] — Plugins are shared libraries (.dll on Windows, .so on Linux), found in the application's plugins folder and the user's plugins folder (%APPDATA%/Yantra/plugins, \~/.local/share/yantra/plugins), with radio plugins in a radios/ subfolder of each (PLG-25). The settings show each plugin's name, version, API version, author, licence and status, and allow enabling and disabling it. A plugin that fails to load or reports an error is disabled with a message; the app keeps running.

**PLG-10** M [NEW] — Licence: a plugin that runs inside Yantra forms one program with it, so plugins must be under a GPL-3.0-compatible licence and declare it as an SPDX identifier, which the settings show.

**PLG-11** M [NEW] — Every technology plugin delivered with Yantra comes with a simulated radio that delivers its records (for TETRA: the simulator of yantra-radios, RAD-61) and golden recorded measurement files, so demo mode (section 19), the screenshots (SCR-02) and the tests (section 17) work for it without hardware.

**PLG-12** S [NEW] — Recordings, projects and cell databases store the technology id and the plugin version that produced each record. Without the plugin, its recorded data can still be opened read-only with the generic views.

**PLG-13** M [NEW] — Plugin test kit: a command-line harness that loads a plugin, feeds it recorded measurement files and compares its output with golden files, plus a CMake template and a CI example. The built-in plugins pass it in CI. Radio plugins are checked with the radio test kit of yantra-radios (RAD-91), which Yantra runs on every radio plugin it ships.

**PLG-14** C [NEW] — Optional out-of-process hosting: a plugin can run in a separate helper process, so a crashing plugin cannot take down the app.

**PLG-15** M [NEW] — An example technology plugin lives in the repository (a simple made-up beacon technology, whose records come from the example radio plugin of yantra-radios, RAD-64). It is built and tested in CI, and the tutorial (DOC-17) uses it, so the API and its documentation are always proven to work outside the TETRA code.

### 20.4 Radio plugins (hosted from yantra-radios)

**PLG-16** M [NEW] — Yantra builds every radio window itself, from the radio API only (RAD-49), with the same code for every plugin. The Radio menu always lists every loaded radio plugin; a plugin without a device is greyed out with the reason (e.g. "no device found", "no permission", RAD-30). Menu Radio > \<plugin> > \<device>:

- Settings window, generated from the device-specific settings (RAD-40..44): one section per group in sort order; a checkbox per bool, a combo box per enum, a slider with input field, unit and range per number, a text field per string, a button per action (with its result text); the value read back from the device next to the requested one (RAD-41); UNAVAILABLE settings greyed out with the reason; WARNING settings ask for confirmation; ADVANCED settings behind "Advanced settings"; RESTART settings apply with a stream restart after confirmation; a presets menu (RAD-43); help markers and tooltips from the help texts (DOC-07); translations from the plugin's table (RAD-48); "Reset to defaults". Rebuilt when the plugin reports settings_changed (RAD-44). Values are stored per device key and applied again after open (RAD-47).
- Debug window (Yantra's own): the latest records with all their fields and flags, record rate per carrier, the stream counters (RAD-24) and events (RAD-25), the sensors (RAD-45) and the device log (RAD-14). No IQ is shown: signal-level diagnostics of the receiver are sensors of the plugin (e.g. clipping, noise floor, frequency error).
- Calibration window (Yantra's own): the level calibration wizard (RF-02) with a signal generator or reference radio over chosen frequencies, levels and settings; frequency (ppm) calibration from a known carrier; table view with date, device serial and validity; verify mode; import/export. The calibration key is driver + serial + the CALIBRATION settings (HW-01b).
- Device info and self-test window: the device entry (RAD-30), module and firmware versions, sensors, and the self-test (RAD-46) with pass/fail per item and what to do on fail.

All windows follow the theme, touch layout, translations and help rules (UI-12/13, DOC-07). Every radio plugin appears in one radio list and runs live in the app (all panels, recording, walk test).

**PLG-25** M [NEW] — Radio host: Yantra loads every radio plugin in its radio plugin folders (PLG-09), checks the radio API version (RAD-11), passes the radio host table (RAD-14), and runs the common stream control (RAD-30..35) the same way for every plugin: enumerate, open, apply the stored settings, tune, start, stop, close. The record sink copies the records and drops and counts when behind (RAD-24), checks them against their schema, and hands them to the measurement pipeline (OA-13). DEVICE_LOST and DEVICE_BACK reopen and restart the device (NFR-04). For a device with the SCENE capability Yantra answers the context callbacks (PLG-27). A plugin that fails to load, refuses its version or crashes a call is disabled with a message; the app keeps running.

**PLG-27** M [NEW] — Host callback API for radio plugins (RAD-18, RAD-19): Yantra implements the context functions of the radio host table: get_floor_plan (the name of the floor plan shown, its example id when it is a bundled example plan such as the Fraunces Tavern or a MAK building, the floor id and name, plan size and scale state), get_cursor (the mouse position over a floor plan in the project window or walk-test view, converted to metres in the floor's plan frame with the floor scale, IND-08), get_walk_position (GEO-02) and get_scene (the fixed demo scene of a bundled example floor, DEMO-05, as versioned JSON in the RAD-36 format; for a user's own floor: no scene, the plugin then uses its own defaults). The UI thread publishes these as snapshots (OA-03) whenever they change, so the calls never block and never touch the UI; positions are updated at every mouse move over a plan (notifications at most 30 per second). Yantra calls the plugin's on_context_event on a worker thread when another plan or floor is shown, the cursor moves and the walk position changes. Every bundled example plan has a stable example id (DEMO-05, DEMO-12). The plugin manager lists the plugins that use these callbacks. Tests: the snapshot values match the project and the converted mouse position exactly (TST-50), and a plugin that is slow in on_context_event never delays the UI.

**PLG-29** M [NEW] — One repository (owner, 2026-11-20; replaces PLG-26): yantra-radios is merged into the Yantra repository with a normal merge commit (both histories kept, nothing rewritten); its tags become radios-v0.1.0 .. radios-v0.4.0; the submodule third_party/yantra-radios is removed. Plugin-first layout, visible at the top level:

```text
radio-api/    the radio API (C ABI headers, its version 1.2,
              API docs, conformance kit: RAD-02, RAD-91)
plugins/      one folder per plugin: hackrf, soapy, pei,
              simulator, example (radio plugins) and probe,
              beacon (feature plugins, PLG-24/25), each with
              its own CMakeLists.txt, README and tests
radio-common/ code shared by radio plugins only: receiver
              (DSP, TETRA decoder, meters), SoapySDR common,
              TETRA common, the Rust protocol crate
core/         Yantra's measurement core (records in, no DSP)
app/          the GUI application
gen/, tools/, sdk/, data/, docs/, tests/, packaging/,
third_party/ (osmo-tetra pinned 59fb603 without crypto/)
```

One top-level CMake project, one ctest suite, one CHANGELOG, one product version (next 0.3.0); the radio API keeps its ABI version. README and ARCHITECTURE.md open with a diagram app -> core -> radio-api \<- plugins (-> radio-common) and a table of the plugins; docs/PLUGINS.md tells how to add a plugin. The packages (NFR-09, TST-64) and the Windows cross build work as before; plugins stay self-contained libraries (RAD-132).

**PLG-30** M [NEW] — The radio boundary inside one repository: app/ and core/ include only radio-api/ headers and never link a plugin or radio-common target; plugins never include app/ or core/ headers. CMake target dependencies express this and a ctest lint (include and link check) fails on any violation. The plugins-off build (no plugin, no radio-common) still builds and passes.

**PLG-26** (replaced by PLG-29, 2026-11-20.) yantra-radios is a git submodule of Yantra pinned to a tagged commit (third_party/yantra-radios, RAD-98). Yantra's build builds the radio plugins it ships from it (HackRF, simulator, generic SoapySDR; the TETRA terminal when built), its tests use the simulator and recorded measurement files (TST-10), and its packages install the self-contained plugins (RAD-132) with their licence bundle and the udev rules (RAD-99, NFR-09); no SoapySDR or vendor package is needed on the target system. Updating the pinned commit passes Yantra's full test suite and the radio test kit (RAD-91) first. The submodule URL in .gitmodules is the absolute clone URL https://github.com/peterdewitte/yantra-radio-plugins.git (the published name of yantra-radios).

### 20.5 Feature plugins

**PLG-23** (moved to yantra-radios, RAD-80.)

**PLG-24** S [NEW] — Probe feature plugin: network remote control and the unattended fixed probe (API-02, API-03), the remote-probes view in the app, and its headless runner. Without it the app has no network listener.

MVP: PLG-01..13, PLG-15, PLG-16, PLG-25..27, DOC-17, DOC-18.

## 21. INDOOR PROJECTS (MAIN PURPOSE)

An indoor project is one building: an ordered list of floors (levels). Each floor is a part of a plan source (a PDF page or an image), trimmed out in the plan import window, with its own scale, walk tests and interpolation between the measured points. One source sheet can hold several floors (e.g. the MAK sheets show every floor and a cross-section on one page), and a project can use several sources.

### 21.1 Project and floors

**IND-01** M [NEW] — File > New indoor project (UI-14) asks for the project name, site name, operator and notes, then opens the plan import window (IND-02) to add the first floor. The project is saved as one \*.yantra file (SQLite, documented format, NFR-05/DOC-12). The file stores the paths of its plan sources and recordings relative to the project file's folder when possible (same volume), absolute otherwise, so a project folder can be moved or shared; files with absolute paths from earlier versions still open.

**IND-02** M [NEW] — Plan sources: PDF (any page, via PDFium) and every image format stb_image reads (PNG, JPEG, BMP, GIF, TGA, PSD, PNM, HDR). A PDF shows a page list with thumbnails. Large sheets (> 15000 px) are handled with tiled, zoom-dependent rendering. A source that cannot be read gives a clear message with the reason. C: TIFF and WebP, once a library for them is added to 16.2.

**IND-03** M [NEW] — The project keeps a copy of every source file inside the project (or, for licence-restricted sources such as DEMO-12, a reference to the cached file with its SHA-256), so a project can be moved to another computer. The source is never modified.

**IND-04** M [NEW] — Project window: one tab (or list entry) per floor, ordered by level, showing the trimmed plan with the measured points and the interpolation layer (each layer can be shown or hidden in the Map tab, UI-19). Floors can be renamed, reordered, given a level number and height, and removed (with confirmation).

### 21.2 Plan import and trimming window

**IND-05** M [NEW] — When a source is added, the plan import window opens with the whole page or image, with zoom and pan. The user trims the part that is one floor: a rectangle, or a polygon for irregular outlines (to leave out legends, title blocks, cross-sections and other floors). Rotation by 90 degree steps and a fine deskew are offered.

**IND-06** M [NEW] — "Add as floor" turns the current trim into a floor of the project and asks for its name and level (e.g. "Ground floor", 0). The window stays open, so more floors can be trimmed from the same source one after the other. Trims already added are outlined and labelled on the source, and can be edited or removed there.

**IND-07** M [NEW] — Trimming is non-destructive: a floor stores its source, page, trim shape, rotation and deskew, not a new image. Re-opening the import window shows the trims again and lets the user adjust them.

**IND-08** M [NEW] — Scale per floor: two points and their real distance (or a scale bar on the plan), in metres. Floors from one sheet can share one scale. A floor without scale is allowed but marked, and its report shows distances and areas as "not to scale".

**IND-09** S [NEW] — Floor alignment: a common reference point (e.g. a staircase corner) on every floor, so points line up between floors in the multi-floor view.

**IND-10** M [NEW] — Add floor (File > Add floor, and a "+" next to the floor tabs): choose a new source file (PDF or image) or an already used source, then trim the floor in the plan import window (IND-05..07).

### 21.3 Building model (removed in 0.4)

**IND-11, IND-12** (removed in 0.4: no walls and no antennas in Yantra; the demo simulator scenes keep theirs as data, DEMO-12. Not to be reused.)

### 21.4 Measured points

**IND-20** M [NEW] — Walk tests per floor (GEO-02, GEO-05) are stored with plan positions in metres (and pixels for display). Several walk tests per floor, each with date, radio, calibration and notes.

### 21.5 Report and window show the same points

**IND-30** M [NEW] — The report (EXP-05/06) and the project window use the same sample set: every measured point drawn on a floor in the project window appears in that floor's report at the same position, with the same value and colour scale. No decimation, thinning, binning or re-sampling is done for the report. When a page is dense the markers are drawn smaller, never dropped; a detail view per area may be added. The report states the number of points per floor, which equals the count in the project window (TST-34). Binning (PP-05) is an extra, labelled view, never a replacement.

### 21.6 Interpolation between the measured points

**IND-40** (removed in 0.4 with the ray tracer; not to be reused.)

**IND-41** M [NEW] — The coverage map of a floor is an inverse-distance-weighted (IDW) interpolation of the measured levels of that floor's walk tests on a grid (default 0.5 m, settable), power 2 and search radius 10 m by default (settable, UI-19), in dB. It uses only the measured points: no walls, antennas or propagation model. A grid cell holding a measured point takes that point's level (median when several). Own code (no extra library).

**IND-42** (removed in 0.4: no estimated sources; not to be reused.)

**IND-43** M [NEW] — The interpolated map is shown as a colour layer with the same legend and thresholds as the measured points, a confidence mask that fades areas far from any measured point, and hover readout (value, measured or interpolated, nearest sample). It is always labelled "interpolated" and kept apart from measured values in statistics: the report gives both "Share of measured points (%)" and "Share of floor area (interpolated) (%)" against the acceptance profile (EXP-06), with the same wording in NL, FR and DE.

**IND-44** (removed in 0.4: no inter-floor propagation; not to be reused.)

**IND-45** M [NEW] — Interpolation runs in the background (never blocks the UI, OA-03), shows progress, can be cancelled, and is stored in the project with its parameters, so reports are reproducible. It is part of the demo (DEMO-05, DEMO-13).

**IND-46** M [NEW] — Project files from 0.3 and earlier open: their walls, wall classes, antennas and ray-tracing maps are dropped on load (the file is migrated on the next save, with a backup of the old file), a note says so once, and the map is recomputed with IND-41 on request.

**IND-47** M [NEW] — "About building" (owner, 2026-11-17, 0.5): the first tab of every project, which cannot be removed, holds: the address (free text; "Find on map" geocodes it with Nominatim only when pressed, with a correct User-Agent and the OSM attribution), an OpenStreetMap map of the location (tiles fetched only when shown, cached, usage policy respected; works offline with the cached tiles or without a map), a photo of the building (opened from a file or taken with a camera: Yantra's own capture code over V4L2 on Linux and Media Foundation on Windows, stored in the project), the surveyor's name, the survey date (default today) and notes. The report (EXP-05) starts with this information. Nothing of it leaves the computer except the geocoding query and tile requests the user triggers.

**IND-48** M [NEW] — Notes on the plan (owner, 2026-11-17, 0.5): a long click (long press on touch) on a spot of the floor plan opens a window to add a note there: a picture (file or camera, IND-47), a text and/or an audio fragment (recorded and played with miniaudio). Notes show as pins on the plan (Map tab, toggle in the Show row), can be opened, edited, moved and deleted, are stored in the project and listed in the report with their picture and text (audio as "audio note, n s").

MVP: IND-01..08, IND-10, IND-20, IND-30, IND-41, IND-43, IND-45, IND-46, BRD-01..07.

## 22. CURTAT.BE REFERENCE AND BRANDING

**BRD-01** M [CUR] — Yantra is made by curtat.be (https://curtat.be) as a reference on building new programs with Claude, Anthropic's AI assistant. This statement appears in the About dialog (UI-15), the README (DOC-01) and the user manual's introduction.

**BRD-02** M [CUR] — Every report (EXP-05/06, PDF and HTML) carries the curtat.be logo and the reference "Made with Yantra - curtat.be" with the link https://curtat.be, on the cover page and in the footer of every page. Demo reports carry it next to the DEMO label (DEMO-07).

**BRD-03** M [CUR] — The logo is supplied by the owner in the requirements project under assets/branding/ (see its README.md) and moved into the Yantra repository under data/branding/: curtat_logo.svg (master) and PNG renders for light and dark backgrounds at 64, 256 and 1024 px. The app (stb_image) and the PDF reports (libharu) use the PNGs, picking the variant that matches the theme or page background; the HTML report may embed the SVG. No SVG library is added (16.2).

**BRD-04** M [CUR] — The logo is a trademark of Curtat BV and is not covered by GPL-3.0. It gets its own entry in REUSE.toml (LicenseRef-Curtat-Trademark) and THIRD_PARTY.md, with the note that forks must remove or replace it. It is kept in its own folder (data/branding/).

**BRD-05** M [NEW] — Yantra's own logo (a floor plan with a walk-test route, a map pin and a radio) is supplied by the owner in the requirements project under assets/yantra_logo/ (see its README.md) and moved into the Yantra repository under data/logo/: yantra_logo.svg (master, rounded square), yantra_logo_square.svg, yantra_glyph.svg and png/ with renders at 16, 24, 32, 48, 64, 128, 256, 512 and 1024 px. The build uses the files as supplied and never redraws or recolours them; renders are only remade in the requirements project with tools/build/render_yantra_logo.py. The github/, linkedin/ and web/ folders are for the owner to upload by hand and are not moved.

**BRD-06** M [NEW] — The Yantra logo is used as: the application and window icon (PNG through stb_image, as BRD-03); the icon in the Linux .desktop file, installed by the .deb and AppImage into the hicolor icon theme at 16..512 px (NFR-09); the Windows .exe and installer icon once Windows is supported (an .ico built from the PNGs by Yantra's packaging scripts); the About dialog next to the name (UI-15); the top of README.md (DOC-01) and the title page of the user manual (DOC-02); the favicon of the HTML report. Reports keep the curtat.be logo and reference as BRD-02 requires.

**BRD-07** M [NEW] — The Yantra logo is part of Yantra and licensed GPL-3.0-or-later like the code (NFR-01), with a REUSE.toml entry for data/logo/. Unlike the curtat.be logo (BRD-04), forks may keep it.

MVP: IND-01..08, IND-10, IND-20, IND-30, IND-41, IND-43, IND-45, IND-46, BRD-01..07.
