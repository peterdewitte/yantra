<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Yantra architecture

This page describes how the code is put together (DOC-10): the layers,
the threads and queues, the data flow from a radio record to a stored
point, recording or report, the folder layout and the dependency on
yantra-radios. The topic documents in `docs/` hold the details; this page
links them instead of repeating them. Requirement IDs refer to Yantra's
requirements specification.

## Overview

```
                 yantra-radios (git submodule, third_party/yantra-radios)
                 radio plugins *.so: SDR processing, TETRA decoder, simulator
                                        |
                       radio.h C ABI: measurement records only (RAD-20..26)
                                        v
 +---------------------------- yantra_core (static library) -----------------------------+
 | plugin/   loader of plugin.h plugins (technology, feature, exporter), state, licence   |
 | radio/    radio host: radio plugins, devices, settings, sink queues, scene export      |
 | measure/  measurement pipeline: schema mapping, level correction, C1, checks, cycles   |
 | recording/ recorder (yantra-recording/1), reader, replay, cut, voice                  |
 | survey/   walk tests, points, statistics, binning, interpolation, acceptance, viewer  |
 | building/ plan/ project/ celldb/ calibration/ context/   indoor model and storage     |
 | raytrace/ ray tracer, fit, residuals          export/ report/   outputs               |
 | control/ command layer   headless/ Runner   util/ jobs, SHA-256   platform/          |
 +----------------------------------------------------------------------------------------+
        ^                    ^                         ^                    ^
   app (yantra)        tools/yantra-probe       plugins/*.so (plugin.h)   tests/
   model / gui /       headless runner          tetra, probe,             doctest suites
   audio / net                                  example_beacon            under ctest
```

Every arrow points from the user of a layer to the layer it uses. The
core knows no window, no TETRA and no radio hardware (PLG-01): technology
rules come from technology plugins, radios from radio plugins, the remote
API from a feature plugin.

## The radio boundary and yantra-radios

- **Repository split (PLG-26).** The radio plugins, their receiver DSP,
  the TETRA decoder and the test-signal generator live in the
  yantra-radios project, a git submodule in `third_party/yantra-radios`
  pinned at v0.3.0 (6fac3ee; radio API 1.1). Yantra depends on yantra-radios, never the
  reverse; moving the pin is a commit of its own (CHANGELOG.md).
- **What is compiled in.** Only the radio API header (`radio.h`, CMake
  target `yantra::radio_api`) is compiled into Yantra. The submodule is
  built as a separate CMake build (`cmake/YantraRadiosBuild.cmake`,
  BUILD.md "Radio plugins") whose shipped plugins (hackrf, simulator,
  soapysdr, pei) land in the radio plugin folder and are loaded at run
  time. A prebuilt yantra-radios release can be used instead
  (`YANTRA_RADIOS_PREBUILT`).
- **What crosses it (RAD-20..26).** Measurement records only: levels,
  cell identity, optional quality, C1, broadcast parameters and
  neighbours, plus device events, settings and sensors. No IQ samples,
  PDUs or slots reach Yantra; `~/yantra` has no DSP, decoder or SoapySDR
  code. In the other direction the host gives the plugins a context
  (building scene, floor, positions; PLG-27, `context/`), which the
  simulator uses to compute levels.
- The radio plugins themselves, their decoder and generator are
  described in yantra-radios (its ARCHITECTURE.md and RADIO_PLUGINS.md).

## Layers

### Plugins (`plugin/`, `include/yantra/plugin.h`)

`plugin.h` is Yantra's own C plugin API (PLG-03, version 1.1; reference
by Doxygen, guide [docs/PLUGINS.md](docs/PLUGINS.md), summary
[docs/PLUGIN_API.md](docs/PLUGIN_API.md), changes
[docs/PLUGIN_API_CHANGELOG.md](docs/PLUGIN_API_CHANGELOG.md)). The loader
(`plugin/loader.cpp`) finds plugins in the application and user folders
(PLG-09), checks the ABI and licence (PLG-10), keeps their on/off state
and settings (`state_store.cpp`) and runs their timers. Kinds:

- **technology plugins** give record schemas, field help, C1 and
  reselection rules and cell-database columns: `plugins/tetra`
  ([docs/plugins/TETRA.md](docs/plugins/TETRA.md)) and the example
  `plugins/example_beacon` (PLG-15);
- **feature plugins** (API 1.1, PLG-02) receive every accepted record
  (`measure/feature_feed.h`, FeatureFeed) and may call the application's
  commands through the `control` service: `plugins/probe` is the remote
  API (REST and WebSocket on CivetWeb, PLG-24, API-02;
  [docs/PROBE.md](docs/PROBE.md), [docs/API.md](docs/API.md));
- **exporter plugins** add export formats (`export/plugin_export.cpp`).

Plugins may draw their own UI (PLG-06) through the app; the generated
radio windows (PLG-16) are made by the app from the radio plugins'
setting and sensor definitions.

### Radio host (`radio/`)

`RadioHost` (PLG-25, NFR-04) loads the radio plugins (radio.h), checks
their API version, enumerates devices, opens and configures them with
their stored settings (RAD-47), starts streams and receives records and
events through a non-blocking sink per stream. It reopens a device after
an unplug (DEVICE_BACK), resolves the calibration key of a device at each
record's measurement time (HW-01b) and gives control calls a time limit
(RAD-15) on a control thread. `scene_export.cpp` writes the building as
the yantra-radios scene format (RAD-36) for simulated radios.

### Measurement pipeline (`measure/`)

`MeasurementPipeline` (OA-13, RF-01..08) maps each radio record to the
technology plugin's schema (`TechnologyCatalog`), applies the level
correction (calibration table, antenna gain; RF-02, HW-09), adds an
estimated C1 (RF-06), refuses implausible and duplicate records, flags
late ones, and closes a measurement cycle per period (1 s by default,
at least 1 Hz) with one sample per carrier. Observers get every accepted
record and every cycle. Details:
[docs/MEASUREMENT_PIPELINE.md](docs/MEASUREMENT_PIPELINE.md),
[docs/CALIBRATION.md](docs/CALIBRATION.md). Record files of
yantra-radios (RAD-65) are read by `record_file.cpp` and fed through the
same pipeline.

### Recording (`recording/`)

The `Recorder` writes the pipeline's numbered inputs, cycle summaries,
keyframes, markers and voice notes as JSON Lines, optionally zstd
compressed (REC-01..11, format
[docs/formats/RECORDING.md](docs/formats/RECORDING.md)); `reader`,
`replay` and `cut` read them back. Replay runs the same pipeline with a
fixed corrector and a manual clock, so a replay gives the same cycles as
the live run.

### Survey (`survey/`)

Walk tests (GEO-02, GEO-05, IND-20) turn cycles into measured points on a
floor (`walk_test.cpp`), stored in the project; `stats`, `bins`,
`acceptance` (EXP-06), `interpolation` (stored maps) and the
post-processing viewer and views (PP-01..03) work on those points.
Details: [docs/SURVEY.md](docs/SURVEY.md).

### Indoor core: building, plans, project, cell database, calibration

- `building/`: floors, scale, reference point, slabs, walls with
  per-band wall classes, antennas, cells; JSON `yantra-building`
  ([docs/BUILDING_MODEL.md](docs/BUILDING_MODEL.md)).
- `plan/`: plan sources (images through stb_image, PDF through PDFium),
  trims, tiles with a memory budget, dark-line wall proposals
  ([docs/PLANS.md](docs/PLANS.md)).
- `project/`: the project file, one live SQLite database with schema
  migrations ([docs/formats/PROJECT.md](docs/formats/PROJECT.md)).
- `celldb/`: cell database, CSV import, verification, cross-check and
  reselection simulation (DB-01..07,
  [docs/CELL_DATABASE.md](docs/CELL_DATABASE.md)).
- `calibration/`: the calibration table, its wizard and the
  `TableCorrector` used by the pipeline.
- `context/`: the context the radio host hands to plugins (floor, mouse
  and walk positions, scene; PLG-27).

### Propagation (`raytrace/`, `survey/interpolation`, `util/job`)

The ray tracer (IND-40) works per floor in plan metres, with SIMD and
scalar kernels (`kernels_simd.cpp`, `kernels.cpp`, the same results,
TST-03). `fit.cpp` fits EIRP and wall losses to measured points (IND-41),
`residuals.cpp` interpolates the residuals and estimates unknown sources
(IND-42, IND-43). Long runs are `util::Job`s with progress and cancel.
Details: [docs/PROPAGATION.md](docs/PROPAGATION.md).

### Export and reports (`export/`, `report/`)

CSV, DXF, floor images and batch export (EXP-01..04, EXP-08;
[docs/EXPORT.md](docs/EXPORT.md)) and the coverage and acceptance report
as PDF (libharu) and self-contained HTML (EXP-05;
[docs/REPORT.md](docs/REPORT.md)).

### Command layer and headless runner (`control/`, `headless/`)

`control/command.h` is the one command set of API-01 (record, monitor,
play, sound, status, rotate, quit) in text and JSON form. The headless
`Runner` (OA-01, API-03) wires plugins, radio host, pipeline, recorder
and replay without a window and executes those commands; it is the
`Controller` behind the plugin API's `control` service.
`tools/yantra-probe` is its command-line front end. The desktop app has
its own controller (`app/model/app_control.h`) over the same command set.

### Application (`app/`)

- `app/model` (`yantra_app_model`): all state and actions of the
  desktop app without drawing (AppState, project window, live survey,
  plan import, editors, settings, translations, help texts, report and
  export dialogs, tour). The UI tests drive this model and the real GUI
  headless (TST-50, [docs/APP.md](docs/APP.md)).
- `app/gui` (`yantra_app_gui`): the Dear ImGui views; they show what the
  model returns and call its commands; also the screenshot mode
  (SCR-01, `screenshot.cpp`).
- `app/audio` (`yantra_audio`): miniaudio for alerts and voice notes
  (UI-09, REC-06).
- `app/net` (`yantra_app_net`): libcurl for the MAK sheet download
  (DEMO-12) and optional sharing (EXP-07); linked only into the `yantra`
  executable and its tests.
- `app/src/main.cpp`: GLFW window, OpenGL 3 backend, main loop.

### Tools

`tools/yantra-probe` (headless probe, API-03), `tools/yantra-demo`
(builds the Fraunces and MAK demo projects at build time, DEMO-01..06,
[docs/DEMO.md](docs/DEMO.md)), `tools/yantra-plugin-test` (plugin test
kit, PLG-13), `tools/screenshots` (SCR-04), `tools/ci` (lint, doc checks,
UI lint).

## Data flow

```
radio plugin --record--> RadioHost sink queue --> delivery thread
   --> MeasurementPipeline (map, correct, C1, checks)
         |-- on_record --> Recorder (recording file)
         |              --> FeatureFeed --> feature plugins (probe live view)
         '-- on_cycle  --> WalkTest --> points in the project (SQLite)
                       --> survey panels, alerts (app) / Runner status
project points + building --> ray tracer / fit / residuals --> stored maps
project + maps + cell DB  --> report (PDF, HTML), export (CSV, DXF, images)
recording file --> replay --> the same pipeline (post-processing, PP-01)
```

## Threads and queues

Every thread Yantra starts is named (OA-03,
`core/include/yantra/core/platform/thread_name.h`; names start with
`yt-`, visible in `top -H` and debuggers). Rules for plugins are in
PLG-07 and docs/PLUGINS.md.

| Thread | Started by | Work |
|---|---|---|
| main / UI | app, yantra-probe | window and ImGui frame, model actions; plugin init/start/stop |
| `yt-radio-deliv` | radio host, one per stream | takes records from the stream's sink queue into the pipeline and its observers |
| `yt-radio-ctl` | radio host | control calls to a plugin with their time limit (RAD-15) |
| `yt-radio-task`, `yt-radio-notify` | radio host | device open/reopen work; context notifications to plugins (30/s cursor limit) |
| `yt-pipeline` | CycleRunner | ticks the pipeline: closes measurement cycles, calls cycle observers |
| `yt-rec-flush` | Recorder | writes queued lines to disk at least every 0.5 s with fdatasync (REC-10) |
| `yt-replay` | replay | feeds a recording through the pipeline |
| `yt-job`, `yt-raytrace` | util::Job, ray tracer | interpolations, fits, demo builds; ray-tracing workers |
| `yt-plan-tiles` | TileRenderer | renders plan tiles in the background |
| `yt-plugin-timer` | plugin loader | plugin timer callbacks |
| `yt-export` | app ReportExport | export, batch and report jobs, one at a time |
| `yt-sheet-fetch`, `yt-radio-window` | app | MAK sheet download; radio window control calls |
| `yt-probe-work`, CivetWeb workers | probe plugin | summaries and remote polling; HTTP and WebSocket requests |
| `yt-stdin` | yantra-probe | reads commands from standard input |

Queues are bounded, so a slow consumer never blocks a radio or grows
memory without limit:

- the radio host's sink per stream holds at most `sink_capacity` (4096)
  records; when full, records are dropped and counted (RAD-24);
- the recorder's queue for its flush thread is bounded by
  `RecorderConfig::max_pending_bytes`; when the disk cannot keep up, lines
  are dropped and counted in the recorder status (OA-03);
- pipeline observers run on the delivery or ticking thread and must not
  block; feature plugins' `on_record` takes a short lock and does no I/O
  (PLG-07);
- the app's remote control queues commands to the next UI frame
  (`{"queued":true}`), so the UI thread never waits for a client.

## Folder layout

| Folder | Content |
|---|---|
| `core/` | `yantra_core`: `include/yantra/core/<module>/` headers, `src/<module>/` sources (modules above) |
| `include/yantra/` | public plugin API: `plugin.h` (C ABI) and `plugin.hpp` (C++ helpers) |
| `app/` | desktop application: `model/`, `gui/`, `audio/`, `net/`, `src/main.cpp` |
| `plugins/` | Yantra's plugin.h plugins: `tetra`, `probe`, `example_beacon` |
| `tools/` | yantra-probe, yantra-demo, yantra-plugin-test, screenshots, CI scripts |
| `tests/` | one folder per module, doctest binaries `test_<module>` under ctest; fixtures, fuzz targets, benchmarks, install check |
| `third_party/` | vendored libraries (Dear ImGui, stb, nlohmann/json, CivetWeb, miniaudio, doctest) and the yantra-radios submodule; THIRD_PARTY.md |
| `data/` | logo, branding, fonts, translations, demo descriptions, desktop entry |
| `docs/` | topic documents, file formats (`docs/formats/`), manual, quick starts, images, sample report |
| `cmake/` | build modules: warnings, sanitizers, tests, PDFium, yantra-radios |

## Build targets

`yantra_core` (static) uses only `yantra::plugin_api`, `yantra::radio_api`,
nlohmann/json, SQLite, zstd, libharu, stb and, when found, PDFium. The app
libraries (`yantra_app_model` -> `yantra_app_gui`, `yantra_audio`,
`yantra_app_net`) use the core; the executable `yantra` links all of them
with GLFW and OpenGL. Plugins link only the plugin API header (and
CivetWeb for the probe), never the core: they talk to Yantra through
`plugin.h` alone. The tests link what they test. BUILD.md lists the
build trees and options.
