<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

![Yantra logo](docs/img/yantra_logo_128.png)

# Yantra

An open-source tool for measuring and reporting TETRA coverage inside
buildings, floor by floor, with a software-defined radio (SDR).

Yantra is made by [curtat.be](https://curtat.be) as a reference on building
new programs with Claude, Anthropic's AI assistant.

## What Yantra is

Yantra is an indoor tool, for example for DAS and indoor-coverage
projects:

- import floor plans (PDF or image), cut several floors out of one sheet,
  scale them and line them up;
- draw walls (or accept walls Yantra proposes from the plan) and antennas;
- walk the floors with a radio and click your position on the plan (or
  practise with the mouse and the simulated radio);
- watch the cells live: levels, C1, quality, the decoded broadcast
  information and the neighbour lists;
- check the network against its cell database;
- get a coverage map fitted to your measurements by a ray tracer, with a
  confidence mask where nobody measured;
- write a coverage and acceptance report (PDF and HTML) per floor and for
  the building, against acceptance profiles on the measured points, the
  tested area or the floor area.

Yantra reads only broadcast and system information, levels and quality.
Short data messages, subscriber addresses, voice and decryption are out of
scope by design, and there are no outdoor drive tests, GPS or maps. See
the [legal and scope note](docs/LEGAL.md).

A sample report of the demo project, as Yantra writes it:
[PDF](docs/samples/fraunces_report.pdf) and
[HTML](docs/samples/fraunces_report.html).

## Supported radios

Radios are plugins from the separate yantra-radios project, included as a
git submodule in `third_party/yantra-radios`. The shipped radio plugins:

- **Simulator**: a simulated TETRA network and devices, no hardware
  needed; it drives the demo and the tour.
- **HackRF**: HackRF One.
- **SoapySDR**: any SoapySDR radio, for example RTL-SDR and Airspy, and
  network radios through SoapyRemote.
- **TETRA terminal (PEI)**: a terminal's own measurements over its serial
  port (terminal mode).

Hardware status: none of these radios has been measured with Yantra on
real hardware yet; they are tested with simulated and fake devices. The
hardware tested so far is listed in yantra-radios' `DEVICES.md`.

Yantra's own plugins: the TETRA technology plugin and the probe plugin
(the remote API of the headless probe).

## Install

**Linux**: build from source; `ninja -C build deb` then makes a .deb
package of the build ([BUILD.md](BUILD.md#linux-package-nfr-09)). The radio plugins
come from the yantra-radios repository as a git submodule with a relative
URL (`../yantra-radios`), so yantra-radios is cloned next to yantra:

    git clone <yantra-radios repository> yantra-radios
    git clone https://github.com/peterdewitte/yantra.git yantra
    cd yantra
    git submodule update --init --recursive
    cmake --preset default
    ninja -C build
    ctest --test-dir build -j"$(nproc)"

[BUILD.md](BUILD.md) has the dependencies, the build options, the radio
plugin build (or a prebuilt yantra-radios package), the USB permissions
for the radios and the plugin folders.

**Windows**: not yet available.

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
  in the app (F1 and the Help buttons).
- [Quick starts](docs/quickstart/README.md): one page each.
- [Measurement method note](docs/METHOD.md): what is measured and how,
  calibration and its uncertainty, the ray-tracing model and its limits.
- [Legal and scope note](docs/LEGAL.md): receive-only, what is decoded,
  privacy and licences.
- For developers: [architecture](ARCHITECTURE.md),
  [building](BUILD.md), [contributing](CONTRIBUTING.md),
  [plugins](docs/PLUGINS.md),
  [the building model](docs/BUILDING_MODEL.md),
  [the measurement pipeline](docs/MEASUREMENT_PIPELINE.md),
  [calibration](docs/CALIBRATION.md),
  [the cell database](docs/CELL_DATABASE.md),
  [propagation](docs/PROPAGATION.md), [reports](docs/REPORT.md),
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
