<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Scenario files

A scenario (RAD-74) describes a fictitious TETRA network as it is received
inside a building: the cells and their broadcast parameters, the floors
with their walls and antennas, the channel and receiver impairments, where
the receiver is over time and what happens when. The simulator plugin
(RAD-61), `yantra-gen` (RAD-71) and the tests use the same files. They live
in `data/scenarios/*.json`; scene files they reference live in
`data/scenarios/scenes/`.

The code is `gen/include/yantra/gen/scenario.hpp` (parsing, validation)
and `scenario_engine.hpp` (running a scenario). The formulas of the
channel and the multi-wall model are in [CHANNEL_MODEL.md](CHANNEL_MODEL.md).
All networks are fictitious (MCC 901, the test range); no real network,
place or person is described.

## Format and version

```json
{ "format": "yantra-radios-scenario", "version": 1, "id": "indoor_good_das", ... }
```

- `format` must be `"yantra-radios-scenario"`; anything else is not a
  scenario.
- `version` is an integer; this program reads version 1. A newer version is
  refused with a message naming both versions. Version 1 may gain optional
  members: unknown members are ignored everywhere, so older programs read
  newer version-1 files. A change that older programs would misread raises
  the version (CHANGELOG.md, "Formats").
- Errors name the place of the problem as a path, for example
  `cells[1].neighbours[0].cell: unknown cell "X9"`.

## Top level

| Member | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | required | Unique id; the file name is `<id>.json`. |
| `name`, `description` | string | "" | Shown in the simulator's scenario list. |
| `example_ids` | [string] | [] | Bundled example plans (RAD-18 `example_id`) this scenario belongs to; an entry ending in `*` matches every id starting with the rest. |
| `default` | bool | false | The scenario for plans without one of their own (one file). |
| `seed` | unsigned | 1 | Seed of every random part (noise, fading, shadowing). |
| `duration_s` | number | 10 | Signal time; 0 = endless (the simulator). At most 86400. |
| `truth_interval_s` | number | 0.1 | Time between ground-truth rows (0.001..3600). |
| `sample_rate_hz` | number | 500000 | Complex sample rate of the span (36 k..20 M). |
| `centre_frequency_hz` | number | middle of the carriers | Centre of the span. Every carrier, with its frequency errors, must fit: `|offset| + 12.15 kHz < fs / 2`. |
| `network` | object | | `mcc` (default 901), `mnc` (default 1). |
| `cells` | [cell] | required | At least one. |
| `floors` | [floor] | [] | The building's floors as scenes. |
| `antennas` | [override] | [] | `{ "id", "eirp_dbm" }`: EIRP of scene antennas by id, also in scenes the host supplies at run time. |
| `channel` | object | | Channel parameters (below). |
| `receiver` | object | | Receiver and its impairments (below). |
| `path` | object | | Receiver position over time (below). |
| `events` | [event] | [] | Time line (below). |
| `planted` | [string] | [] | What the scenario plants on purpose (for test and manual authors). |

## Cells

| Member | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | required | Unique; the same id as the cell in the scenes. |
| `label` | string | "" | |
| `cell_number` | 0..31 | 1 | The cell identifier neighbour lists use. |
| `la` | 0..16383 | 1 | Location area. |
| `colour_code` | 0..63 | 1 | |
| `frequency_band`, `main_carrier`, `offset` | 0..15, 0..4095, 0..3 | 3, 3600, 0 | Downlink `band x 100 MHz + carrier x 25 kHz + offset` (0, +6.25, -6.25, +12.5 kHz). |
| `encryption` | bool | false | The air-interface encryption flag of SYSINFO only. |
| `rxlev_access_min`, `ms_txpwr_max_cell` | 0..15, 0..7 | 2, 5 | SYSINFO values. |
| `reselect` | object | 2, 2, 4, 2 | `slow_threshold`, `slow_hysteresis`, `fast_threshold`, `fast_hysteresis` of D-NWRK-BROADCAST. |
| `network_time` | bool | true | D-NWRK-BROADCAST carries the network time. |
| `neighbours` | [ref] | [] | `{ "cell", "main_carrier"?, "la"? }`, at most 31: the cells this cell announces. The optional values plant a wrong announcement. A neighbour in another band or with an offset gets a carrier extension. |
| `level_dbm` | number | -70 | Level when there is no position, or no antenna of the cell on the receiver's floor and no site. |
| `site` | object | none | An outdoor site in the plan frame: `x_m`, `y_m` (required), `height_m` (30, above the receiver's floor), `eirp_dbm` (45), `pattern` (`omni`, or `sector` with `azimuth_deg`, `beamwidth_deg`, `front_to_back_db`). |
| `ppm` | number | 0 | Base-station oscillator error. |
| `time_offset_s` | number | 0 | The cell has been transmitting this long at t = 0 (TDMA time, 0 to below 61.2 s). |
| `on_air` | bool | true | false: the cell is announced but does not transmit (a failed site). |

## Floors

Each floor is a scene (RAD-36, [SCENE_FORMAT.md](SCENE_FORMAT.md)), either
inline, `{ "scene": { ... } }`, or from a file,
`{ "scene_file": "scenes/demo_office_f0.json" }` (relative to the scenario
file). Floor ids must be unique and have a scale. Every cell of a scene
must be a scenario cell with the same MCC, MNC, LA, colour code and
carrier. For a bundled example plan the scenario usually has no floors:
the host supplies the plan's scene (RAD-18) and the scenario gives the
network and the antenna powers.

## Channel

| Member | Default | Meaning |
|---|---|---|
| `path_loss` | `exponent` 3.5, `reference_distance_m` 1, `reference_loss_db` = free space at d0 | Log-distance loss of site cells. |
| `penetration_loss_db` | 12 | Building entry loss of site cells. |
| `shadowing` | `sigma_db` 0, `decorrelation_m` 20 | Correlated shadowing of site cells over the plan. |
| `multiwall` | `reflections` 0 (0..2), `reflection_loss_db` 6, `slab_attenuation_db` 15, `storey_height_m` 3, `shadowing_sigma_db` 0, `shadowing_decorrelation_m` 4 | The multi-wall model of scene cells (RAD-73); a scene's floor values win. |
| `fading` | `model` none | `none`, `rayleigh`, `rician` (`k_db` 6), `static`, `TU50`, `HT200`; `sinusoids` 32; `doppler_hz` (flat fading; default from the path speed, at least `min_doppler_hz` 1). |
| `doppler_shift_hz` | 0 | A fixed shift of every carrier. |

## Receiver

| Member | Default | Meaning |
|---|---|---|
| `noise_figure_db` | 7 | AWGN `kT0 fs F`; the truth's SNR uses 18 kHz. |
| `noise` | true | false: no noise at all. |
| `reference_level_dbm` | -20 | The level of sample power 1. |
| `height_m` | 1.5 | Receiver antenna height above its floor. |
| `ppm` | 0 | LO error: every carrier moves by `-ppm x centre`. |
| `clock_ppm` | 0 | Sample-clock error: the signal is sampled at `fs (1 + clock_ppm 1e-6)` and labelled `fs`. |
| `gain_db` | 0 | Front-end gain (changed by `rx_gain` events). |
| `iq_imbalance` | `gain_db` 0, `phase_deg` 0 | |
| `swap_iq` | false | |
| `dc_dbfs` | none | DC spike relative to full scale. |
| `full_scale_dbm` | the reference level | Level of a full-scale component. |
| `clip` | false | Clip each component at full scale. |
| `quantise_bits` | 0 | 0 off, 2..16 (8 like cs8). |
| `drops` | none | `first_s`, `interval_s`, `samples`: samples lost every interval (USB overflows). |

## Path

`{ "mode": "hold" | "linear", "points": [ { "t", "floor", "x_m", "y_m" } ] }`:
receiver positions at increasing times in the plan frame of a floor.
`hold` keeps each point until the next; `linear` moves in a straight line
between points on the same floor (a floor change is a jump). Before the
first point the first is used, after the last the last. Without points
there is no position and every cell has its fixed `level_dbm`. The
simulator replaces the path by the cursor or walk position.

Level of a cell at a position: the multi-wall model when the floor's scene
has an antenna of the cell; else log-distance from its site; else its
`level_dbm`. Level events are added; an off cell has no level.

## Events

`{ "t", "type", "cell"?, "db"? }` in time order, acting from their sample
on:

- `cell_off`, `cell_on`: the cell stops or starts transmitting (its TDMA
  time runs on);
- `cell_level`: `db` is added to the cell's level from now on (replacing an
  earlier level event of the cell);
- `rx_gain`: the receiver's front-end gain becomes `db` (an AGC step).

## Ground truth

The engine records a row every `truth_interval_s`: time, signal sample,
output sample (after drops), position, noise in 18 kHz, receiver gain and
per cell its identity (MCC, MNC, LA, colour code), nominal frequency and
offset, frequency error, on-air state, level source (`fixed`, `scene`,
`site`, `off`), mean level, instantaneous level with flat fading, SNR, and
for scene cells the strongest antenna and its walls crossed. `yantra-gen`
writes these rows as CSV and record files (RAD-71).

## Bundled scenarios

| File | Use |
|---|---|
| `indoor_good_das.json` | Default. Demo office (two floors, `scenes/demo_office_f0/f1.json`) with a DAS per floor; a walk along the ground-floor corridor (DEMO-09). |
| `indoor_weak_das.json` | The same building with the DAS turned down: weak coverage (DEMO-09). |
| `floor_change.json` | A walk from the ground floor to the first floor: the serving cell changes (DEMO-09). |
| `failed_site.json` | Three sites, one announced but not on the air, one off for 2 s (DEMO-09, Yantra PP-03). |
| `weak_signal.json` | A weak cell with a noisy receiver, fading, DC, IQ imbalance and an AGC step (DEMO-09). |
| `fraunces.json` | Example plan `fraunces-*`. |
| `mak_beer_house.json`, `mak_haus_claeson.json`, `mak_villa_bunzl.json` | Example plans `mak-beer-house-*`, `mak-haus-claeson-*`, `mak-villa-bunzl-*` (Yantra DEMO-12): cell `T1` of the plan's scene and the DAS antenna powers. |

Adding a scenario: write the file, run `test_gen_sim` (every file in the
folder must parse) and `test_gen_sim --update --test-suite=sim-golden` to
add its golden digest, and give the reason in the commit message.
