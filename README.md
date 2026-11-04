<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

<p align="center">
  <img src="data/logo/png/yantra_logo_128.png" alt="Yantra logo" width="128" height="128">
</p>

# Yantra

An open-source tool for measuring and reporting TETRA coverage inside
buildings, floor by floor, built on a software-defined radio (SDR).

<p>
  <a href="https://curtat.be">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="data/branding/curtat_logo_dark_64.png">
      <img src="data/branding/curtat_logo_light_64.png" alt="curtat.be" width="64" height="64">
    </picture>
  </a>
</p>

Yantra is made by [curtat.be](https://curtat.be) as a reference on building
new programs with Claude, Anthropic's AI assistant.

> **Status:** early development. This repository was started on
> 2026-11-04; nothing is usable yet. The sections below describe the goal
> and are filled in as features land.

## What Yantra is

Yantra is an indoor tool: walking a building floor by floor with an SDR,
measuring the TETRA downlink on imported floor plans and producing
coverage and acceptance reports, for example for DAS and indoor-coverage
projects. It reads the broadcast and system information of the cells it
hears and reports levels and signal quality. There are no outdoor drive
tests, no GPS and no geographic maps.

Radios are plugins from the separate yantra-radios project, included as a
git submodule in `third_party/yantra-radios`: each radio plugin processes
its own IQ samples and gives Yantra measurement records only. Feature
plugins (for example the remote probe API and terminal mode) use Yantra's
public plugin API (`include/yantra/plugin.h`). How to write and test a
plugin: [docs/PLUGINS.md](docs/PLUGINS.md).

Out of scope, by design: short data messages, subscriber addresses, voice
and decryption. Yantra only handles broadcast and system information,
levels and quality.

## Supported radios

Planned through yantra-radios: HackRF (own plugin), any SoapySDR radio
through the generic SoapySDR plugin (RTL-SDR, Airspy, network radios), a
TETRA terminal over its PEI, and a simulator that needs no hardware. The
tested hardware is listed in yantra-radios' `DEVICES.md`.

## Install

Linux: build from source. The radio plugins come from the yantra-radios
repository as a git submodule (`third_party/yantra-radios`, pinned to a
tagged release). Its submodule URL is relative (`../yantra-radios`), so
yantra-radios is cloned next to yantra:

    git clone <yantra-radios repository> yantra-radios
    git clone <yantra repository> yantra
    cd yantra
    git submodule update --init --recursive
    cmake --preset default
    ninja -C build
    ctest --test-dir build -j"$(nproc)"

[BUILD.md](BUILD.md) has the build options, the radio plugin build (or a
prebuilt yantra-radios package) and the plugin folders.

Packages (.deb, later AppImage) are planned. Windows packages are planned
but not yet built.

## Quick start

Follows once the application runs.

## Screenshots

Follow once the main screens exist; they are generated automatically from
demo data.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Licence and credits

Yantra is free software under the GNU General Public License, version 3 or
later (`GPL-3.0-or-later`, see [LICENSE](LICENSE)). Every file carries an
SPDX header or a `REUSE.toml` entry; the project is checked with
[reuse](https://reuse.software). Third-party libraries, tools, fonts and
data are listed with their licences in [THIRD_PARTY.md](THIRD_PARTY.md).

The Yantra logo in `data/logo/` is part of Yantra (GPL-3.0-or-later). The
curtat.be logo in `data/branding/` is a trademark of Curtat BV and is not
covered by the GPL; forks must remove or replace it.

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
