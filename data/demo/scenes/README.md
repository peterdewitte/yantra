<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Fixed demo scenes

The simulator scene of every bundled example floor (DEMO-05, DEMO-12,
DEMO-13, PLG-27): one file per floor, named by the floor's example id
(`<example id>.json`), in the scene format of yantra-radios (RAD-36:
`"format": "yantra-radio-scene"`, `"version"` and `"minor_version"`;
yantra-radios docs/SCENE_FORMAT.md). Each holds the floor's walls with
their attenuations, the DAS antennas and their cells, the storeys with
antennas above or below and the slab losses, in metres in the floor's
plan frame (the trim and scale of the example description).

| Files | Example floors (description) |
|---|---|
| `fraunces-1.json`, `fraunces-2.json` | Fraunces Tavern demo project (`../fraunces/fraunces.json`) |
| `mak-beer-house-1.json` .. `-4.json` | Beer house, Vienna (`../mak/mak_beer_house.json`) |
| `mak-haus-claeson-1.json` .. `-4.json` | Haus Claeson, Falsterbo (`../mak/mak_haus_claeson.json`) |
| `mak-villa-bunzl-1.json` .. `-3.json` | Villa Bunzl, Vienna (`../mak/mak_villa_bunzl.json`) |

They are used by the simulator only and never shown or edited in Yantra:

- the app gives the shown floor's scene to the simulator through the
  radio host's `get_scene` when the floor has an example id with a scene
  file here; a user's own floor has no scene and the simulator uses its
  own defaults;
- the demo builds (`yantra-demo --scene-dir`) put the scene of each floor
  into the yantra-gen scenario of its walk, whose recordings are shipped
  (DEMO-13).

The files were exported once (2026-11-17) from the example descriptions
with the scene exporter of Yantra 0.2.0:

    yantra-demo --export-scenes data/demo/scenes --description data/demo/fraunces/fraunces.json

(likewise for the three MAK descriptions). The ctests `demo_scenes_*`
check that they are still that export byte for byte; they contain no
plan image data. Licence: GPL-3.0-or-later (REUSE.toml).
