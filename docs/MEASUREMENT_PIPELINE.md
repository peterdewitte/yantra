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
| 3 | Level correction | The `LevelCorrector` is asked once per record, with the record's device, carrier frequency and calibration key (the radio host attaches the key that was valid when the record was measured). Its answer is added to every LEVEL reading: `dBm = dBFS + offset_db + correction_db` for dBFS radios and `dBm + correction_db` for dBm levels (a SELF_CALIBRATED radio, or a record flagged `YR_RECORD_LEVEL_DBM`, radio API 1.1: the unit is the record's, whatever its schema says). Only DOUBLE fields in the unit of the radio's own level are readings; broadcast thresholds are SYSTEM_INFO and are never corrected. Without a calibration a dBFS level keeps its value and is flagged uncalibrated. The answer is kept with the record. The table and antenna correction are described in [CALIBRATION.md](CALIBRATION.md) (`calibration::TableCorrector`). | RF-02, HW-01b, HW-09 |
| 3b | C1 | For a record of a technology plugin's cell schema with a DOUBLE `c1_db` field (the radio API's C1): a C1 delivered by the radio is kept as it is. When the radio left it empty and the level is calibrated (dBm), the plugin's `compute_c1` is asked with the corrected level and `ms_power_dbm`; its answer fills the field and the record is flagged `c1_estimated`. UNSUPPORTED (missing broadcast parameters) leaves it empty; an error answer also does and is counted (`c1_failed`) and logged once. An uncalibrated (dBFS) level gets no C1. | RF-06, PLG-02 |
| 4 | Checks and carrier state | A level that is not finite or outside [`min_level`, `max_level`] is refused; a record older than the newest of its carrier is ignored (out of order), one equal to it is ignored (duplicate); one that arrived more than `late_after_ns` after it was measured is used and flagged late. Each carrier keeps its newest record. | TST-23 |
| 5 | Observers | `on_input` for every input, record or event, also refused, ignored and duplicate records (`PipelineInput`: its number in the order the state took it in, its arrival time on the pipeline clock, the level correction used; what a recording stores, REC-03); `on_record` for every accepted record (feature plugins' `on_record`), on the delivery thread; `on_event` for every event. | REC-03, PLG-02 |
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
  clock's UTC at the tick minus the time since the grid time. The UTC is
  read once per tick, so the cycles of a late tick are a period apart in
  UTC as in monotonic time even while the wall clock is being set, and a
  replay (which sets its clock once per tick) gives the same times.
- No gaps: a tick that comes late emits every cycle it missed, in order, up
  to `max_catch_up`; any further cycles are counted
  (`PipelineStats::skipped_cycles`) and the sequence number jumps by the
  same amount, so a gap is always visible.
- `CycleRunner` drives a pipeline on the real clock (a thread that ticks at
  each period end); with a manual clock the caller ticks.
- A sample's age is the cycle time minus the record's measurement time
  (0 when the record was measured after the cycle's grid time but arrived
  before the tick).

## Recording and replay (REC-03, REC-07)

A recording ([formats/RECORDING.md](formats/RECORDING.md)) keeps the
pipeline's inputs, not only its output, so a replay drives the same
pipeline and gets the same cycles and statistics as the live run:

- Every input has a number (`PipelineInput::sequence`, 1, 2, ...) given
  under the state lock, and every cycle says how many inputs it was built
  from (`MeasurementCycle::inputs`) and when its tick ran (`built_ns`).
  Records of several delivery threads may reach an observer in another
  order than their numbers; the numbers are the order that counts.
- `snapshot()` gives the whole state between two inputs
  (`PipelineState`: the cycle grid, the next cycle number, the inputs
  taken in, every carrier with the radio record of its newest sample, its
  arrival time and correction, the device states), with the stats.
  `restore()` puts it into another pipeline (the records are mapped again
  through the same technology plugins and the recorded corrections).
- A replay restores a snapshot (a keyframe of the recording), then feeds
  each later input at its arrival time (a `ManualClock`) with its
  recorded correction, and ticks at each recorded tick time
  (`ManualClock::set(monotonic, utc)`), after the inputs that cycle was
  built from. `cycle_difference()` names the first difference between two
  cycles ("" when equal).

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

## Self-interference (NFR-07)

A laptop, its USB bus and cables radiate RF noise that an antenna close
to them picks up: the noise next to every carrier rises and the radio
sees carriers it cannot sync to. Yantra has no spectrum (the radio
plugins keep their IQ), so `measure::SelfInterferenceMonitor`
(`core/measure/self_interference.h`) works on the cycles:

| Check | Rule (defaults) |
| --- | --- |
| Noise | the least level - `snr_db` (radio API QUALITY field) over the cycle's fresh, synced, calibrated samples, dBm |
| Rise | noise minus the reference: the noise taken with the antenna away from the PC (`set_reference`, from `cycle_noise_dbm`), else the thermal floor -174 dBm/Hz + 10 log10(25 kHz) + 15 dB noise figure (about -115 dBm). A rise >= 6 dB is a finding when a sample has an SNR below 30 dB (`snr_limit_db`) |
| Unsynced | 3 or more fresh NO_SYNC samples (a level but no identity) are a finding; not counted: those less than 6 dB above the reference, and those within 100 kHz of a synced carrier 30 dB or more stronger (its skirts) |
| Warning | on after 3 cycles in a row with a finding, off after 10 in a row without one |

Samples without an SNR, uncalibrated (dBFS), stale or held samples do
not count. Each sample's level - `snr_db` is the noise next to its
carrier plus the receiver's own distortion: an SNR estimate stops rising
at the receiver's limit (about 36 dB for the TETRA receiver of
yantra-radios), so for a strong carrier, such as an indoor DAS at
-35 dBm, it lies 60 dB above the noise. The least of them is the closest
to the noise (noise from the PC raises it next to every carrier), and a
rise needs a sample below the SNR limit: strong carriers alone only bound
the noise. A strong carrier also shows weak unsynced carriers beside it
(its skirts, 6.25 to 62.5 kHz away and 50 dB down), and the receiver
reports some carriers at the noise itself; neither counts.
(Before, the median of level - `snr_db` and every NO_SYNC sample raised
a false warning on the demo's Fraunces walk: an indoor DAS carrier next
to a weak macro cell.)

The warning carries an English text with the advice for the UI (phase 9: status bar and level panel): keep the antenna away from the
laptop, its USB ports and cables; a USB extension cable helps. The radios'
scenarios as recorded do not raise it; the same records with 20 dB more
noise next to every carrier do (tests/measure_host/test_measure_self_interference.cpp).

## Threading

Records and events of different radios arrive on their own delivery
threads, each marked as a measurement thread (PLG-07). The pipeline holds
its state lock only for short updates and calls observers without it:
`on_record`/`on_event` on the delivery thread, `on_cycle` on the ticking
thread, in sequence. Observers must not block: a slow `on_record` makes that
radio's sink queue fill and drop (counted), never the plugin wait. The
level corrector is called on the delivery threads, several at once.

## Hooks for the next steps

- Self-interference (7.6, done): feed every cycle to a
  `SelfInterferenceMonitor` on the cycle thread and show
  `warning().text` while `active` (UI, phase 9); offer "take reference"
  with the antenna away from the PC (`set_reference(cycle_noise_dbm(c))`).
- Cell database (7.6, done): `celldb::Observations::add(cycle)` collects
  the survey's cells and lists for verification, cross-check and learning;
  `celldb::walk_steps` gives the reselection simulator the levels.
  [CELL_DATABASE.md](CELL_DATABASE.md).

- Calibration and antenna correction (6.2, done): `calibration::TableCorrector`
  with the user's table and `AntennaSettings`, set with `set_corrector`;
  set a new one when the table or antenna settings change.
  [CALIBRATION.md](CALIBRATION.md).
- TETRA technology plugin (6.3, done): add it to the `TechnologyCatalog`
  with `add(info, descriptor, instance)` of the enabled plugin, so records
  map onto its schemas and C1 is estimated where the radio has none.
  Remove it (`remove_plugin`) before the plugin is disabled or unloaded.
  `PipelineConfig::ms_power_dbm` is the RF-06 MS class power setting
  (30 dBm portable, 35 dBm mobile). [plugins/TETRA.md](plugins/TETRA.md).
- Record files (6.4): `measure::replay(file, pipeline, options)` feeds a
  yantra-radios record file in as the radio host would: every record with
  its header schema as `radio_schema` and a calibration key
  (`ReplayOptions::calibration_key`, else the header device's driver and
  serial: `device_calibration_key`; record files hold no CALIBRATION
  settings), so replayed dBFS levels are calibrated and get an estimated
  C1 like live ones. For cycles, `ReplayOptions::advance` is called with
  each line's time before it is fed: move a `ManualClock` there and
  `tick()` every period that ended (the cycles of the recording, TST-31).
- End-to-end tests (TST-10, tests/measure_host/test_measure_e2e.cpp): the
  radios' scenarios through RadioHost + pipeline + TETRA plugin with the
  simulator plugin, and as record files the radios' yantra-gen and
  `yantra-radio-host decode-iq` write into the build tree at build time
  (tests/measure_host/gen_records.cmake; nothing committed), against the
  generator's ground truth (STEM.cells.csv, STEM.truth.csv): levels,
  identities, neighbour lists, delivered and estimated C1, flags, held
  carriers, cadence; late, burst, out-of-order and duplicate records
  (TST-23); calibration to 0.01 dB (TST-30). A 600 s replayed walk is the
  opt-in ctest test_measure_host_long.
