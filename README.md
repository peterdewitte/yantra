<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

![Yantra logo](docs/img/yantra_logo_128.png)

# Yantra

An open-source tool for measuring and reporting TETRA coverage inside
buildings, floor by floor, with a software-defined radio (SDR).

## Architecture

Yantra is built around plugins. The application and its core know two
small C interfaces, and everything that touches a radio lives in a
plugin (an arrow means "uses"; [ARCHITECTURE.md](ARCHITECTURE.md) has
the details):

```
  app/ --> core/ --> radio-api/  <-- plugins/ radio plugins  --> radio-common/
                |    (radio.h)        hackrf soapy pei             gen/
                |                     simulator example
                |
                +--> plugin-api/ <-- plugins/ Yantra plugins
                     (plugin.h)       tetra probe beacon
```

A radio plugin owns its device, processes its own IQ samples (with the
receiver and TETRA decoder of `radio-common/`) and hands the core
measurement records only. The core and the app never include or link
a plugin or `radio-common/`, and the radio parts never use the app or
the core: the ctest `boundary_lint` checks this both ways (PLG-30).

| Folder | What it holds |
|---|---|
| [`app/`](app/README.md) | the desktop application `yantra` (windows, tour, help) |
| [`core/`](core/README.md) | the measurement core: radio host, pipeline, survey, map, reports, project files; no DSP |
| [`radio-api/`](radio-api/README.md) | the radio API `yantra/radio.h` (C ABI, version 1.2), the only interface to the radio plugins |
| [`plugin-api/`](plugin-api/README.md) | Yantra's plugin API `yantra/plugin.h` for technology, feature and exporter plugins |
| [`plugins/`](plugins/README.md) | one folder per plugin, radio plugins and Yantra plugins side by side (table below) |
| [`radio-common/`](radio-common/README.md) | code shared by the radio plugins only: receiver, TETRA decoder, SoapySDR and TETRA glue |
| [`gen/`](gen/README.md) | the TETRA test-signal generator library (yantra-gen) |
| [`tools/`](tools/README.md) | programs (yantra-demo, yantra-probe, yantra-plugin-test, yantra-radio-host, yantra-gen, yantra-hackrf) and scripts (CI, docs, SDK, dependencies, Windows) |
| `data/` | demo projects, scenarios, translations, fonts, logos |
| `docs/` | user manual, quick starts, developer and format documentation |
| `tests/` | the one ctest suite (`tests/radio`: the radio parts' tests), labelled per area |
| `packaging/` | the .deb and the Windows zip and setup |
| `udev/` | USB permissions for the SDRs, RTL-SDR driver blacklist |
| `cmake/`, `third_party/` | build helpers; pinned third-party sources (osmo-tetra 59fb603 without `crypto/`) |

The plugins:

| Plugin | Kind | Device or purpose | Capabilities | Status |
|---|---|---|---|---|
| [`hackrf`](plugins/hackrf/README.md) | radio | HackRF One | levels, cell information, band scan, spectrum, hot-plug | shipped; tested with a fake device |
| [`soapy`](plugins/soapy/README.md) (`soapysdr`) | radio | RTL-SDR, Airspy and network radios through SoapySDR | levels, cell information, spectrum, hot-plug | shipped; tested with fake devices |
| [`pei`](plugins/pei/README.md) | radio | a TETRA terminal over its PEI serial port | the terminal's own measurements in dBm, hot-plug | shipped; tested with a simulated terminal |
| [`simulator`](plugins/simulator/README.md) | radio | a simulated TETRA network, decoded by the real receiver | simulated, floor-plan scene, spectrum | shipped; drives the demo and the tour |
| [`example`](plugins/example/README.md) (`beacon`) | radio | the radio plugin tutorial's made-up beacon | simulated | example, not shipped |
| [`tetra`](plugins/tetra/README.md) | technology | TETRA schemas, C1, reselection, cell database columns | technology rules | shipped |
| [`probe`](plugins/probe/README.md) | feature | remote API (REST, WebSocket) and the fixed probe | feature, records feed | shipped |
| [`beacon`](plugins/beacon/README.md) | technology | the plugin tutorial's technology for the beacon | technology rules | example, not shipped |

None of the radio plugins has been measured on real hardware yet
([docs/DEVICES.md](docs/DEVICES.md)). How to add a plugin:
[docs/PLUGINS.md](docs/PLUGINS.md#add-a-plugin).

## What Yantra is

Yantra is made by [curtat.be](https://curtat.be) as a reference on building
new programs with Claude, Anthropic's AI assistant.

Yantra is an indoor tool, for example for DAS and indoor-coverage
projects:

- describe the building in its first tab, About building: the address
  with an OpenStreetMap map, a photo (from a file or taken with the
  camera), the surveyor, the survey date and notes;
- import floor plans (PDF or image), cut several floors out of one sheet,
  scale them and line them up;
- walk the floors with a radio and click your position on the plan (or
  practise with the mouse and the simulated radio);
- keep notes on the plan: a long press on a spot adds a picture, a text
  and a short audio recording, shown as a numbered pin;
- watch the cells live: levels, C1, quality, the decoded broadcast
  information and the neighbour lists, and the TETRA band's spectrum and
  waterfall (Tools > Spectrum) from a radio that measures it;
- check the network against its cell database;
- get a coverage map interpolated from your measured points, with a
  confidence mask where nobody measured;
- write a coverage and acceptance report (PDF and HTML) per floor and for
  the building, against acceptance profiles on the measured points, the
  tested area or the floor area; a failing profile names its weak areas,
  and a popup opens the finished report.

The project window has two tabs for the two parts of the work: **Walk**
(the radio, the live values, the walk test and the recording) and **Map**
(show the plan, the points and the coverage map, compute the map, and an
Advanced dropdown for the interpolation, binning, post-processing and
acceptance settings). The survey windows (neighbours, graphs, scanner,
events, statistics) open from the Window menu, and the report is
File > Report. Yantra models no walls or antennas: the map comes from
the measured points alone (inverse-distance weighting, IDW,
[docs/INTERPOLATION.md](docs/INTERPOLATION.md)).

Yantra reads only broadcast and system information, levels and quality.
Short data messages, subscriber addresses, voice and decryption are out of
scope by design, and there are no outdoor drive tests or GPS; the only
map is the OpenStreetMap map of the building's address. See
the [legal and scope note](docs/LEGAL.md).

A sample report of the demo project, as Yantra writes it:
[PDF](docs/samples/fraunces_report.pdf) and
[HTML](docs/samples/fraunces_report.html).

## Supported radios

Radios are plugins (see [Architecture](#architecture)). The shipped
radio plugins:

- **Simulator**: a simulated TETRA network and devices, no hardware
  needed; it drives the demo and the tour.
- **HackRF**: HackRF One.
- **SoapySDR**: any SoapySDR radio, for example RTL-SDR and Airspy, and
  network radios through SoapyRemote.
- **TETRA terminal (PEI)**: a terminal's own measurements over its serial
  port (terminal mode).

Hardware status: none of these radios has been measured with Yantra on
real hardware yet; they are tested with simulated and fake devices. The
hardware tested so far is listed in [docs/DEVICES.md](docs/DEVICES.md).

Yantra's own plugins: the TETRA technology plugin and the probe plugin
(the remote API of the headless probe).

## Install

**Linux**: build from source; `ninja -C build deb` then makes a .deb
package of the build ([BUILD.md](BUILD.md#linux-package-nfr-09)). The radio plugins
are built with the application (PLG-29); their osmo-tetra decoder
library is a git submodule:

    git clone https://github.com/peterdewitte/yantra.git yantra
    cd yantra
    git submodule update --init --recursive
    cmake --preset default
    ninja -C build
    ctest --test-dir build -j"$(nproc)"

[BUILD.md](BUILD.md) has the dependencies, the build options (the
radio parts alone: `cmake --preset radio-parts`), the USB permissions
for the radios and the plugin folders.

**Windows** (10 or 11, x64): two packages of the same files, made from
the cross build on Linux ([BUILD.md](BUILD.md#windows-packages-nfr-09)):

- `yantra-<version>-windows-x86_64.zip`, the portable zip: unpack it
  anywhere and start `bin\yantra.exe`; nothing is installed;
- `yantra-<version>-windows-x86_64-setup.exe`, the setup: installs for
  the current user (no administrator rights) into
  `%LOCALAPPDATA%\Programs\Yantra`, with a Start menu entry and an
  uninstaller (Settings > Apps); silent: `setup.exe /S`.

Both hold the app, `yantra-probe`, the plugin test kit, the plugins with
the self-contained radio plugins and the radio tools
(`yantra-hackrf`, `yantra-radio-host`, the test-signal generator
`yantra-gen` with its scenarios), every DLL they need, the demo project,
the PDF user manual and the licence texts (`SOURCE.txt` names the source).
Yantra needs a graphics driver with OpenGL 3.3. USB radios (HackRF One,
RTL-SDR, Airspy) need the WinUSB driver, installed once per device with
[Zadig](https://zadig.akeo.ie). User settings and data are in
`%APPDATA%\yantra`, caches in `%LOCALAPPDATA%\yantra`.

## First steps

1. Start `build/yantra`. The start screen offers the tour, the demo
   project, a new indoor project and your recent projects.
2. Take the tour (Help > Tour, or `yantra --tour`): it goes through a
   whole indoor survey of an example building with the simulated radio,
   from the plan to the report, without hardware.
3. Then follow the quick starts:
   [first indoor project](docs/quickstart/01-first-indoor-project.md),
   [indoor walk test with acceptance report](docs/quickstart/02-walk-test-acceptance.md)
   and [calibrate a radio](docs/quickstart/03-calibrate-a-radio.md).

`yantra --demo` opens the demo project (the Fraunces Tavern, two floors,
simulated walks) directly.

![The start screen](docs/img/start_menu_dark.png)

The indoor workflow in eight seconds, from the start screen through the
plan import, the walk test and the coverage map to the report (also as
[WebM](docs/img/indoor_workflow.webm)):

![The indoor workflow: plan import, walk test, coverage map, report](docs/img/indoor_workflow.gif)

## Documentation

- [User manual](docs/manual/README.md): every window and function; also
  in the app (F1 and the Help buttons) and as one PDF with contents,
  bookmarks and an index, `yantra-manual-<version>.pdf` (Help > User
  manual (PDF); in the packages, or `ninja -C build docs-manual`). The
  manual is in English only for now; the quick starts are also in Dutch,
  French and German (DOC-06).
- [Quick starts](docs/quickstart/README.md): one page each.
- [Measurement method note](docs/METHOD.md): what is measured and how,
  calibration and its uncertainty, the interpolation and its limits.
- [Legal and scope note](docs/LEGAL.md): receive-only, what is decoded,
  privacy and licences.
- For developers: [architecture](ARCHITECTURE.md),
  [building](BUILD.md), [contributing](CONTRIBUTING.md),
  [plugins](docs/PLUGINS.md),
  [the building model](docs/BUILDING_MODEL.md),
  [the measurement pipeline](docs/MEASUREMENT_PIPELINE.md),
  [calibration](docs/CALIBRATION.md),
  [the cell database](docs/CELL_DATABASE.md),
  [the coverage map](docs/INTERPOLATION.md), [reports](docs/REPORT.md),
  [the headless probe](docs/PROBE.md) and its [API](docs/API.md),
  [the demo](docs/DEMO.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Licence and credits

Yantra is free software under the GNU General Public License, version 3 or
later (`GPL-3.0-or-later`, see [LICENSE](LICENSE)). The radio plugins
contain AGPL-3.0 parts (osmo-tetra); the complete corresponding source is
available at the source link below. Every file carries an SPDX header or a
`REUSE.toml` entry; the project is checked with
[reuse](https://reuse.software). Third-party libraries, tools, fonts and
data are listed with their licences in [THIRD_PARTY.md](THIRD_PARTY.md).

The Yantra logo in `data/logo/` is part of Yantra (GPL-3.0-or-later). The
curtat.be name and the logo in `data/branding/` are a trademark of Curtat
BV and are not covered by the GPL; forks must remove or replace the logo.

Source: https://github.com/peterdewitte/yantra
(clone: https://github.com/peterdewitte/yantra.git). Homepage:
https://curtat.be. Maintainer: Curtat BV <info@curtat.be>.

## Project history

- 2026-10-30: development of Yantra started (first build, specification
  v0.1).
- 2026-11-03: the radio plugins moved to their own project, yantra-radios
  (released as v0.1.0 on 2026-11-04).
- 2026-11-04: this repository started for specification v0.2, on top of
  yantra-radios v0.1.0.
- 2026-11-09: release 0.1.0 (specification v0.2).
- 2026-11-12: release 0.2.0 (specification v0.3): Windows x64, clang,
  the PDF user manual.
- 2026-11-17: specification v0.4: a simpler indoor workflow (Walk and
  Map tabs, the IDW coverage map, fixed demo scenes; walls, antennas and
  the ray tracer removed); specification v0.5 on 2026-11-18.
- 2026-11-20: yantra-radios merged into this repository with its
  history (its releases tagged radios-v0.1.0 .. radios-v0.4.0;
  specification v0.6, PLG-29), then one plugin-first tree and one build
  (`radio-api/`, `plugins/`, `radio-common/`, `gen/`).
- 2026-11-24: release 0.3.0 (specifications v0.4-v0.6): one version for
  the application, the plugins and the radio parts; radio API 1.2.
- 2026-11-25: release 0.3.1: CI on GitHub's hosted runners and GitHub
  releases (.deb, Windows setup .exe, PDF manual); no functional change.
