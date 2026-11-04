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
