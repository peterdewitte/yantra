<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# yantra-radios

The radio plugins of **Yantra**, an open-source indoor TETRA coverage
survey tool, together with everything they need: the radio API, the
receiver (DSP, carrier search, TETRA decoder, level and quality meters),
a TETRA test-signal generator, a radio test host and the tests.

A radio plugin talks to its own device (an SDR through SoapySDR or a
direct driver, a terminal over a serial port, or a simulator), processes
its own IQ samples and gives Yantra **measurement records only**: level,
cell identity, optional quality, C1, broadcast parameters and neighbours.
No IQ samples, bursts or PDUs cross the radio API.

Yantra uses this repository as a git submodule pinned to a commit. The
dependency goes one way: Yantra depends on yantra-radios, never the
reverse. yantra-radios builds, tests and runs its plugins without Yantra,
through its own test host.

## Scope

The plugins are **receive-only**. They decode what every TETRA base
station broadcasts in the clear (synchronisation, system information,
neighbour lists) and measure levels and quality. They do not decode
speech, short data or subscriber identities, and never decrypt anything.

## Layout (planned, RAD-02)

| Folder | Contents |
|--------|----------|
| `include/yantra/radio.h` | the radio API, the only interface between Yantra and a radio plugin |
| `common/` | shared code: record files (RAD-65), scene JSON (RAD-36), later the SoapySDR layer |
| `receiver/` | DSP, carrier search, TETRA decoder, level and quality meters; `receiver/rust/` the protocol crate |
| `gen/` | TETRA test-signal generator library |
| `plugins/` | one folder per plugin: hackrf, simulator, soapy, pei, and `beacon`, the example |
| `tools/` | `yantra-gen`, `yantra-radio-host`, `yantra-hackrf`; `tools/deps/` static dependency builds |
| `third_party/` | pinned sources of every bundled library, incl. osmo-tetra |
| `tests/`, `docs/` | tests and documentation |

## Status

Work in progress. The project started on 2026-11-03. Linux is built and
tested first; the Windows build (MSYS2 UCRT64) is planned.

## Build (Linux)

Needs CMake >= 3.20, Ninja, gcc or clang with C++20 and Python 3.

    git submodule update --init
    cmake -S . -B build -G Ninja
    ninja -C build
    ctest --test-dir build -j"$(nproc)"

Presets (CMakePresets.json): `ci` (warnings as errors), `scalar` (no SIMD,
same results), `asan-ubsan`, `tsan`. `tools/ci/run_linux.sh` runs the same
steps as the CI on Linux, including the completeness check of the plugins
(`tools/ci/check_completeness.py`).

## Radio test host

`build/bin/yantra-radio-host` loads a radio plugin without Yantra
(RAD-91): `info`, `devices`, `settings`, `selftest`, `stream` (records into
a record file, docs/RECORD_FILES.md) and `read`. For plugins that use the
floor plan and scene it emulates Yantra's context callbacks (`--scene`,
`--cursor`, `--walk`). `yantra-radio-host --help` lists every option.

`yantra-radio-host conformance --plugin FILE` runs the conformance kit:
every check of the radio API rules it can, with a JSON report
(`--report`). Every plugin built here has a `conformance_<name>` ctest.

## TETRA test-signal generator

`build/bin/yantra-gen` (RAD-71) writes simulated TETRA downlink I/Q
(cf32, cs16, cs8, cu8) with SigMF metadata, a ground-truth CSV and a
ground-truth record file from a scenario in `data/scenarios` or from
cells on the command line, e.g.
`yantra-gen --scenario failed_site --format cs8 --out failed_site.cs8`.
`yantra-gen --help` lists every option; docs/YANTRA_GEN.md describes the
outputs. `cmake --install build` installs it with the scenarios.

## Documentation

- [docs/RADIO_PLUGINS.md](docs/RADIO_PLUGINS.md): writing a radio plugin,
  a tutorial with the example plugin `plugins/beacon` (RAD-64), settings
  as data, the record schemas and the conformance kit.
- The radio API reference: `ninja -C build docs-radio-api` (Doxygen) into
  `build/docs/radio-api/html`.
- [docs/YANTRA_GEN.md](docs/YANTRA_GEN.md) the yantra-gen command and its
  output files.
- [docs/RECORD_FILES.md](docs/RECORD_FILES.md) record files,
  [docs/SCENE_FORMAT.md](docs/SCENE_FORMAT.md) scene JSON,
  [docs/SCENARIO_FORMAT.md](docs/SCENARIO_FORMAT.md) scenario files,
  [docs/CHANNEL_MODEL.md](docs/CHANNEL_MODEL.md) channel and multi-wall
  model.
- [docs/TETRA_RECORDS.md](docs/TETRA_RECORDS.md) the TETRA record schemas
  shared by the measurement engine, the plugins and yantra-gen.
- [CHANGELOG.md](CHANGELOG.md): releases and the radio API changelog
  (RAD-11).

## Licence

GPL-3.0-or-later, see [LICENSE](LICENSE). The TETRA plugins link
osmo-tetra (AGPL-3.0); GPL-3.0 section 13 allows the combination, and
those parts keep their AGPL terms. Third-party components are listed in
[THIRD_PARTY.md](THIRD_PARTY.md). How to contribute:
[CONTRIBUTING.md](CONTRIBUTING.md).

## Made by curtat.be

Yantra and yantra-radios are made by [curtat.be](https://curtat.be) as a
reference on building new programs with Claude, Anthropic's AI assistant.
Homepage: https://curtat.be.

## Source

Source code: https://github.com/peterdewitte/yantra-radio-plugins
(clone URL `git@github.com:peterdewitte/yantra-radio-plugins.git`). Every
release publishes the complete corresponding source there (RAD-137).
