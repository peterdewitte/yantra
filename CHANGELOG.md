<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Changelog

What changed in Yantra per release, for users (DOC-16), with real dates.
Changes of the plugin API are in
[docs/PLUGIN_API_CHANGELOG.md](docs/PLUGIN_API_CHANGELOG.md). The radio
plugins come from yantra-radios (git submodule `third_party/yantra-radios`,
PLG-26); every move of its pinned release is recorded here (RAD-98), its
own changes are in its CHANGELOG.md.

## [Unreleased]

### Indoor projects

- 2026-11-05: the indoor project file `*.yantra` (an SQLite database,
  documented in docs/formats/PROJECT.md, schema version 1): project name,
  site, operator and notes; the building's floors with name, level and
  height, ordered by level, renamed, reordered and removed; a copy of
  every plan source file (or, for licence-restricted plans, a reference by
  SHA-256). Every change is stored at once and survives a crash; New and
  Save as write whole files; a project is open in one Yantra at a time;
  older files are migrated after a backup, newer and damaged files are
  refused with the reason.
- 2026-11-05: plan sources (IND-02): PDF plans (any page, through a
  pinned PDFium release) and every image format stb_image reads (PNG,
  JPEG, BMP, GIF, TGA, PSD, PNM, HDR); a file that cannot be read is
  refused with the reason. Large sheets render in tiles at the detail the
  zoom needs, within a memory budget. Floors are trimmed non-destructively
  from their source (IND-05..07): a rectangle or a polygon, quarter turns
  and a fine deskew; several floors from one sheet and several sheets per
  project (IND-06, IND-10); trims are kept in the project file (schema
  version 2, older files are migrated) and can be edited later. The plan
  import window itself comes with the desktop UI.
- 2026-11-05: project schema version 4: the project lists its recording
  files (name, size, SHA-256, header) and stores its walk tests and
  manual-point sets per floor (several per floor, with name, date, radio,
  calibration and notes) and their measured points, with the plan
  position in plan units and in metres; metres follow the floor scale, a
  trim edit keeps the points where they were measured on the sheet, and
  a floor with walk tests is not removed. Older files are migrated.
- 2026-11-05: walk tests on a floor plan (docs/SURVEY.md): point and
  click and continuous mode with the samples spread evenly between the
  clicks, pause and delete last segment (GEO-02); manual points averaged
  over N seconds as power (GEO-05); a floor change in the middle of a walk
  goes on in a new session on the other floor; with the simulator, the
  walk position is where it measures. Every action is recorded, so a
  replay gives the same points. One point list per floor for the project
  window and the report, never thinned (IND-30), with coverage
  statistics: shares above thresholds, percentiles, per area, per floor
  and for the building, serving-cell changes and acceptance profiles
  (EXP-05, EXP-06, PP-02).
- 2026-11-05: the cell database (docs/CELL_DATABASE.md), kept in the
  project's cells (project schema 5): site, sector, azimuth, graph
  colour, notes, broadcast and reselection parameters and up to 31
  neighbours per cell; cells identified by LA, by channel or by both.
  CSV import and export with LA in hex or decimal and channel numbers;
  rows that cannot be read are reported by line. Received broadcasts are
  verified against it (match, differs, new, missing, per parameter and
  neighbour) with a difference log, can fill or extend it (auto-learn),
  and neighbour relations that are not mutual are found across all cells
  of a survey or project. A reselection simulator shows the serving-cell
  state and the reselection that would occur, also along a walk.
- 2026-11-05: post-processing (docs/SURVEY.md "Post-processing"):
  recordings are loaded into a project and give the same points as the
  live walk test (PP-01), with a hover readout of a point's position,
  serving and strongest cell and every neighbour (the stored points now
  keep the serving cell's announced neighbour list); good/bad against a
  threshold, serving-cell changes and the coverage rate of the tested
  area (PP-02); switching sites off and recomputing the best server and
  coverage from the measured levels (PP-03); a level offset simulation
  (PP-04); a binned view with the median or another percentile per square
  (PP-05). Acceptance profiles can be evaluated over the tested area
  besides the points (EXP-06). A serving cell on another carrier now
  counts as a serving-cell change.
- 2026-11-05: the propagation model (docs/PROPAGATION.md, IND-40,
  IND-44): a ray tracer per floor predicts each cell's level on a grid
  (default 0.25 m) from the drawn walls and antennas, with free-space
  loss, the loss of every wall crossed at the cell's carrier, specular
  reflections (default 2) and the slabs to antennas on other floors,
  summed in power per cell. It gives the levels the simulator plugin
  computes for the same floor, runs on all cores and can be cancelled.
  The coverage map built on it comes with the measured-point fit.
- 2026-11-05: the interpolated coverage map of a floor (docs/PROPAGATION.md
  "Interpolation of a floor", IND-41..45): the propagation model is fitted
  to the measured points (unknown EIRPs, path-loss exponent, wall-class
  losses and the dB per floor, within bounds; the fit quality is shown),
  the remaining differences are spread over the floor limited by walls,
  and a confidence mask fades the map away from the points. A floor
  without antennas gets estimated sources from its points, labelled as
  such. The map always says "interpolated"; its "% of floor area
  (interpolated)" stays apart from the measured-point statistics and is a
  new acceptance basis (EXP-06). Maps run in the background (progress,
  cancel) and are stored in the project with their parameters; a stored
  map is marked stale when the building, the points or the settings
  change. Project schema version 6 (propagation settings and stored maps;
  older files are migrated).
- 2026-11-05: the Fraunces demo project (DEMO-05, DEMO-06, DEMO-13;
  docs/DEMO.md): two floors of the Fraunces Tavern from public-domain
  HABS sheets (Library of Congress), with trims, scale, walls, DAS
  antennas, a fictitious cell database, one simulated walk recording per
  floor and the stored ray-tracing interpolation of each floor. It is made
  at build time by the new tool `yantra-demo` from its description
  (`data/demo/fraunces/fraunces.json`) with the radios' Fraunces scenario
  and yantra-gen, the same on every build, and installed to
  `share/yantra/demo/fraunces`. The demo scenarios (DEMO-09) are listed in
  `data/demo/scenarios.json`. Recordings can have a fixed session id, and
  a walk test can take its start time from the measurement clock.
- 2026-11-05: the three MAK example buildings of the guided tour
  (DEMO-12, DEMO-13; docs/DEMO.md): Beer house and Villa Bunzl in Vienna
  and Haus Claeson in Falsterbo, one description each in `data/demo/mak/`
  (image URL, SHA-256, size, licence and credit; per floor the trim,
  an approximate scale, walls, DAS antennas and a walk route). The MAK
  plans themselves (CC BY-NC-ND 4.0) are never shipped: the example
  projects, made at build time with one simulated walk recording and
  interpolation per floor and installed to `share/yantra/demo/mak/`,
  reference the plan by its SHA-256 (IND-03). A plan file that is not the
  described one is refused with both hashes and the download address.
  `yantra-demo` takes `--sheets DIR` (the cache) or `--reference-sheets`.

### Measurements

- 2026-11-05: a self-interference warning: raised noise next to the
  carriers or many carriers without sync for three seconds suggest that
  the antenna picks up the laptop's RF noise; the advice is to keep the
  antenna away from the PC, its USB ports and cables.

- 2026-11-04: the TETRA technology plugin (`tetra.so`): TETRA cell,
  neighbour, scan and terminal records with their own columns, the
  380/410/450 MHz channel raster, C1 (MS power 30 dBm portable, 35 dBm
  mobile) and cell reselection states. A C1 the radio does not deliver is
  calculated from the calibrated level and marked estimated.
- 2026-11-04: the TETRA channel raster covers every band the radios tune
  (350-470 MHz and 790-920 MHz, including 350-370 MHz and 800 MHz TETRA);
  channel numbers are band-qualified (band code x 10000 + carrier number)
  so one number names one frequency.

### Radio plugins (yantra-radios)

- 2026-11-06: pinned to yantra-radios **v0.2.6** (commit 5394318): the
  simulator change of 0.2.5 is taken back (plugin 0.2.4): a slowly
  simulated record has the time its position was taken again, so its
  sim_position matches its time; such records arrive late and are flagged
  late. Radio API 1.0 unchanged.
- 2026-11-06: pinned to yantra-radios **v0.2.5** (commit ba7814c): the
  simulator (plugin 0.2.3) keeps its record times on the host clock when
  the computer simulates slower than real time; before, each record came
  an interval's processing time after its time, was flagged late and
  stale, and on a loaded machine the serving carrier left the cycles
  between two records. Radio API 1.0 unchanged.
- 2026-11-05: pinned to yantra-radios **v0.2.4** (commit c41718a): the
  TETRA terminal plugin (pei 0.1.1) no longer takes the late reply of a
  stopped session as the answer to the next session's first command, so
  a self-test no longer passes a terminal that does not answer; the SDR
  tests are limited in signal time. Radio API 1.0 unchanged.
- 2026-11-05: pinned to yantra-radios **v0.2.3** (commit 547d834): the
  simulator calculates a project cell on the carrier of one of its
  default cells (390.0 or 390.1 MHz) from the drawn floor; before, the
  default cell kept the carrier and the records showed no walls. Radio
  API 1.0 unchanged.
- 2026-11-05: pinned to yantra-radios **v0.2.2** (commit a78c5c1): the
  SDR plugins (SoapySDR, HackRF) return from stop within the 1 s control
  call limit also on a slow or loaded computer; before, stopping (or a
  replug restart followed by a stop) could take seconds and the plugin
  was disabled. Radio API 1.0 unchanged.
- 2026-11-04: pinned to yantra-radios **v0.2.1** (commit f359ee1): the
  simulator plugin measures every cell of its scenario's capture, not only
  the carrier at the capture centre, and a carrier pinned twice (a given
  serving carrier) gets one decoder, so one record per carrier and
  interval; radio API 1.0 unchanged.
- 2026-11-04: pinned to yantra-radios **v0.2.0** (commit 205ca7a): scene
  format 1.1 (per-band wall attenuation, storeys with their own height and
  slab attenuation, wall polylines whose vertex is crossed once); radio API
  1.0 unchanged, so every radio plugin keeps loading.
- 2026-11-04: first pin, yantra-radios **v0.1.0** (commit fb194f5): radio
  API 1.0 with the HackRF, generic SoapySDR, simulator and TETRA terminal
  plugins.
