<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Walk tests, measured points and coverage statistics

How an indoor walk test turns measurement cycles into points on a floor
plan, and how the project window and the coverage report get the same
points and statistics from them (IND-20, IND-30, GEO-02, GEO-05, GEO-06,
EXP-05, EXP-06, PP-02, TST-32, TST-34). Code: `core/survey/`
(namespace `yantra::survey`). The points are stored in the project file
(schema 4, [formats/PROJECT.md](formats/PROJECT.md) `sessions` and
`points`). The interpolated coverage map (IND-40..45) is not covered
here.

## Walk test

A walk test (`WalkTest`, walk_test.h) is a measurement observer
([MEASUREMENT_PIPELINE.md](MEASUREMENT_PIPELINE.md)) on one floor. It is
one session of the project (`sessions` row: name, date, radio,
calibration, notes, mode). A floor can have several walk tests. Every
measurement cycle (1 s by default) is at most one point. A cycle that
has no plan position is not stored.

Every action has a time on the pipeline clock, the same clock as the
cycles' `time.monotonic_ns`.

| Kind | What the surveyor does | Where a cycle at time T goes |
| --- | --- | --- |
| `walk`, point and click (GEO-02) | click the start point, walk, click the end point; the next click starts a new segment | on the segment from click A (tA, pA) to click B (tB, pB) when tA <= T <= tB: p(T) = pA + (T - tA) / (tB - tA) (pB - pA), so samples at a steady rate are spread evenly. A cycle at a click lies on the clicked point. |
| `walk`, continuous | every click ends the open segment and starts the next | the same; a cycle at a joint is placed once, on the segment it ends |
| `manual` (GEO-05) | click a chosen point (e.g. a room) and stand there N seconds | the cycles with T in (t, t + N] (at least the first cycle after t) become one point at the click, averaged (below) |
| `simulated` | nothing: the simulator's position is used (RAD-19) | at the cycle's simulator position (`sim_position` fields) when it is on the walk's floor |

The rules of a walk:

- Cycles after an open start wait until the closing click places them.
  Cycles before the first click, after the end click (point and click),
  and during a pause get no position.
- Pause drops the open segment and its waiting cycles. The next click
  starts anew.
- Delete last segment removes the last segment's points from the
  project. Segment numbers (`points.grp`) are never reused.
- Changing the mode drops an open start.
- Clicks must not go back in time.

The walk position: every click, and every walker position without a
click (`WalkTest::walker`), is published through the walker callback,
e.g. to `context::BuildingContext::set_walk`. The simulator radio plugin
follows it (setting `position_source`: walk, or auto while a walk runs).
A simulated walk therefore measures where the surveyor clicked.

### Floor change (GEO-06)

`change_floor` ends the session. A new session on the other floor
continues the walk (`sessions.continues` = the previous session). An open
segment is dropped, because a segment cannot cross floors: the first
click on the new floor is the new start. With a recorder, the change is
also written as a `floor` line.

### Recording and replay

With a recorder (REC-01), every action is written as a `walk` line
([formats/RECORDING.md](formats/RECORDING.md)). A replay sets the
pipeline clock to the recorded times. It emits each `walk` line before
the cycle with the same time. A `WalkTest` fed through `WalkReplay`
(walk lines) and the replay pipeline (cycles) therefore stores the same
points, in the same order, with the same values as the live walk test.

## Sample values

`points.sample` holds the values of the point's cycle as JSON
(`sample_json`):

```json
{"cycle": 12, "flags": "",
 "carriers": [{"dev": "simulator:0", "tech": "tetra", "schema": "tetra_cell",
               "role": "serving", "f": 390200000, "level": -71.4, "unit": "dBm",
               "c1": 22.6, "c1_src": "estimated",
               "quality": [{"field": "mer_db", "unit": "dB", "value": 28.1}],
               "cell": {"colour_code": "1", "la": "100", "mcc": "901", "mnc": "1"},
               "sim": {"floor": "floor-1", "x_m": 6.0, "y_m": 5.0},
               "flags": "", "age_ms": 120.0}]}
```

There is one carrier object per sample of the cycle that has a level, a
C1 or a quality value. Neighbour-list entries without a level are left
out. The fields:

- `role`: `serving`, `neighbour`, `other`, `scan` or `neighbour_entry`.
- `unit`: `dBm`, or `dBFS` for an uncalibrated level.
- `cell`: the technology schema's key fields without the frequency (the
  cell identity, PLG-01: the core does not know which fields these are).
- `sim`: the simulator's position (RAD-19).
- `flags`: the sample flags as text.
- `age_ms`: the age of the value at the cycle time.

A manual point's sample is `average_samples` of its cycles. It adds
`cycles` and, per carrier, `n` (in how many cycles the carrier was
present).

## Manual points

GEO-05 points are averaged per carrier (the same device, schema, role,
frequency and entry):

- Values in dBm, dBFS or dB (levels, C1, SNR, MER) are averaged as
  power: 10 log10(mean(10^(x/10))). Over a few seconds at one place the
  level fades around its local mean power, and the power mean estimates
  that power. A dB mean would be biased low by the fading dips. C1 is the
  level minus a constant, so the power mean of C1 is the C1 of the mean
  level.
- Every other unit is averaged arithmetically.
- A carrier that is in dBm in some cycles and in dBFS in others keeps
  only the values in its first unit.
- The newest cell identity and simulator position are kept, with the
  union of the flags and the largest age.

## Shared point list

`point_list(project, {floor, sessions, metric, scale})` is the one
function that both the project window and the coverage report call
(IND-30, TST-34):

- It returns every stored point of the selected sessions (default: all
  of the floor's), in a fixed order: sessions by id, points by sequence.
- No decimation, thinning, binning or re-sampling is done. When a page is
  dense, markers are drawn smaller, never dropped.
- A point without a value for the metric is still a point. It gets the
  no-value colour.
- Values come from the stored sample, and colours from the list's colour
  scale. Callers never recompute either.
- `point_counts(project)` gives the number of points per floor, which
  the window's floor tabs and the report state.

A metric chooses:

- the carrier: serving, strongest (highest level), or the carrier on one
  frequency (its serving sample first, else the strongest);
- the value: level, C1 or a quality field (PP-02 views: RSSI, MER/BER).

Default colour scales:

| Unit | Bands (from) |
| --- | --- |
| dBm | -100, -90, -80, -70 (red, orange, yellow, green); below -100 dark red |
| dBFS | -70, -60, -50, -40 |
| dB | 0, 5, 10, 20 |
| % | lower is better: <= 1, 2, 5, 10 (green to red); above 10 dark red |

Binning (PP-05) will be an extra, labelled view built from this list,
never a replacement.

## Coverage statistics

`coverage_stats`, `floor_stats` and `building_stats` (stats.h) work on
the shared list:

- **Which points count**: every point of the selection. A point without
  a value for the metric, or with a value in another unit (e.g. an
  uncalibrated dBFS point in a dBm list), is counted but is in no share.
  For coverage it counts as not covered.
- **Threshold shares** (EXP-05 good/bad, PP-02 coverage rate):
  `at_or_above` = points with value >= threshold, `below` = points with a
  value < threshold, both in percent of ALL points of the selection.
- **Percentiles**: linear interpolation between closest ranks (R-7,
  spreadsheet PERCENTILE.INC). For sorted values x[0..n-1],
  h = (n - 1) p / 100 and P = x[floor h] + (h - floor h)
  (x[floor h + 1] - x[floor h]).
- **mean** is the arithmetic mean of the values. **power_mean** is
  10 log10 of the mean power, for dB units only.
- **Per area**: an area is a polygon in plan units (a room, a wing). Its
  points are those inside it by the even-odd rule, taken from the same
  list.
- **Per floor and building**: a floor's statistics cover the floor's
  list. The building summary covers all floors' points together.
- **Serving-cell changes**: in list order within each session, a point
  whose serving cell differs from the previous point that had one.
  Serving cells are also given as shares of all points.
- **Acceptance profiles** (EXP-06): criteria "at least X % of the points
  >= Y" in a unit. A criterion passes when its share >= X. A profile
  passes when all its criteria pass. A selection without points does not
  pass.
