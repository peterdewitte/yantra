<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# yantra-hackrf: the HackRF One test tool

`yantra-hackrf` (RAD-66) tests a HackRF One through the HackRF plugin
(RAD-60) and the radio API, so it checks exactly what Yantra uses. Every
command except `capture` loads the plugin; `capture` records IQ through
the same built-in SoapySDR modules while the plugin has the device
closed. The tool never transmits: the HackRF transmit path is not used.

## Running it

    yantra-hackrf COMMAND [options]

The plugin is found next to the tool (`bin/plugins/hackrf.so` in the
build tree) or in the install layout (`lib/yantra-radios/plugins`);
`--plugin FILE` or `YANTRA_HACKRF_PLUGIN` names another. `--device KEY`
(`hackrf:<serial>`, or the serial) picks one of several HackRFs.

Common options: `--set KEY=VALUE` (repeatable) and `--preset ID` before
the command, `--json` for JSON output, `--no-prompt` for scripts,
`--duration S` for the stream length of a command. Frequencies and rates
take `k`, `M` and `G` suffixes (`392.5M`, `2M`).

Exit codes: 0 pass, 1 fail, 2 no device, 3 usage error.

Antenna port power (`antenna_power=true`) puts 3.3 V on the antenna port
for a preamplifier; it can damage a DC-coupled input or a short. The
tool refuses it without `--bias-tee`. The plugin switches it off at every
open and replug.

## Commands

| Command | What it does | Pass when |
|---|---|---|
| `list` | HackRFs with serial, board, firmware, USB speed | always (2 without a HackRF) |
| `info` | plugin, device information, all sensors and the bundled libraries with versions and licences (RAD-136) | the device opens |
| `selftest` | the plugin self-test (RAD-46) with remedies | no item fails (warnings pass) |
| `settings [list]` | every setting with value, warnings and why a setting is unavailable | |
| `settings get KEY...` | values | the keys exist |
| `settings set KEY=VALUE...` | writes and shows the read-back (RAD-41) | every write succeeds |
| `settings preset [ID]` | lists the presets, or applies one and shows the gains read back | |
| `measure` | records for `--plan serving`, `carriers` (`--carrier F`...) or `scan` (`--first F --last F [--step 25k]`), to the terminal, as JSON or with `--out FILE` as a record file (RAD-65); `--records N` | records arrive, none invalid |
| `capture` | IQ to SigMF (`--out FILE.sigmf-data`, `--rate`, `--centre`, `--lna`, `--vga`, `--amp`, `--format cs16/cs8/cf32`) for `--duration` (default 10 s) | no samples lost |
| `noise` | noise floor (dBFS) per gain point `--gains LNA/VGA,...` with the antenna port terminated (50 ohm) | every point measured |
| `level` | level linearity against a signal generator at `--frequency F`: for each level of `--levels DBM,...` it asks you to set the generator and press Enter, or runs `--step-cmd CMD` (`{dbm}` is replaced by the level); reports dBFS against dBm, the offset for `c1_level_offset_db` and the linearity error (RAD-125) | worst error within `--tolerance` (1 dB) |
| `ppm` | frequency error of a known TETRA carrier `--frequency F` (from the records' `freq_error_ppm`) | within `--max-ppm` (30) |
| `throughput` | each sample rate (`--rates MSPS,...`, default all) for `--duration` (5 s): overflows, samples lost, receiver load (RAD-24) | no overflow, no loss |
| `replug` | guided unplug and replug (RAD-35): waits for the plugin to report the device lost and back, checks antenna port power is off, streams again; `--unplug-cmd`/`--replug-cmd` run commands instead of asking, `--timeout S` (60) | lost, back, records again |
| `report` | selftest, noise, throughput and replug, then one DEVICES.md row (RAD-95), printed and appended to `--out FILE` | all four pass |

The DEVICES.md row has the board and firmware, the SoapyHackRF version,
the OS, what was tested with results, the calibration offset (measured
with `level`, by hand) and known issues; it holds no serial number.

## Examples

    yantra-hackrf list
    yantra-hackrf settings set gain_lna=24 gain_vga=20
    yantra-hackrf measure --plan scan --first 390M --last 395M --duration 30
    yantra-hackrf capture --out site.sigmf-data --centre 392.5M --duration 20
    yantra-hackrf level --frequency 392.5M --levels -100,-90,-80,-70,-60
    yantra-hackrf report --out DEVICES.md

## Tests

`tests/yantra_hackrf` runs every command in-process against the HackRF
plugin's test build `hackrf_fake` and the fake module (RAD-92): exit
codes, JSON, the `--bias-tee` refusal, presets, record files, SigMF,
noise per gain, level steps through the fake's `<serial>.level_offset`
control file, ppm, throughput with and without an overflow, replug
through the control files and the report row.
