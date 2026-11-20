<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# yantra-gen: TETRA test-signal generator

`yantra-gen` (RAD-71) writes simulated TETRA downlink I/Q from a scenario
file (RAD-74) or from cells given on the command line: a fictitious
network (sync bursts, SYSINFO, AACH, D-NWRK-BROADCAST with neighbours),
several carriers in one span, the channel model (RAD-72) and the indoor
multi-wall model (RAD-73). Next to the samples it writes SigMF metadata,
a ground-truth CSV, a cell CSV and a ground-truth record file (RAD-65),
so tests know exactly what a receiver should find. Every file is marked
as simulated; nothing in it is a real network.

The decoder tests use its IQ files; Yantra's tests use its record files
(Yantra TST-10). Code: `tools/yantra-gen/` on top of the generator library
`gen/` (`yantra::gen`). Scenario files:
[SCENARIO_FORMAT.md](SCENARIO_FORMAT.md); channel and multi-wall model:
[CHANNEL_MODEL.md](CHANNEL_MODEL.md); record files:
[RECORD_FILES.md](RECORD_FILES.md).

## Usage

```
yantra-gen [options] --out FILE|-
yantra-gen --list | --help | --version
```

### Scenario

| Option | Meaning |
|---|---|
| `--scenario FILE\|ID` | a scenario file, or the `id` of a file in the scenario folder |
| `--example ID` | the scenario of a bundled plan's example id (RAD-18): the file listing it in `example_ids`, else the default scenario |
| (neither) | the default scenario (`"default": true`, `indoor_good_das`) |
| `--scenario-dir DIR` | search DIR first |
| `--cell K=V,...` | a cell on the command line instead of a scenario; repeat for more cells |
| `--mcc N`, `--mnc N` | network of the `--cell` cells (default 901, 1) |
| `--list` | the scenarios of the scenario folder: id, name, default mark, example ids |

Scenario folders, first existing one wins: `--scenario-dir`,
`$YANTRA_SCENARIO_DIR`, `<directory of the program>/../share/yantra-radios/scenarios`
(the installed layout; the build tree mirrors it as
`build/share/yantra-radios/scenarios`). The program folder comes from the
running program itself (`/proc/self/exe` on Linux, `GetModuleFileNameW` on
Windows), so an installed or unpacked copy works wherever it is; no folder
of the build machine is compiled in.

`--cell` keys: `id` (default `C<n>`), `carrier` (main carrier, required),
`band` (3), `offset` (0..3), `la` (1), `cc` (colour code, 1), `number`
(neighbour cell identifier, default n), `level` (dBm, -70), `ppm` (base
station oscillator error, 0), `encryption` (0 or 1, the air-interface
encryption flag only), `neighbours` (cell ids joined with `+`), `label`.
Example: `--cell id=A,carrier=3600,la=10,cc=5,neighbours=B --cell id=B,carrier=3604,la=11,cc=6,level=-85`.

### Run

| Option | Meaning |
|---|---|
| `--duration S` | length in seconds (default: the scenario's). `0` streams without end, only with `--out -` |
| `--rate HZ` | sample rate 50k..20M (default: the scenario's, 500 ksps). All carriers plus 12.15 kHz must fit in +/- rate/2 |
| `--centre HZ` | span centre (default: the scenario's, else the middle of its carriers); also `--center`, `--freq` |
| `--seed N` | random seed (default: the scenario's) |
| `--truth-interval S` | ground-truth row interval (default: the scenario's, 0.1 s) |
| `--format F` | `cf32` (default), `cs16`, `cs8` or `cu8` |
| `--full-scale-dbm DBM` | integer formats: the level (dBm) of a full-scale component, see "Sample scaling" |

Rates and frequencies take an optional `k`, `M` or `G` suffix.

### Impairments (RAD-72)

They replace the scenario's values; see CHANNEL_MODEL.md for the models.

| Option | Scenario member |
|---|---|
| `--noise-figure DB`, `--no-noise` | `receiver.noise_figure_db`, `receiver.noise` |
| `--ppm PPM` | `receiver.ppm` (LO error: every carrier moves by -ppm x centre) |
| `--clock-ppm PPM` | `receiver.clock_ppm` (sample-clock error) |
| `--gain-db DB` | `receiver.gain_db` |
| `--iq-gain-db DB`, `--iq-phase-deg DEG`, `--swap-iq` | IQ imbalance and swap |
| `--dc-dbfs DBFS` | DC spike relative to the receiver's full scale |
| `--fading none\|rayleigh\|rician\|static\|tu50\|ht200` | `channel.fading.model` |
| `--rician-k DB`, `--doppler HZ`, `--doppler-shift HZ` | Rician K, maximum Doppler of flat fading, fixed Doppler shift |
| `--drops FIRST_S,INTERVAL_S,SAMPLES` | sample drops: SAMPLES samples every INTERVAL_S from FIRST_S |

### Output

| Option | Meaning |
|---|---|
| `--out FILE` | data file (required). Without an extension it gets `.sigmf-data` |
| `--out -` | samples to stdout; only side files named explicitly are written |
| `--meta F`, `--truth F`, `--cells F`, `--records F` | side-file names |
| `--no-side-files` | samples only |
| `--quiet` | no progress lines on stderr |

Side files (default names from the data file without its extension, STEM):

| File | Content |
|---|---|
| `STEM.sigmf-meta` | SigMF metadata |
| `STEM.truth.csv` | ground truth per time step and cell |
| `STEM.cells.csv` | what each cell broadcasts |
| `STEM.records.jsonl` | ground-truth record file (RAD-65) |

Exit codes: 0 done; 1 generation error; 2 usage error (unknown option,
bad value, `--out` missing, conflicting options); 3 scenario unknown,
unreadable or invalid (the message names the file and JSON path);
4 an output file cannot be written. Files are written under a temporary
name (`.part`) and renamed when complete; a failed run leaves no files.

The output is deterministic: the same scenario, options and seed give the
same bytes on every platform and build tree (no time stamps, own random
generator, no fused multiply-add in `gen/`).

Examples:

```
yantra-gen --scenario failed_site --format cs8 --out failed_site.cs8
yantra-gen --example fraunces-first-floor --duration 30 --out fraunces
yantra-gen --cell carrier=3600,la=7,cc=9 --rate 1M --format cu8 --out one.cu8
yantra-gen --scenario weak_signal --duration 0 --format cs16 --out - | my-receiver
```

## Sample formats and scaling

| `--format` | SigMF `core:datatype` | Sample |
|---|---|---|
| `cf32` | `cf32_le` | float I, float Q; power 1 = the receiver's `reference_level_dbm` (default -20 dBm) |
| `cs16` | `ci16_le` | int16 I, Q; full scale 32767 |
| `cs8` | `ci8` | int8 I, Q; full scale 127 |
| `cu8` | `cu8` | uint8 I, Q; code = round(127.5 + 127.5 x / A), 0..255 (rtl-sdr style; read back as (code - 127.5) / 127.5) |

For the integer formats, a component amplitude A maps to the largest
code, where A is the amplitude of a constant sample (A, 0) whose power is
the full-scale level in dBm. The full scale is `--full-scale-dbm`, else
the scenario's `receiver.full_scale_dbm`, else automatic: the highest
total instantaneous level of the run (all cells, noise, receiver gain and
gain events, on a 1 ms grid) plus 10 dB headroom as an amplitude, plus the
receiver's DC offset, rounded up to a whole dB. Values beyond full scale
are clipped; the number of samples with a clipped component is in the
metadata and a warning on stderr. All bundled scenarios run without
clipping at the automatic scale.

## SigMF metadata

SigMF 1.2 (`core:version` "1.2.0"), one channel. `global`:
`core:datatype`, `core:sample_rate`, `core:version`, `core:num_channels`,
`core:description`, `core:author`, `core:recorder` ("yantra-gen
VERSION"), `core:hw`, `core:extensions` (the `yantra` extension, optional),
and the `yantra:` members:

| Member | Meaning |
|---|---|
| `yantra:simulated` | always `true` |
| `yantra:generator_version` | yantra-gen version |
| `yantra:scenario_id`, `yantra:scenario_name`, `yantra:scenario_format` | the scenario (`command-line` for `--cell`) |
| `yantra:seed`, `yantra:duration_s` | the run |
| `yantra:reference_level_dbm` | dBm of sample power 1 in engine units (cf32) |
| `yantra:full_scale_dbm` | integer formats: dBm of a full-scale component |
| `yantra:clipped_samples`, `yantra:dropped_samples` | counts |
| `yantra:true_sample_rate` | the rate the signal is really sampled at (sample-clock error) |
| `yantra:truth_file`, `yantra:cells_file`, `yantra:records_file` | side-file names (null when not written) |
| `yantra:planted` | what the scenario plants on purpose |

`captures`: one segment at sample 0 (`core:frequency` = span centre,
`core:global_index` 0), plus one segment after every sample drop whose
`core:global_index` is the signal sample index it continues at (the gap
to `core:sample_start` is the number of samples dropped so far;
`yantra:dropped_before` the drop length).

`annotations`, sorted by `core:sample_start`: one per cell over the whole
recording (`core:freq_lower_edge`/`core:freq_upper_edge` = carrier
+/- 12.5 kHz, `core:label` the cell id, `core:comment` its identity, and
`yantra:cell`, `yantra:mcc`, `yantra:mnc`, `yantra:la`,
`yantra:colour_code`, `yantra:main_carrier`, `yantra:encryption`,
`yantra:cfo_hz`), and one per scenario event at its output sample
(`core:label` e.g. `cell_off N2`, `rx_gain -6.0 dB`; `yantra:event_t_s`).

## Ground-truth CSV (`STEM.truth.csv`)

One row per truth time step (every `truth_interval_s` of signal time from
t = 0) and cell, in scenario order:

| Column | Meaning |
|---|---|
| `t_s` | signal time, s |
| `sample`, `output_sample` | signal sample index, and index in the file (after drops) |
| `floor`, `x_m`, `y_m` | receiver position (empty without one) |
| `noise_dbm` | receiver noise in 18 kHz, dBm (`-inf` without noise) |
| `rx_gain_db` | receiver gain (gain events included) |
| `cell`, `mcc`, `mnc`, `la`, `colour_code` | cell identity |
| `frequency_hz`, `offset_hz` | nominal downlink frequency and offset from the span centre |
| `cfo_hz` | frequency error in the samples (cell ppm, receiver ppm, Doppler shift) |
| `on_air` | 1 or 0 (failed site, cell off events) |
| `source` | how the level was worked out: `fixed`, `scene` (multi-wall), `site` (log-distance), `off` |
| `level_dbm` | local mean level at the antenna connector (no fast fading), dBm; `-inf` when off |
| `level_inst_dbm` | with flat fading: what the samples carry at that time |
| `snr_db` | `level_dbm` minus `noise_dbm` |
| `best_antenna`, `walls` | scene cells: strongest antenna and its walls crossed |

## Cell CSV (`STEM.cells.csv`)

One row per cell: `cell`, `label`, `mcc`, `mnc`, `la`, `colour_code`,
`cell_number`, `main_carrier`, `frequency_band`, `offset`, `frequency_hz`,
`offset_hz`, `encryption`, `on_air` (at the start), `neighbours`: the
broadcast neighbour list as `id:carrier[:LAn]` separated by spaces, with
planted (wrong) values as broadcast.

## Ground-truth record file (`STEM.records.jsonl`)

A record file (RAD-65, [RECORD_FILES.md](RECORD_FILES.md)) with what an
ideal, calibrated TETRA plugin would deliver: header plugin `yantra-gen`
(technology `tetra`, capabilities `simulated` and `self_calibrated`, a
`carriers` plan with every cell's frequency, `created_utc_ns` 0), then per
truth row:

- a `serving_changed` event when the serving cell (the strongest on-air
  cell) changes;
- one `tetra_cell` record per on-air cell (flag `simulated`;
  `time.monotonic_ns` = signal time; `utc_source` none);
- the serving cell's neighbour list as `tetra_neighbour` records at the
  first row, on a serving change and every second;

and at the end an `end_of_stream` event and the end line.

The schemas are the shared TETRA schemas of the radio parts
([TETRA_RECORDS.md](TETRA_RECORDS.md); level in dBm, simulated), the same
ones the measurement engine and the TETRA plugins use, so truth and
decoded records compare field by field. yantra-gen fills:

| Field | Group | Meaning |
|---|---|---|
| `frequency_hz`, `channel`, `role`, `level`, `mcc`, `mnc`, `la`, `colour_code` | core | `level` in dBm = the truth `level_dbm`; `channel` the main carrier; `role` serving (strongest on-air cell) / neighbour (in the serving cell's list) / other |
| `snr_db`, `freq_error_hz`, `freq_error_ppm` | quality | truth SNR and `cfo_hz` |
| `c1_db` | c1 | C1 from the truth level and the SYSINFO values (MS maximum 30 dBm) |
| `encryption`, `ms_txpwr_max_cell`, `ms_txpwr_max_cell_dbm`, `rxlev_access_min`, `rxlev_access_min_dbm`, `main_carrier`, `frequency_band` | broadcast | SYSINFO values, codes and dBm |
| `sim_floor_id`, `sim_x_m`, `sim_y_m` | sim_position | receiver position (only when there is one) |
| `cell_id`, `level_inst_dbm`, `level_source`, `best_antenna`, `walls` | other | scenario cell id and the truth columns |

`tetra_neighbour`: `frequency_hz` (the serving carrier), `entry_index`,
`entry_count`, `cell_number`, `main_carrier` (as broadcast),
`neighbour_frequency_hz`, `neighbour_la` (only when broadcast),
`neighbour_cell_id`.

## Tests

- `tests/radio/yantra_gen` (`test_yantra_gen`): options and exit codes, every
  format against the engine's samples, clipping, automatic scale on every
  bundled scenario, SigMF validity, truth CSV, cell CSV and record file
  against scenario and engine, example ids, seeded golden digests
  (`tests/radio/yantra_gen/golden/digests.txt`; rewrite on purpose with
  `test_yantra_gen --update` or the target `update-golden-yantra-gen`);
  process tests for stdout streaming and an install to a scratch prefix.
- `tests/radio/gen_osmo` (`test_gen_osmo`): a yantra-gen cs16 file with two
  carriers, noise and frequency errors decodes with osmo-tetra's burst
  synchroniser and receive chain; SYNC, SYSINFO and D-NWRK-BROADCAST match
  the ground truth. osmo-tetra is the independent cross-check oracle.

## Install

`cmake --install build --prefix PREFIX` installs `PREFIX/bin/yantra-gen`
and the scenario files with their scenes in
`PREFIX/share/yantra-radios/scenarios`. The installed program finds them
relative to itself, so the tree may be moved (or unpacked from the release
.tar.gz/.zip) anywhere.
