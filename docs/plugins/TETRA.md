<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TETRA technology plugin

`plugins/tetra` builds `tetra.so`, the technology plugin (PLG-02) for the
TETRA radio plugins of the radio parts (technology id `tetra`). It is
installed into the application's plugin folder `lib/yantra/plugins`
(PLG-09) and is the only place in Yantra with TETRA knowledge: the core
uses it through the plugin API (PLG-01, [PLUGIN_API.md](../PLUGIN_API.md)).
The meaning and source of every radio field is in the radio parts'
`docs/TETRA_RECORDS.md`; the standard is ETSI EN 300 392-2.

| File | What |
|------|------|
| `tetra_technology.cpp` | Descriptor, entry point, the plugin API functions |
| `tetra_schemas.inc` | The four record schemas, field by field |
| `tetra_raster.h/.cpp` | Carrier numbers, offsets, duplex spacing (library `yantra::tetra_rules`) |
| `tetra_rules.h/.cpp` | C1/C2, reselection, verification grids (same library) |
| `tetra_translations.inc` | Dutch, French and German labels (UI-12) |
| `testdata/` | Record files and the golden output of the conformance test |

## Schemas (PLG-05)

The schemas are those of the radio plugins with the same id, field by
field: name, type, unit, enum items, label and help text. The plugin adds
the data category of every field (PLG-08), OPTIONAL for every field a
radio may leave out, and VERIFY for the broadcast parameters compared with
the cell database (DB-04).

| Schema | Kind | Radios | Key |
|--------|------|--------|-----|
| `tetra_cell` | measurement, the cell schema | simulator, hackrf, soapysdr | `frequency_hz`, `mcc`, `mnc`, `la` |
| `tetra_neighbour` | neighbour list entry, the neighbour schema | simulator, hackrf, soapysdr | `frequency_hz`, `entry_index` |
| `tetra_scan` | band scan channel | hackrf | `frequency_hz`, `mcc`, `mnc`, `la` |
| `tetra_terminal_cell` | cell as a terminal reports it | pei | `frequency_hz`, `mcc`, `mnc`, `la` |

- `level` is a LEVEL in dBm (the radios deliver dBFS or dBm; Yantra
  calibrates, RF-02). `c1_db` is a LEVEL in dB.
- The quality group (MER, CRC error rates, BER, SNR, EVM, frequency error)
  is QUALITY; the simulator's plan position is PLAN_POSITION; everything
  else, the broadcast thresholds included, is SYSTEM_INFO and is never
  corrected (HW-09).
- A cell (DB-01) is the main carrier of a location area of a network. The
  colour code is not part of the key, since no neighbour list carries it;
  it is a VERIFY field.
- Neighbour links (DB-05, plugin API 1.2 link rule, PLG-05):
  `neighbour_frequency_hz` names the cell's `frequency_hz`, and
  `neighbour_la`, `neighbour_mcc` and `neighbour_mnc` its `la`, `mcc` and
  `mnc` when the entry carries them (D-NWRK-BROADCAST sends them when it
  chooses to, e.g. for a neighbour of another network, the radio parts
  RAD-22); a field the entry lacks is the broadcasting cell's and is kept
  as inherited (it ranks a match but never refuses one).
- Every schema is checked against every shipped radio plugin by
  `test_tetra` (`check_radio_schema`); a radio schema change in
  the radio parts' tests fail it until this table follows.

## Channel raster (PLG-02)

`downlink = band x 100 MHz + carrier x 25 kHz + offset`, offsets 0,
+6.25, -6.25 and +12.5 kHz (EN 300 392-2 21.4.4.1); the uplink is the
downlink minus the duplex spacing (plus with reverse operation) of the
TS 100 392-15 table, the same table the radio receiver decodes with.

The bands cover the whole tuning range of the TETRA radios (the radio parts
RAD-110: 350-470 MHz and 790-920 MHz for 800 MHz TETRA), cut where the
band code changes, so every record a radio can deliver maps onto a band.
The TS 100 392-15 allocations (380-400 MHz public safety, 410-430,
450-470, 806-870, 870-921 MHz) lie inside them.

| Band | Downlink | Band code | Carriers | Channel numbers |
|------|----------|-----------|----------|-----------------|
| `tetra_350` | 350.000-399.975 MHz | 3 | 2000-3999 | 32000-33999 |
| `tetra_400` | 400.000-469.975 MHz | 4 | 0-2799 | 40000-42799 |
| `tetra_790` | 790.000-799.975 MHz | 7 | 3600-3999 | 73600-73999 |
| `tetra_800` | 800.000-899.975 MHz | 8 | 0-3999 | 80000-83999 |
| `tetra_900` | 900.000-919.975 MHz | 9 | 0-799 | 90000-90799 |

Carrier numbers repeat between band codes (carrier 2000 is 350 MHz in
band 3 and 450 MHz in band 4), so the plugin's channel number is
band-qualified: band code x 10000 + carrier number (33600 = band 3,
carrier 3600 = 390 MHz). `frequency_to_channel` accepts every offset;
`channel_to_frequency` gives the offset-0 frequency. The radio records'
`channel` field stays the bare carrier number of MAC-SYSINFO, and the
identity of a carrier is its frequency (`frequency_hz`, a cell KEY
field). `tetra_raster.h` also gives `band_of` and `channel_label`, the
text for the user ("3600 (band 3)", "3603 (band 3, -6.25 kHz)").

## C1 (RF-06)

`C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS power)`
(EN 300 392-2 clause 23), in dBm and dB, with the operations of
the radio parts' `tetra_c1_db`, so a radio's C1 and Yantra's are the same
number for the same inputs (tested). The MS power is the class power
setting, `PipelineConfig::ms_power_dbm`: 30 dBm portable (default),
35 dBm mobile. `compute_c1` answers UNSUPPORTED for a record without
`rxlev_access_min_dbm` and `ms_txpwr_max_cell_dbm`.

The pipeline keeps a radio's C1 and asks the plugin only for records
without it whose level is calibrated; such records carry the
`c1_estimated` flag ([MEASUREMENT_PIPELINE.md](../MEASUREMENT_PIPELINE.md)).

## Cell reselection (DB-08)

`reselect` takes the serving cell and its neighbours (records of
`tetra_cell` with a level), computes C1 and the neighbours' C2 with the
same formula and checks, in this order (thresholds SRT, SRH, FRT, FRH of
the serving cell's D-NWRK-BROADCAST, dB):

| State | Condition | Reselection |
|-------|-----------|-------------|
| `radio_link_failure` | C1 < 0 | to the best neighbour with C2 > 0 |
| `exclusive` | no neighbour with C2 > 0 | none |
| `superior` | C1 >= every C2 | none |
| `relinquishable` | C1 < FRT | fast, when the best C2 > C1 + FRH |
| `improvable` | C1 < SRT | slow, when the best C2 > C1 + SRH |
| `usable` | otherwise | none |

It answers UNSUPPORTED without the serving cell's access parameters, or
when a neighbour is better and the thresholds are unknown. The time
conditions of the standard (5 s) are not simulated.

## Verification (DB-04)

`values_match` compares exactly, except the dBm parameters MAC-SYSINFO
sends as codes: RXLEV_ACCESS_MIN (5 dB steps from -125 dBm),
MS_TXPWR_MAX_CELL (5 dB steps from 15 dBm) and the access parameter
(2 dB steps from -53 dBm) match when the stored value stands for the
received code (nearest code, ties to the higher one).

## Tools window (DB-07)

The plugin adds Tools > TETRA calculator (PLG-06, window
`plg.yantra.tetra.calculator`, `tetra_calculator.cpp`), drawn by Yantra
through `yantra_ui_api`, so the TETRA arithmetic stays here:

- Channel to frequency: frequency band code (1..15), carrier number
  (0..4095), offset (0, +6.25, -6.25, +12.5 kHz), duplex spacing code
  (0..7) and reverse operation give the downlink, the duplex spacing of
  that band and code ("reserved" when the table has none), the uplink and
  Yantra's channel number (band x 10000 + carrier).
- Frequency to channel: a downlink frequency in MHz gives its band,
  carrier number, offset and channel number, or "Not on the TETRA raster".
- Subscriber classes: the `subscriber_class` value (0..65535) and the 16
  classes as check boxes, both ways, with the hexadecimal value. Class 1
  is the first bit sent in D-MLE-SYSINFO, the most significant bit of the
  value (0x8000 = class 1 only).

Every text of the window is translated (nl, fr, de); the kit checks it
(`--strict-translations`) and the golden file holds the first frame.

## Tests

- `conformance_tetra`: the plugin test kit (PLG-13) on two record files,
  `yantra-gen --scenario fraunces` (dBm) and the simulator through
  `yantra-radio-host` (dBFS, with C1), against `testdata/tetra.golden.json`;
  `conformance_tetra_live` on the live simulator.
- `test_app_ui` (`ui/test_ui_cells.cpp`): the calculator window through
  the app, both directions and the subscriber classes.
- `test_tetra`: raster and duplex values, C1 by hand, against
  `tetra_c1_db` and against the C1 in the record files, reselection and
  verification cases, the schemas of every shipped radio plugin.
- `test_measure_host`: the simulator through the radio host and pipeline
  (records on `tetra_cell`, the radio's C1 equal to the plugin's), and
  `soapysdr_fake` with a calibration (C1 estimated and flagged).

## Open points

- The reselection formulation is ours; it is to be checked against
  EN 300 392-2 clause 18.3.4. The cell database's simulator (DB-08) uses
  it as it is. The earlier implementation (yantra-v1) applied
  relinquishable and improvable only when a neighbour meets the
  criterion and had 5 s / 15 s timers; here the states follow the
  thresholds alone and a walk's hold time is the caller's
  (`celldb::WalkOptions::hold_s`).
- Help texts are English only; the labels are translated.
