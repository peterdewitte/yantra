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
| `common/` | shared SoapySDR code |
| `receiver/` | DSP, carrier search, TETRA decoder, level and quality meters; `receiver/rust/` the protocol crate |
| `gen/` | TETRA test-signal generator library |
| `plugins/` | one folder per plugin: hackrf, simulator, soapy, pei, example |
| `tools/` | `yantra-gen`, `yantra-radio-host`, `yantra-hackrf`; `tools/deps/` static dependency builds |
| `third_party/` | pinned sources of every bundled library, incl. osmo-tetra |
| `tests/`, `docs/` | tests and documentation |

## Status

Work in progress. The project started on 2026-11-03; build instructions
follow with the CMake project. Linux is built and tested first; the
Windows build (MSYS2 UCRT64) is planned.

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
