<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Coverage map: interpolation between the measured points

The coverage map of a floor is an inverse-distance-weighted (IDW)
interpolation of the measured levels of that floor's walk tests on a grid
(IND-41). It uses only the measured points: no walls, antennas or
propagation model. Code: `core/include/yantra/core/survey/interpolation.h`,
`core/src/survey/interpolation.cpp`; stored as described in
[formats/INTERPOLATION.md](formats/INTERPOLATION.md).

## The map (IND-41)

The points are the shared point list of the floor (IND-30,
[SURVEY.md](SURVEY.md)): the metric's level in dBm at each point's
position in plan-frame metres. The grid covers the floor's plan rectangle
(0, 0 .. plan size in metres; a floor without a plan size: the points'
bounding box plus 1 m). For the centre `x` of every grid cell:

- a cell **holding measured points** takes their level: the median when
  several (an even count: the mean of the two middle values), so the map
  equals the measured value at every measured point's cell;
- any other cell takes, over the points `i` within the search radius `R`
  of `x`,

      value(x) = sum(w_i * v_i) / sum(w_i),   w_i = 1 / d_i ^ p

  with `d_i` the distance from `x` to point `i` and `p` the power, in dB
  (levels are averaged as dB values, not as powers);
- a cell with no point within `R` has no value (shown empty, not counted).

| Setting | Default | Range | Meaning |
| --- | --- | --- | --- |
| grid | 0.5 m | 0.05 .. 10 m | cell size |
| power `p` | 2 | 0.5 .. 6 | how fast a point's weight falls with distance |
| search radius `R` | 10 m | 0.5 .. 1000 m | farther points do not count |
| full confidence | 2 m | 0 .. < `R` | the confidence mask is 1 up to here |

The settings belong to the project (Interpolation settings dialog,
UI-19; table `interpolation_settings`, [formats/PROJECT.md](formats/PROJECT.md))
and every stored map keeps the settings it was made with (IND-45).

## Confidence mask (IND-43)

The confidence of a cell is 1 up to the full-confidence distance from the
nearest measured point and falls linearly to 0 at the search radius:

    confidence = 1                              d <= full
                 (R - d) / (R - full)           full < d < R
                 0                              d >= R

Cells whose centre is off the floor (outside its trim outline, else
outside the plan rectangle) have confidence 0. The map is drawn with the
measured points' legend and thresholds, each cell's alpha being its
confidence, so areas far from any measured point fade out.

## Readout and statistics (IND-43, EXP-06)

- Hover readout (`survey::readout`): the cell's value, whether the cell
  holds a measured point ("measured") or not ("interpolated"), its
  confidence, and the nearest measured point with its distance.
- Statistics stay apart: the point statistics (`survey::coverage_stats`,
  "Share of measured points (%)") count measured points only; the area
  statistics (`survey::area_stats`, "Share of floor area (interpolated) (%)")
  give, per threshold, the share of the assessed area at or above it.
  The **floor area** is the grid cells whose centre lies on the floor;
  the **assessed area** is those with confidence >= 0.5 (option).
  `survey::coverage_summary` gives both figures side by side and whether
  the map is stale, as the report shows them.
- Acceptance on the floor-area basis (EXP-06, `AcceptanceProfile::Basis::
  FloorArea`, [SURVEY.md](SURVEY.md)): `survey::evaluate_area` evaluates
  a profile over the maps of one or more floors, a criterion's share
  being of the assessed area of all floors together (area-weighted). An
  interpolated value never counts as a measured one.

## Stored maps and staleness (IND-45)

A map is stored in the project under a key made of its metric; a new run
with the same key replaces it (with a new id). It keeps the request, the
settings, the points used and the cells, so reports are reproducible;
reading it back gives the same map (tested). A stored map is never
changed or dropped when its inputs change: it is marked **stale**
(`survey::staleness`) with the reasons, and the UI offers to run it
again. Its SHA-256 fingerprints:

| Fingerprint | Stale when |
| --- | --- |
| building | a floor, its plan size, trim or scale changed |
| points | a measured point of its selection was added, removed, moved or changed value |
| settings | the interpolation settings or the request changed |

Project files of Yantra 0.3 and earlier held ray-tracing maps; they are
dropped when such a file is opened (IND-46) and the map is computed again
on request.

## Background job (IND-45)

`survey::InterpolationJob` runs a request on a worker thread
(`util::Job`, with the project shared): it never blocks the UI, reports
its stage (Points, Map, Storing) and a non-decreasing progress, and can
be cancelled at any time. It stores the result at the job's commit point,
after the last cancel check: a cancelled job stores nothing. The map
click of the mouse survey (DEMO-14), the Map tab and the demo projects
(DEMO-05, DEMO-13) all run it.

## Speed and SIMD

The points are sorted into square buckets of the search radius; a cell
looks at the 3 x 3 buckets around it. The squared distances to a run of
points are computed by a SIMD kernel (GCC/Clang vector extensions,
`-DYANTRA_SIMD=ON`) or plain C++; both do the same IEEE operations and
floating-point contraction is off in these files, and every cell sums its
points in a fixed order, so the map is the same bit for bit with and
without SIMD and with any thread count (TST-53).

`bench_interpolation` (tests/bench, TST-41; ctest labels slow, perf,
timing, run serially) times a 60 x 30 m floor with 2000 measured points
on the default 0.5 m grid (7200 cells) and on a 0.1 m grid (180 000
cells), one thread, SIMD and scalar. A CPU calibration line (a fixed
scalar workload) turns times into calibrated units;
tests/bench/limits.csv holds the limits and a run beyond a limit fails.
`bench_pipeline` times the record pipeline with the same calibration.

## Limits

- IDW knows nothing about walls or antennas: a level measured on one
  side of a wall spreads to the other side within the search radius.
  Walk both sides of a wall that matters.
- Between distant points the map is a weighted mean, not a prediction:
  it never exceeds the measured range, and with few points it is coarse.
  The confidence mask shows where it rests on nearby measurements.
- Levels are averaged in dB (a local median-like mean), not in power.
