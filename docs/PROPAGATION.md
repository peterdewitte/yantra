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
the residual map with its confidence mask, readout and floor-area
statistics (IND-43) and the background job with its stored result
(IND-45) build on this model: "Interpolation of a floor" below
(`core/include/yantra/core/raytrace/fit.h`, `raytrace/residuals.h`,
`survey/interpolation.h`, `util/job.h`).

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
  at the grid cell centres it equals the map (tested to 1e-9 dB). The
  fit of IND-41 uses it.
- `RunControl`: the inner-loop ISA (SIMD or scalar), threads (default all
  hardware threads), a cancel flag that may be set from any thread
  (`cancelled` in the result), and a progress callback (0 .. 1,
  non-decreasing, at most about 200 calls, on the calling thread).
  Results do not depend on the thread count or the ISA. The background
  job of IND-45 drives it.
- Exceptions: `std::invalid_argument` for invalid parameters, an unknown
  floor or wall class, a group without a frequency or a grid beyond
  `kMaxGridCells`; `std::logic_error` for a receiver floor without scale.

## Interpolation of a floor

The measured levels of a floor and the model are combined in five steps
(`survey::interpolate`, the job `survey::InterpolationJob`):

1. **Points.** The measured points come only from the shared point list
   (`survey::point_list`, IND-30, [SURVEY.md](SURVEY.md)): the same
   points, values and count as the project window and the report. Points
   with a level in dBm and a position in metres are used; the metric is
   a level (`Metric::Value::Level`) of the serving, strongest or a chosen
   carrier.
2. **Sources** (only when no antenna reaches the floor, IND-42).
3. **Fit** of the model parameters (IND-41).
4. **Map**: `compute_map` with the fitted parameters.
5. **Residuals** interpolated over the map, and the confidence mask.

The start parameters are the project's propagation settings
(`Project::propagation()`, table `propagation`,
[formats/PROJECT.md](formats/PROJECT.md)), the building's wall-class
table and dB per floor. Each source is traced at its cell's carrier; a
source without one, and estimated sources, use the measured carrier: the
request's frequency, else the metric's carrier, else the median carrier
of the points. The value compared is the level of the cell on the
metric's carrier when the metric names a carrier and an antenna serves
that cell (group `cell:<id>`), else the strongest group at each point.

The result is always labelled "interpolated" ("interpolated, estimated
sources" with estimated sources) and kept apart from the measured values
(IND-43).

### Fit to the measured points (IND-41)

`raytrace::fit_model` fits, by least squares on the level in dB:

- the EIRP of every antenna whose EIRP is not known (no EIRP and no feed
  power; option: all or none), bounds -30 .. 60 dBm;
- the path-loss exponent n, bounds 1.5 .. 6;
- the loss of every wall class at the source's frequency, bounds 0 .. 2 x
  the table value + 10 dB (at most 100 dB), or bounds given per class;
- the dB per floor S of the inter-floor term (IND-44), when a measured
  point depends on an antenna of another floor, bounds 0 .. 60 dB; the
  start is the building's value (a floor's own slab value where set) and
  the fitted value replaces both in the model.

Walls with their own attenuation, the reflection loss and the reference
distance stay as set. A parameter no measured point depends on (an
antenna that reaches no point, a class no ray crosses, no slab between
the points and an antenna) is not fitted and listed with the reason.

The rays to the points are traced once (`trace_points`). Every ray's
level in dB is linear in the parameters (EIRP +1, exponent
-10 log10(d/d0) beyond d0 and 0 within, wall class -crossings, dB per
floor -slabs), and a point's level is the power sum of its group's rays,
so the derivative of a point's level is the power-weighted mean of its
rays' coefficients: exact and cheap.

The solver is Levenberg-Marquardt with Marquardt's scaling (damping
lambda x diag(J'J), start 1e-3, /10 after an accepted step, x10 after a
rejected one) and bounds: a step is clamped into the bounds, and a
parameter on a bound whose gradient points outwards is frozen for that
step. The start is deterministic: the start values (the antenna's EIRP or
the default EIRP, the project's exponent, the table losses) clamped into
the bounds, then the fitted EIRPs moved together by the mean residual. It
stops when

- an accepted step changes no parameter by more than 1e-9,
- an accepted step lowers the cost by less than 1e-14 of it,
- the cost (sum of squared errors) is below 1e-20 dB^2 (exact fit),
- the damping exceeds 1e12 (no step lowers the cost: a minimum within
  the bounds), or the projected gradient is zero,
- or after 200 iterations (option), or when cancelled.

The fit quality, shown and reported, is the root mean square of (model -
measured) over the used points and their count, plus the mean error, the
largest error and the rms before the fit. The reported values come from
`evaluate` with the fitted parameters, the model the map uses.

Tests (TST-36, tests/raytrace/test_fit.cpp): on the office floor, levels
made by the ray tracer at known parameters (other wall losses, EIRPs 13
and 7 dBm, exponent 2.6) are fitted from the default table and unknown
EIRPs: without reflections the exponent comes back within 1e-6 and the
EIRPs and wall losses within 1e-4 dB, rms below 1e-6 dB; the dB per floor
of a two-floor building within 1e-4 dB. With 2 reflections and a seeded
correlated shadowing field (5 m lattice), over eight seeds: rms 0.5 ..
1.1 sigma, the fitted model within 2/3 sigma rms of the noise-free truth,
exponent within 0.35, wall losses within 2 dB and single EIRPs within 6
dB (correlated shadowing trades the EIRPs of nearby antennas against the
exponent). SIMD and scalar give the same fit. Against the simulator
(tests/measure_host/test_measure_raytrace.cpp): the simulator's
calibrated levels at 13 points of a floor with other wall losses and
EIRP than the plan's give the EIRP back within 0.5 dB and the concrete,
brick and glass losses within 1 dB (measured: 0.05 dB and 0.001 dB).

### Estimated sources (IND-42)

When no antenna reaches the floor (none on it, and none on other floors
when they count), `raytrace::estimate_sources` places sources from the
measured levels alone, one at a time:

- a candidate grid over the floor (spacing: the larger floor side / 32,
  at least 0.5 m, or set), then four finer 5 x 5 rounds around the best
  candidate (spacing halved each round, down to 1/16);
- for each candidate the EIRPs of all sources are fitted (direct rays,
  the project's exponent and wall losses) and the candidate with the
  lowest rms wins; the first in grid order wins a tie (deterministic);
- after a source is added, every kept source is searched again over the
  whole floor with the others fixed (at most two passes), because the
  first ones were placed without the later ones;
- a further source is kept only when it lowers the rms by at least 1 dB;
  at most 3 sources (options).

By default the sources serve one cell (`estimated`, at the measured
carrier) and their levels add up in power (a DAS); as an option each is
a source of its own and the strongest counts. The sources (antennas
`estimated-1`, ... at 2.7 m with their fitted EIRP) are kept with the
result, never written into the building model, and the map is labelled
"estimated sources". The exponent and wall losses are then fitted with
the sources in place.

### Residual interpolation (IND-41)

The residual of a measured point is measured - model. It is spread over
the floor by inverse-distance weighting limited by walls:

    correction(x) = sum w_i r_i / (w_0 + sum w_i),   w_i = 1 / d_i^p,   w_0 = 1 / R^p

over the points i within R (10 m) of x that x sees without crossing a
wall of the floor (the ray tracer's wall test), p = 2. At a measured
point the correction is its residual (the mean of points at the same
place), so the map agrees with the measured points there. Away from the
points, w_0 pulls the correction back to 0: the fitted model alone. The
value of a cell is model + correction at its centre; a cell no source
reaches has no value. Tests check hand-computed values (e.g. 2 m from
two points with residuals 4 and -1.5 dB: (1 - 0.75) / 0.76 dB).

### Confidence mask (IND-43)

The confidence of a cell fades with the distance to the nearest measured
point it sees (walls limit it as above): 1 up to 2 m, 0 from 10 m,
linear between (options). Cells off the floor (outside its trim outline)
have confidence 0. The colour layer uses the measured points' legend and
thresholds (`survey::cell_colours`: the scale colour of the value with
alpha = confidence), so unmeasured areas fade out.

### Readout and statistics (IND-43, EXP-06)

- Hover readout (`survey::readout`): the value at the position, and
  whether it is measured (the position is within half a cell of a
  measured point: that point's measured value) or interpolated (the
  cell's value), the cell's model value and confidence, and the nearest
  measured point with its distance.
- Statistics stay apart: the point statistics (`survey::coverage_stats`,
  "% of measured points") count measured points only; the area
  statistics (`survey::area_stats`, "% of floor area (interpolated)")
  give, per threshold, the share of the assessed area at or above it.
  The **floor area** is the grid cells whose centre lies on the floor:
  inside its trim outline (rectangle or polygon), else inside the plan
  rectangle (`survey::floor_cells`); the **assessed area** is those with
  confidence >= 0.5 (option). `survey::coverage_summary` gives both
  figures side by side with the fit quality and whether the result is
  stale, as the report shows them.
- Acceptance on the floor-area basis (EXP-06, `AcceptanceProfile::Basis::
  FloorArea`, [SURVEY.md](SURVEY.md)): `survey::evaluate_area` evaluates
  a profile over the maps of one or more floors, a criterion's share
  being of the assessed area of all floors together (area-weighted). The
  point statistics give such a profile as not passed, with a note: an
  interpolated value never counts as a measured one.

### Stored results and staleness (IND-45)

A result is stored in the project (table `interpolations`, schema 6,
[formats/PROJECT.md](formats/PROJECT.md)) under a key made of the metric
and the source group; a new run with the same key replaces it (with a
new id). It keeps everything needed to make it again and to report it:
the request (metric, sessions, frequency, grid, fit, residual and
source-search options), the model parameters before and after the fit,
the fit quality and parameters, the estimated sources, the used points
with their model value and residual, the number of cells on the floor
and the map cells (value, model, confidence). Reading it back gives the
same result (tested).

A stored result is never changed or dropped when its inputs change; it is
marked **stale** (`survey::staleness`) with the reasons, and the UI
offers to run it again. It keeps three SHA-256 fingerprints of its
inputs:

| Fingerprint | Stale when |
| --- | --- |
| building | a wall, antenna, cell, wall class, floor, scale, alignment, slab loss or the dB per floor changed (any floor: other floors contribute) |
| points | a measured point of its selection was added, removed, moved or changed value |
| settings | the propagation settings or the request's parameters changed |

### Background job (IND-45)

`survey::InterpolationJob` runs a request on a worker thread
(`util::Job`, with the project shared): it never blocks the UI, reports
its stage (Points, Sources, Fit, Map, Residuals, Storing) and a
non-decreasing progress, and can be cancelled at any time. It stores the
result at the job's commit point, after the last cancel check: a
cancelled job stores nothing, a finished one is stored. The project's
change notification of the store comes on the worker thread. The same
request gives the same map with any thread count and with or without
SIMD (tested).

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

`bench_interpolation` times, on the same floor with 400 measured points
and one thread, the fit (rays to the points with 2 reflections plus the
solver: 0.17 s), the residual field with its mask on the 0.25 m grid
(0.13 s SIMD, 0.31 s scalar, equal bit for bit) and the source search of
IND-42 with a 4 m candidate step (3.6 s). `bench_pipeline` times the
record pipeline (RF-01): 1.6 million records of 8 carriers through the
measurement pipeline with 4000 cycles, 0.97 s. Both use the same
calibration and limits file.

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
- The fit is a local least-squares fit: with few points or correlated
  shadowing, single EIRPs of nearby antennas trade against the exponent
  (the fitted model stays close; the parameters may not). Parameters no
  point depends on are not fitted and are listed.
- Estimated sources use direct rays only and the best-candidate search;
  they stand in for unknown antennas, labelled as such, and are never
  written into the building model.
- The residual map is inverse-distance weighting limited by walls; it
  does not model the shadowing's correlation (no kriging).
