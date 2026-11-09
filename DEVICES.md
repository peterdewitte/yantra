<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Devices

Which radios the plugins have been checked with, and how (RAD-95,
RAD-125). One row per radio and set-up: the radio, the SoapySDR module (or
other interface) and its version, the operating system, the functions
tested with their results, the measured calibration offset and the known
issues. Hardware-in-the-loop checks are manual and run before a release.

## Status of yantra-radios 0.2.7

**No radio has been measured on real hardware yet.** The project has had
no radio or signal generator so far; the rows below with real hardware
are open. Every plugin is tested in CI against its fake device: the fake
SoapySDR module (RAD-92), the simulated PEI terminal and the simulator
(rows under "Test devices").

## How to add a row

- HackRF One: `yantra-hackrf report --out DEVICES.md` runs the self-test,
  the noise floor per gain, the throughput per sample rate and a guided
  unplug/replug, and appends one row; `yantra-hackrf level` with a
  calibrated signal generator gives the calibration offset and the
  linearity (RAD-125; Yantra NFR-03 asks for +/-2 dB after calibration).
  See [docs/YANTRA_HACKRF.md](docs/YANTRA_HACKRF.md).
- Other radios: `yantra-radio-host selftest`, `conformance` (with
  `--unplug`/`--replug` commands or by hand) and `stream` against a known
  cell (records into a record file); report the same columns.
- Never put a serial number, a location or a person's name in a row.

## Real hardware (open)

| Radio | Plugin, module and version | OS | Tested functions and results | Calibration offset | Known issues | Date |
|---|---|---|---|---|---|---|
| HackRF One | `hackrf.so`, SoapyHackRF 0.3.6, libhackrf 2026.01.3 | Linux x86-64 | not yet measured on hardware: selftest, noise, throughput, replug (`yantra-hackrf report`), level linearity (`yantra-hackrf level`) | not yet measured | | |
| RTL-SDR (incl. RTL-SDR Blog V4) | `soapysdr.so`, SoapyRTLSDR 0.3.4, librtlsdr 2.0.3 (osmocom) | Linux x86-64 | not yet measured on hardware | not yet measured | needs the DVB-T driver blacklist (`udev/yantra-rtl-sdr-blacklist.conf`) | |
| Airspy (R2, Mini) | `soapysdr.so`, SoapyAirspy 0.2.1, libairspy 1.0.10 | Linux x86-64 | not yet measured on hardware | not yet measured | | |
| Network radio (SoapySDRServer) | `soapysdr.so`, SoapyRemote 0.5.3 (`remote:<serial>`, servers in `YANTRA_SOAPY_REMOTE`) | Linux x86-64 | not yet measured with a real server and radio | per radio behind the server | no network discovery (by design) | |
| TETRA terminal over PEI | `pei.so`, AT commands of EN 300 392-5 | Linux x86-64 | not yet checked with a real terminal | the terminal's own dBm | cell-list commands are vendor-specific: a profile per terminal is still needed | |

## Test devices (CI, every change)

Filled from the test runs of 2026-11-09 (the 0.2.7 release: plugins
hackrf 0.1.2, soapysdr 0.1.2, pei 0.1.1, simulator 0.2.4; Linux 6.18
x86-64, gcc 15): `ctest` default tree 100 % passed, and the conformance
kit report `build/conformance/<plugin>.json` "pass" (21 checks each).

| Radio | Plugin, module and version | OS | Tested functions and results | Calibration offset | Known issues | Date |
|---|---|---|---|---|---|---|
| HackRF One (firmware 2024.02.1), fake | `hackrf_fake.so` (HackRF plugin code), SoapyHackRF fake module 0.3.6-fake (yantra-gen IQ) | Linux 6.18 x86-64 | `yantra-hackrf report --rates 2,3`: selftest pass; noise -122.9..-82.9 dBFS pass; throughput up to 3 Msps pass; replug pass. Conformance kit with unplug/replug pass; `test_hackrf` (records against the generator's truth within 1.5 dB, presets, scans, C1, overflow, hot-plug) pass | fake: `yantra-hackrf level` -100..-60 dBm gives dBFS = dBm - 33.95 dB, linearity error 0.00 dB | the paced fake generates IQ in real time only up to about 3 Msps on this machine; higher rates are a limit of the fake, not of the plugin | 2026-11-09 |
| Generic SoapySDR device, fake | `soapysdr_fake.so` (generic plugin code), fake module `yantrafake` (HackRF One profile) | Linux 6.18 x86-64 | conformance kit with unplug/replug pass; `test_soapysdr` (generated settings, records against truth, gain-following levels, overflow and drop events, hot-plug, refused open) pass | not applicable | | 2026-11-09 |
| Network radio, loopback | `soapysdr.so` build with SoapyRemote 0.5.3 client, test server on 127.0.0.1 with the fake module | Linux 6.18 x86-64 | `conformance_soapysdr_remote` pass; `test_soapy_remote` (listing with the module version, records over the network against truth, nothing listed without `YANTRA_SOAPY_REMOTE`) pass | not applicable | | 2026-11-09 |
| Simulated TETRA terminal | `pei.so`, `yantra-pei-sim` (tests/fixtures/pei/terminal_cells.json) | Linux 6.18 x86-64 | conformance kit with unplug/replug pass; selftest 7/7 pass; `test_pei` (records, profiles, refused commands, timeouts, replug) pass | not applicable (dBm from the terminal) | | 2026-11-09 |
| Simulator | `simulator.so` | Linux 6.18 x86-64 | conformance kit pass, also with a scene 1.1 (`conformance_simulator_scene_v1_1`); selftest pass (context warns without a floor plan); `test_simulator` incl. real-time pacing within 1 % | true offset -20 dB at the defaults (sensor `true_offset_db`); practice calibration recovers it | | 2026-11-09 |
| Clean environment | the installed plugins in a bubblewrap sandbox without SoapySDR or vendor packages | Linux 6.18 x86-64 | `clean_env_conformance` (RAD-133) pass | | | 2026-11-09 |
