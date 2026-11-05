<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Demo project and demo scenarios

Yantra ships a demo project, so the walk test, replay, statistics,
interpolation and report can be tried without a radio (DEMO-05, DEMO-06,
DEMO-13). Everything in it is simulated and labelled "DEMO - simulated
data" (DEMO-07). It needs no internet: its floor plans are bundled
(DEMO-08).

## The Fraunces demo project

The building is the Fraunces Tavern in New York, from two public-domain
sheets of the Historic American Buildings Survey (Library of Congress):
the first and the second floor ([data/demo/fraunces/README.md](../data/demo/fraunces/README.md)
has the sources, SHA-256 values and credit). The network and its DAS are
fictitious (MCC 901, MNC 1):

| Cell | Carrier | Identity | What it is |
|---|---|---|---|
| T1 | 390.0 MHz | LA 100, colour code 1 | The DAS cell: 3 ceiling antennas on the first floor, 2 on the second (10 dBm at the feed, cable and splitter losses) |
| M1 | 390.2 MHz | LA 200, colour code 2 | A macro site outside, weak indoors (about -98 dBm) |

### The description

The project is made from one file, `data/demo/fraunces/fraunces.json`
(format `yantra-demo-project`, version 1; read by
`yantra/core/demo/demo_project.h`). It holds, in plan units of each floor
unless noted:

- `project` (name, site, operator, notes), `building` (name, slab
  attenuation), `start_utc` (the walks' clock), `cell_database` (a CSV
  file next to it, [CELL_DATABASE.md](CELL_DATABASE.md)) and
  `interpolation.cell` (the cell whose level the maps show);
- `sheets`: id, file, SHA-256, the URL it was fetched from and its
  catalogue page;
- `floors`: id, `example_id` (RAD-18, e.g. `fraunces-1`), name, level,
  height, the sheet, the trim rectangle (page units of the sheet), the
  scale bar (ends and length in metres), the reference point, the walls
  (class and points), the DAS antennas (position, height, gain, feed
  power and losses, cell) and one walk route (`name`, `speed_mps`,
  `waypoints`).

### How it is made

`yantra-demo` (tools/yantra-demo) makes the project at build time
(target `demo_fraunces`, part of the default build when the radio tools
are built):

1. The project file with the building: the sheets copied in unchanged
   (their SHA-256 checked), the floors trimmed from them, with scale,
   reference point, walls, antennas and the cell database.
2. Per floor, the radios' scenario that the floor's example id selects
   (`example_ids` of `data/scenarios/*.json` in yantra-radios, here
   `fraunces.json`) gets the floor's scene as Yantra exports it for the
   simulator (RAD-36) and the walk as its receiver path (`linear`, the
   walker reaching each waypoint at the walk's speed).
   yantra-gen (RAD-71) runs it and writes its ground truth as a record
   file (RAD-65): the levels of the simulator's multi-wall model (RAD-73)
   along the walk. Its samples are discarded; no I/Q reaches Yantra.
3. The record file is replayed through the measurement pipeline on a
   clock that starts at `start_utc`, into a continuous walk test (GEO-02)
   that clicks each waypoint when the walker reaches it, with a recorder
   (REC-01). The recording has a fixed session id (the first 16 hex
   digits of SHA-256 of `fraunces/<floor id>`), so it is the same file on
   every build.
4. Each floor is interpolated with the ray tracer (IND-40..45) and the
   result is stored in the project.

The result is `build/demo/fraunces/fraunces.yantra` and
`build/demo/fraunces/recordings/<floor id>_walk_0000.yrec`, installed to
`share/yantra/demo/fraunces/`. The project lists the recordings with a
path relative to itself (`recordings/...`). By hand:

```sh
yantra-demo --description data/demo/fraunces/fraunces.json --out DIR \
  --gen build/radios/install/bin/yantra-gen \
  --scenario-dir build/radios/install/share/yantra-radios/scenarios \
  --tetra-plugin build/lib/yantra/plugins/tetra.so
```

Options: `--work DIR` (the generated scenarios and record files, default
`DIR/work`, removed afterwards unless `--keep-work`), `--truth-interval S`
(ground-truth rows per second, default 0.25 s), `--threads N` (ray
tracing; 0 = all cores).

The same description, sheets, scenario and tools give the same recording
files (byte for byte) and the same project content; only the project's
creation times differ. The tests check both
(`tests/measure_host/test_measure_demo.cpp`).

### Simulator and project agree

On a Fraunces floor the simulator (RAD-61) picks the `fraunces` scenario
by the floor's example id and computes its levels from the floor's walls
and antennas, which Yantra hands it through the context (PLG-27). At the
walk points they are the ray tracer's levels with no reflections within
0.5 dB (TST-36). The stored maps use the project's default settings (two
reflections) and the fit (IND-41), so they differ from the walk by the
fit's rms error (about 1.4 dB).

## Demo scenarios (DEMO-09)

`data/demo/scenarios.json` (format `yantra-demo-scenarios`, version 1)
lists the selectable demo scenarios: ids of the radios' scenario folder,
the items of the simulator's Scenario setting, used by the screenshot
script, the user manual and the tests.

| Id | Shows |
|---|---|
| `indoor_good_das` | A two-floor office with a DAS on each floor: strong indoor levels (the default scenario for plans without one) |
| `indoor_weak_das` | The same office with the DAS turned down: levels fall below the criteria away from the antennas |
| `floor_change` | A walk up a floor: the serving cell changes with the floor |
| `failed_site` | A site announced in the neighbour lists but off the air, and one that drops out (PP-03) |
| `weak_signal` | One weak cell near sensitivity with a noisy receiver and fading |

The example plans have their own scenarios, chosen by example id:
`fraunces` (this project) and, for the tour's MAK buildings (DEMO-12),
`mak_beer_house`, `mak_haus_claeson` and `mak_villa_bunzl`.
