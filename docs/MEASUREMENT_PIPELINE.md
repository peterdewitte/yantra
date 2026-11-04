<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Measurement pipeline

The measurement pipeline (OA-13) turns the measurement records of the radio
plugins into calibrated, flagged, timestamped samples: one measurement
cycle per period (at least 1 Hz) with a sample for every carrier Yantra
knows. Views, recording, walk test, export and reports read these samples.
The pipeline is part of the core and knows no technology (PLG-01): it reads
only the core fields of the radio API (`frequency_hz`, `role`, `level`, and
`entry_index` of neighbour lists, RAD-21..23) and the field categories of
the technology schema.

Code: `core/include/yantra/core/measure/measurement_pipeline.h` (pipeline,
configuration, statistics, observers, `CycleRunner`), `sample.h` (flags,
mapped records, cycles), `correction.h` (level-correction stage),
`catalog.h` (technology schemas), `technology_map.h` (schema mapping).
Tests: `tests/measure/test_pipeline.cpp` (unit tests with a manual clock),
`tests/measure_host/` (behind the radio host: fake radio, simulator,
beacon example).

## Stages

| # | Stage | What it does | Requirements |
|---|-------|--------------|--------------|
| 1 | Input | The radio host's record sink checks every record against its radio schema, copies it into a bounded queue (drops and counts when full, never blocks the plugin) and hands it on from a delivery thread per stream (`MeasurementInput::on_radio_record`). Stream and host events arrive the same way (`on_radio_event`). | RAD-24, PLG-25 |
| 2 | Schema mapping | The radio record becomes a record of the technology plugin's schema with the same id and technology id (`TechnologyCatalog`, filled from the enabled technology plugins), field by field by name. Without a technology plugin, or when its schema does not describe the radio schema (logged once, counted), the generic schema derived from the radio schema is used (`generic_schema`); such records are marked `generic`. | PLG-05, PLG-12 |
| 3 | Level correction | The `LevelCorrector` is asked once per record, with the record's device, carrier frequency and calibration key (the radio host attaches the key that was valid when the record was measured). Its answer is added to every LEVEL reading: `dBm = dBFS + offset_db + correction_db` for dBFS radios and `dBm + correction_db` for SELF_CALIBRATED radios. Only DOUBLE fields in the unit of the radio's own level are readings; broadcast thresholds are SYSTEM_INFO and are never corrected. Without a calibration a dBFS level keeps its value and is flagged uncalibrated. The answer is kept with the record. The table and antenna correction are described in [CALIBRATION.md](CALIBRATION.md) (`calibration::TableCorrector`). | RF-02, HW-01b, HW-09 |
| 3b | C1 | For a record of a technology plugin's cell schema with a DOUBLE `c1_db` field (the radio API's C1): a C1 delivered by the radio is kept as it is. When the radio left it empty and the level is calibrated (dBm), the plugin's `compute_c1` is asked with the corrected level and `ms_power_dbm`; its answer fills the field and the record is flagged `c1_estimated`. UNSUPPORTED (missing broadcast parameters) leaves it empty; an error answer also does and is counted (`c1_failed`) and logged once. An uncalibrated (dBFS) level gets no C1. | RF-06, PLG-02 |
| 4 | Checks and carrier state | A level that is not finite or outside [`min_level`, `max_level`] is refused; a record older than the newest of its carrier is ignored (out of order), one equal to it is ignored (duplicate); one that arrived more than `late_after_ns` after it was measured is used and flagged late. Each carrier keeps its newest record. | TST-23 |
| 5 | Observers | `on_record` for every accepted record (recording, feature plugins' `on_record`), on the delivery thread; `on_event` for every event. | REC, PLG-02 |
| 6 | Cycle | `tick()` closes every period that has ended on the clock and gives `on_cycle` one `MeasurementCycle` per period with a `CarrierSample` per carrier: the newest record, its age, the records since the previous cycle and the flags. | OA-13, RF-01, RF-03, RF-04, RF-05, RF-07, TST-31 |

A carrier is one (radio plugin, device, schema, frequency) and, for
neighbour lists, the entry index. Its part (`CarrierRole`) comes from the
schema kind and the core role: serving (RF-01), neighbour (RF-03, the
neighbour's measured level), other, scan channel (RF-04) and neighbour list
entry (RF-03, the broadcast list). Quality values (RF-05, and EVM/MER and
frequency error for RF-07 when the radio delivers them) are the QUALITY
fields present in the record (`MappedRecord::quality`).

## Flags (RF-08)

Every sample carries validity flags (`sample_flag`, text by
`sample_flags_text`). The record flags have the values of the radio API and
plugin API record flags; the cycle adds the others.

| Flag | Set when |
|------|----------|
| `no_sync` | The record has NO_SYNC (level only, no identity), or a SYNC_LOST event for the carrier came after its last synchronised record. |
| `overload` | The record has OVERLOAD (clipping or saturation). |
| `uncalibrated` | The record has UNCALIBRATED, or its dBFS level had no calibration entry (HW-01b). |
| `simulated` | The record has SIMULATED or the radio plugin has the SIMULATED capability (DEMO-07). |
| `held` | No record for the carrier arrived since the previous cycle: the newest value is repeated. |
| `stale` | The newest record is older than `stale_after_ns` (scan channels: `scan_stale_after_ns`) at the cycle time. |
| `late` | A record that arrived since the previous cycle came later than `late_after_ns` after it was measured. |
| `device_lost` | The device was unplugged (DEVICE_LOST) and has not come back or delivered records since. |
| `data_loss` | The radio reported dropped records or sample overflows (DROPPED, OVERFLOW) in this cycle. |
| `c1_estimated` | The record's C1 was calculated by Yantra (technology plugin, calibrated level, MS power), not delivered by the radio (`MappedRecord::c1_source`). |

A flagged value is kept and shown; the flags say how far it can be trusted.
A carrier leaves the cycle when its newest record is older than
`drop_after_ns` (`scan_drop_after_ns`), and at the next cycle when its device's stream
stops or its plugin fails.

## Timing (TST-31)

- Clock: the pipeline uses a `platform::Clock` (monotonic and UTC). The app
  uses the system clock; tests and replay inject a `ManualClock`, so no test
  waits on the wall clock for a cycle. Record times are the radio host's
  monotonic clock (RAD-27), the same epoch.
- Grid: cycle *k* (from 0) ends at `start + (k + 1) * period_ns`, where
  `start` is the clock when the pipeline was made. `period_ns` is at most
  1 s (the configuration refuses anything slower); the default is 1 s.
- Timestamps: a cycle's monotonic time is its grid time; its UTC time is the
  clock's UTC at the tick minus the time since the grid time.
- No gaps: a tick that comes late emits every cycle it missed, in order, up
  to `max_catch_up`; any further cycles are counted
  (`PipelineStats::skipped_cycles`) and the sequence number jumps by the
  same amount, so a gap is always visible.
- `CycleRunner` drives a pipeline on the real clock (a thread that ticks at
  each period end); with a manual clock the caller ticks.
- A sample's age is the cycle time minus the record's measurement time
  (0 when the record was measured after the cycle's grid time but arrived
  before the tick).

## Robustness (TST-23)

| Situation | Behaviour |
|-----------|-----------|
| Records faster than the pipeline | The radio host's sink queue (`sink_capacity`) drops and counts (`SinkCounters::dropped`); the plugin's push returns at once. |
| Burst within one cycle | All are accepted; the sample holds the newest and `records_in_cycle` counts them. |
| Out of order | Ignored and counted (`out_of_order`); per carrier. |
| Duplicates | Ignored and counted (`duplicates`). |
| Late | Used if newer than the carrier's newest; flagged `late`; counted. |
| NaN, Inf, out-of-range level | Refused and counted (`implausible`); the radio host already refuses non-finite values. |
| Unplug and replug | `device_lost` until the device is back; then fresh samples. |
| Unknown technology | Generic schema; counted (`generic`). |

## Threading

Records and events of different radios arrive on their own delivery
threads, each marked as a measurement thread (PLG-07). The pipeline holds
its state lock only for short updates and calls observers without it:
`on_record`/`on_event` on the delivery thread, `on_cycle` on the ticking
thread, in sequence. Observers must not block: a slow `on_record` makes that
radio's sink queue fill and drop (counted), never the plugin wait. The
level corrector is called on the delivery threads, several at once.

## Hooks for the next steps

- Calibration and antenna correction (6.2, done): `calibration::TableCorrector`
  with the user's table and `AntennaSettings`, set with `set_corrector`;
  set a new one when the table or antenna settings change.
  [CALIBRATION.md](CALIBRATION.md).
- TETRA technology plugin (6.3, done): add it to the `TechnologyCatalog`
  with `add(info, descriptor, instance)` of the enabled plugin, so records
  map onto its schemas and C1 is estimated where the radio has none.
  Remove it (`remove_plugin`) before the plugin is disabled or unloaded.
  `PipelineConfig::ms_power_dbm` is the RF-06 MS class power setting
  (30 dBm portable, 35 dBm mobile).
- Record files and simulator tests (6.4): replay a record file by building
  `RadioRecord`s with `radio_schema` set from a `RadioSchemaView` and
  driving a `ManualClock` from the record times.
