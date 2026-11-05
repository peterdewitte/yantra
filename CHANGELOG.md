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

### Measurements

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
