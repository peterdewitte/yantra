<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Propagation model: ray tracing per floor

Yantra predicts the level over a whole floor from the building model
([BUILDING_MODEL.md](BUILDING_MODEL.md)): the walls drawn on the plan with
their wall classes (IND-11) and the antennas with the cell they serve
(IND-12). The model is a ray tracer per floor (IND-40) with an
inter-floor term (IND-44), own code without any extra library, in
`core/include/yantra/core/raytrace/model.h`. It is a prediction model on
the building, not signal processing: no IQ, no decoding (the radio
boundary of the radios' plugins is untouched). It works in dBm at each
cell's carrier frequency.

The fit to the measured points (IND-41), the estimated sources (IND-42),
the confidence mask and readout (IND-43) and the background job with its
stored result (IND-45) build on this model in later steps; the hooks they
use are listed under "Results and use".

## What is traced

For one floor (the receiver floor) a grid of square cells covers the
floor's plan rectangle (0, 0) .. (plan width, plan height) in plan-frame
metres (default 0.25 m; without a plan size the box of its walls and
antennas plus 1 m). Plan-frame metres are the plan units times the
floor's scale, without its reference point: the frame of the walk points
(IND-20) and of the scene the radio plugins read (RAD-36). From every
antenna, rays go to the centre of every cell:

- the **direct ray**;
- **specular reflections** on the walls of the receiver floor, up to a set
  order (default 2, at most 3), found with the image method: the antenna
  is mirrored across a wall segment, that image across the next, and so
  on. A reflection counts only where it really happens on the segment (the
  ray hits the segment, the receiver is on the same side as the arriving
  ray). Walls are two-sided reflectors.

Each ray loses (all in dB):

| Term | Formula |
| --- | --- |
| distance | PL(d) = FSPL(max(d, 0.01 m)) for d <= d0, FSPL(d0) + 10 n log10(d / d0) beyond; d = unfolded 3D length of the ray (2D path through the reflection points, and the height difference) |
| free space | FSPL(d) = 20 log10(4 pi d f / c), c = 299 792 458 m/s, f = the cell's carrier |
| walls | the sum of the losses of every wall crossed on every leg, at f: the wall's own value, else its class's loss at f from the class table (linear in log f between the class's bands, IND-11) |
| reflections | per reflection the class's `reflection_loss_db`, else `reflection_loss_db` of the parameters (6 dB) |
| pattern | the antenna pattern loss towards the first leg (`pattern_gain_db`, IND-12) |
| floors | for an antenna on another floor: the slab losses between the floors (IND-44) |

The level of a ray is EIRP - loss, in dBm (EIRP: a fitted value, else
`Antenna::effective_eirp_dbm()`, else `default_eirp_dbm`, listed in the
result). The rays of all antennas serving the same cell are **summed in
power** (incoherently, no phase): P = 10 log10(sum 10^(level/10)). The map
holds this level per cell and grid cell, and the strongest cell per grid
cell.

The crossing rules of a wall are those of `building::walls_crossed()`
([BUILDING_MODEL.md](BUILDING_MODEL.md)): a ray end on a wall (the
reflection point on its reflecting wall, a receiver on a wall) does not
cross it; a path through a polyline vertex crosses that wall once; a
path along a wall does not cross it.

### Groups and frequencies

Antennas are grouped by their cell (key `cell:<id>`); an antenna without
a cell is its own group (`antenna:<id>`). Each group is computed at its
cell's carrier (`Cell::carrier_hz`), so a map holds TETRA cells on
different carriers side by side. `ModelParams::frequency_hz` sets one
frequency for every group instead; a group without a carrier needs it.

### Inter-floor term (IND-44)

Antennas on other floors contribute when `other_floors` is on (default).
Their position is taken through the floor alignment (IND-09: building
metres, each floor's reference point) into the receiver floor's plan
frame; their height is the floor-to-floor heights in between plus their
antenna height, so the distance is 3D. The ray is traced on the receiver
floor's walls only, and it loses per floor boundary the slab value of
`Building::inter_floor_attenuation_db` (the lower floor's
`slab_attenuation_db`, else the project's **dB per floor**,
`floor_attenuation_db`, default 15 dB). A fitted `floor_loss_db` replaces
both. Antennas on floors without scale are left out and listed.

### Same model as the simulator (RAD-73)

The simulator plugin of yantra-radios computes its indoor levels with the
multi-wall model of its docs/CHANNEL_MODEL.md (RAD-73): free space over
the 3D distance (held at 0.01 m or more), the walls crossed at the
carrier, the slabs, the pattern, with the receiver 1.5 m above the floor
and no reflections by default. With 0 reflections, exponent 2 and the
same receiver height the ray tracer gives those levels: TST-36 compares
it with the simulator's calibrated records on a drawn floor
(tests/measure_host/test_measure_raytrace.cpp, within 0.5 dB, the
records' own spread). The fit of IND-41 can therefore recover the drawn
EIRP and wall losses from simulated walks.

## Parameters

`raytrace::ModelParams` is a plain struct; `validate()` lists every value
outside its range. Parameters marked [trace] change the rays; the others
change only the levels, so a fit varies them on traced rays.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `frequency_hz` | each cell's carrier | > 0 | one frequency for every group |
| `grid_m` [trace] | 0.25 m | 0.05 .. 10 | grid cell size |
| `max_reflections` [trace] | 2 | 0 .. 3 | highest reflection order |
| `rx_height_m` [trace] | 1.5 m | 0 .. 10 | receiver height above its floor |
| `other_floors` [trace] | on | | antennas on other floors contribute |
| `path_loss_exponent` | 2 (free space) | 1 .. 8 | n of the distance loss beyond d0 |
| `reference_distance_m` | 1 m | 0.01 .. 100 | d0 |
| `reflection_loss_db` | 6 dB | 0 .. 60 | loss per reflection off a class without its own |
| `floor_loss_db` | unset | 0 .. 100 | replaces the slab values: dB per floor boundary |
| `wall_class_loss_db` | class table | 0 .. 100 | loss per class at every frequency (fitted); walls with their own value keep it |
| `antenna_eirp_dbm` | antenna | -50 .. 80 | EIRP per antenna id (fitted) |
| `default_eirp_dbm` | 10 dBm | -50 .. 80 | EIRP of an antenna without one |

The 6 dB reflection loss is an estimate for concrete and brick at UHF
(reflection coefficient about 0.5), as in RAD-73.

## Results and use

- `compute_map(building, floor, params, run)`: the map of a floor: the
  grid, the groups, the level per group and grid cell, the best level and
  group per cell (`kNoLevel`, -1 where no antenna reaches), the antennas
  used, skipped (with the reason) and given the default EIRP, and the
  number of rays.
- `trace_points(building, floor, points, params, run)`: the geometry of
  every ray to a list of points (length, reflections, reflection losses
  by class, pattern loss, slabs, wall crossings per class).
  `evaluate(building, rays, params)` turns it into levels for any EIRP,
  exponent, wall-class, reflection and floor loss without tracing again;
  at the grid cell centres it equals the map (tested to 1e-9 dB). This is
  the hook for the fit of IND-41.
- `RunControl`: the inner-loop ISA (SIMD or scalar), threads (default all
  hardware threads), a cancel flag that may be set from any thread
  (`cancelled` in the result), and a progress callback (0 .. 1,
  non-decreasing, at most about 200 calls, on the calling thread).
  Results do not depend on the thread count or the ISA. This is the hook
  for the background job of IND-45.
- Exceptions: `std::invalid_argument` for invalid parameters, an unknown
  floor or wall class, a group without a frequency or a grid beyond
  `kMaxGridCells`; `std::logic_error` for a receiver floor without scale.

## Speed and SIMD

Images whose reflection can never reach the floor are dropped early (the
part of each wall a parent image can reach through its own reflection
range is kept as an aperture); each image's beam is scanned row by row
over the grid. For every leg of a ray a bounding-box test picks the walls
to test; it has a SIMD path (GCC/Clang vector extensions,
`-DYANTRA_SIMD=ON`) and a plain C++ path. Both select the same walls, the
exact crossing test is shared and floating-point contraction is off in
the ray tracer's files, so both give the same levels bit for bit; the
tests check the two paths, thread counts, and a digest of a whole office
map that every build tree (default, scalar, sanitizers) must reproduce.

`bench_raytrace` (tests/bench, TST-41; ctest labels slow, perf, timing,
run serially) times the office floor of tests/raytrace/office.h (60 x
30 m, 120 walls, 4 DAS antennas, 0.25 m grid = 28 800 cells, 2
reflections, about 8.8 million rays) on one thread, SIMD and scalar. A
CPU calibration line (a fixed scalar workload) turns times into
calibrated units; tests/bench/limits.csv holds the limits (twice the
units measured when set) and a run beyond a limit fails. Measured on the
build machine (12 hardware threads, RelWithDebInfo, one thread): 4.2 s
with SIMD, 7.1 s scalar (the default run uses all hardware threads).

## Limits

- 2D floor geometry with straight vertical walls; no diffraction around
  wall ends or door frames, no scattering, no ceiling or floor
  reflections, no reflections from other floors.
- Powers add incoherently: no fast fading or standing waves; the map is a
  local mean.
- The reflection loss does not depend on the angle.
- Reflection orders above 3 are refused: the number of images grows with
  (walls) ^ order.
- A floor without scale cannot be traced.
- Outdoor donor directions (IND-12) point at an outdoor site, not at a
  radiator on the floor: they are not traced (listed in `skipped`).
