<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Level calibration and antenna correction

Most radios measure levels in dBFS (relative to their own full scale).
Yantra turns these into dBm at the antenna connector with a calibration
table (RF-02, HW-01b), and then adds the antenna correction (HW-09):

    level_dBm = level_dBFS + offset_db(f) + correction_db
    correction_db = -antenna_gain_dbi + cable_loss_db + attenuator_db

A SELF_CALIBRATED radio (a TETRA terminal, a measurement receiver) already
gives dBm. It gets the antenna correction only. It never uses the table
and its levels are never flagged uncalibrated. A dBFS level without a
usable table entry keeps its dBFS value, still gets the antenna correction
and is flagged UNCALIBRATED.

Code: `core/include/yantra/core/calibration/` (`key.h`, `table.h`,
`antenna.h`, `wizard.h`, `corrector.h`). The pipeline stage that uses it
is stage 3 of [MEASUREMENT_PIPELINE.md](MEASUREMENT_PIPELINE.md).
Tests: `tests/calibration/`, the calibration cases in
`tests/measure/test_pipeline.cpp`, `tests/radio_host/` (the key per
record) and `tests/measure_host/` (end to end with the fake SDR plugin and
the simulator).

## Calibration key (HW-01b)

An entry holds for one combination:

- the device's **driver** and **serial number**, from the radio plugin's
  `device_info`;
- the values of every setting the radio plugin marks
  `YR_SETTING_CALIBRATION`, for example LNA, VGA and amplifier gain (action
  settings are not values and are left out).

As text: `driver|serial|name=value;name=value` with the settings sorted by
name. Values are written as text: `true`/`false`, integers in decimal,
doubles in their shortest exact form (`24`, `0.5`) and enums by their
number.

The radio host works out the key when it opens or reopens a device, after
a setting is set, and when the plugin reports `settings_changed`. It keeps
the last few keys with the time they became valid, so each record gets the
key that was valid when it was **measured**, not when it arrived. A record
that was measured before a gain change and arrives after it still gets the
old key. `RadioDevice::calibration_key()` gives the current key.

## Lookup

For the record's key and carrier frequency:

- between two points of the entry: linear interpolation in dB;
- up to 5 MHz outside the outermost points: the nearest point's offset,
  marked *extrapolated*;
- further out, or no entry for the key: no calibration (uncalibrated);
- an entry past its validity (`valid_days` after `created`) is still used,
  but marked *expired* (the calibration window asks for a new one);
- an entry from the simulator's practice generator (`simulated`) applies
  only to a SIMULATED radio, never to a real one.

What was applied goes with every record (`LevelCorrection`): `source`
(`table`, `radio` or empty), the key, the date of the entry, and the
expired and extrapolated marks.

## File format

The table is UTF-8 JSON, `format` `yantra-calibration`, `version` 1. Yantra
refuses a file of another format, a file without a version, and any
version other than 1. For a newer version the message says to update
Yantra. Within version 1, unknown members are ignored so that older
readers keep working. Saving is atomic (a temporary file, then a rename).

```json
{
  "format": "yantra-calibration",
  "version": 1,
  "entries": [
    {
      "driver": "hackrf",
      "serial": "0000000000000000a1b2c3d4e5f60001",
      "settings": {"gain_amp": "0", "gain_lna": "24", "gain_vga": "20"},
      "radio_plugin": "org.yantra.radios.soapysdr",
      "created": "2026-11-04T10:00:00Z",
      "valid_days": 365,
      "method": "generator",
      "ppm": 1.5,
      "simulated": false,
      "points": [
        {"frequency_hz": 390000000, "offset_db": -12.125,
         "reference_dbm": -60.0, "measured_dbfs": -47.875, "spread_db": 0.25},
        {"frequency_hz": 395000000, "offset_db": -11.5}
      ]
    }
  ]
}
```

| Member | Meaning |
|--------|---------|
| `driver`, `serial` | The device (required: `driver`). |
| `settings` | The CALIBRATION settings as text (string values only). |
| `radio_plugin` | The radio plugin it was made with (information). |
| `created` | UTC, `YYYY-MM-DDTHH:MM:SSZ` (required). |
| `valid_days` | Validity, 1 day or more (default 365). |
| `method` | `generator` (signal generator), `reference_radio` or `practice` (the simulator's generator). |
| `ppm` | The frequency correction found in the same session, if any. |
| `simulated` | Made with a simulated radio. |
| `points` | At least one point. `frequency_hz` > 0 and `offset_db` are required; `reference_dbm` (generator level), `measured_dbfs` and `spread_db` (offsets over the levels measured, for linearity) are information only. |

Points are sorted by frequency. When a frequency appears twice, the later
point wins. When a table is imported, entries for new keys are added; for
a key that is already there, the newer entry (`created`) wins. Export
writes the same format.

## Where the table is kept

There is one file per user (the spec names no place, so this is a build
decision):

- Linux: `$XDG_DATA_HOME/yantra/calibration.json`, by default
  `~/.local/share/yantra/calibration.json`;
- Windows: `%LOCALAPPDATA%\Yantra\calibration.json`.

A missing file is an empty table. A file that cannot be read or is refused
is reported and left alone.

## Stored with the data (RF-02)

`TableCorrector::provenance()` gives the object stored with a recording or
project for each device: the calibration status (`valid`, `expired` or
`uncalibrated`), the key, driver, serial and settings, the entry's date
and expiry date, the antenna correction in use, and a calibration document
in the format above with that one entry (`null` if there is none). This
way a recording can be checked later against the calibration it was made
with.

## Making an entry (wizard)

`Wizard` steps through a plan of frequencies and generator levels. The
user sets the generator; Yantra averages the radio's readings
(`LevelAverager`: the first readings are skipped while the level settles,
and NO_SYNC and OVERLOAD readings are left out). A reading is refused if
it is too close to full scale (`max_dbfs`, -6 dBFS), too weak above the
noise (`min_snr_db`, 20 dB) or overloaded. For each point,
`offset = level_dBm - measured_dBFS`, averaged over the levels at that
frequency, with the spread kept as `spread_db`. `verify_error_db` checks an
existing entry against a known level, and `ppm_from_carrier` gives the
frequency correction from a known carrier.

To practise, use the simulator's signal generator (RAD-61):
`calibration_generator` on, `calibration_level_dbm` as the known level.
Use a plan with method `practice` and `simulated` set. The offset found
matches the simulator's `true_offset_db` sensor. A practice entry is
marked simulated and never applies to a real radio.

## Antenna correction (HW-09)

`AntennaCorrection` holds `antenna_gain_dbi` (-30..40), `cable_loss_db`
(0..60) and `attenuator_db` (0..100). `AntennaSettings` has one default
for all devices plus overrides per (radio plugin, device). The correction
is applied once to every LEVEL reading of the radio, whether dBFS or dBm.
It is never applied to broadcast thresholds (for example a cell's minimum
receive level or maximum transmit power): those are SYSTEM_INFO values,
not readings. The plugin host refuses a technology schema that declares a
broadcast or neighbour-list field as LEVEL. Only DOUBLE fields in the unit
of the radio's own level reading are corrected.
