<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Fraunces Tavern floor plans (demo project)

The floor plans and the description of the demo project (DEMO-05,
DEMO-06, DEMO-08). The plans are bundled, so the demo needs no internet.
The network, the levels and the walks of the demo project are simulated
(see [docs/DEMO.md](../../../docs/DEMO.md)).

- `fraunces.json`: the demo project description (floors, trims, scale
  bars, walls, DAS antennas, walk routes; format `yantra-demo-project`).
- `cells.csv`: the demo cell database (fictitious cells T1 and M1).
- the two plan sheets below.

| File | Sheet | Source |
|---|---|---|
| `fraunces_tavern_first_floor.png` | First floor, HABS NY-6321 sheet 3 of 15 | https://www.loc.gov/pictures/item/ny1654.sheet.00003a/ |
| `fraunces_tavern_second_floor.png` | Second floor, HABS NY-6321 sheet 4 of 15 | https://www.loc.gov/pictures/item/ny1654.sheet.00004a/ |

Fraunces Tavern Museum Complex, 54-58 Pearl Street, New York, New York
County, NY. Drawn 1976-77 for the Historic American Buildings Survey.

**Credit:** Historic American Buildings Survey, Library of Congress.
**Licence:** public domain (work of the United States federal government,
`LICENSES/LicenseRef-Public-Domain.txt`).

The files are the full-resolution sheets (14447 x 9471 and 14442 x 9191
pixels, 1-bit grey) as served unchanged by the Library of Congress image
service, for example
`https://tile.loc.gov/image-services/iiif/service:pnp:habshaer:ny:ny1600:ny1654:sheet:00004a/full/full/0/default.png`
for sheet 4, and the same URL with `sheet:00003a` for sheet 3 (fetched
with a plain request, no other data sent, on 2026-11-05). They are not
altered: the demo project cuts each floor out of its sheet with a trim.

| File | SHA-256 |
|---|---|
| `fraunces_tavern_first_floor.png` | `aeb0a76beb802def5b99160090f3d28bc607261d61d07ea2d26bfc8495285804` |
| `fraunces_tavern_second_floor.png` | `2ba0c7bf48e380d7bb3db2da7acf70bf3d977cd4ef91e0a853da05fe2a15dbcd` |

Scale: the metric scale bar (0-3 m) at the bottom right of each sheet is
984 px long on sheet 3 and 981.5 px on sheet 4 (about 328 px/m). North is
given by the arrow next to it.

The walls, antennas, trims and walk routes were drawn on these sheets
for the demo; they follow the drawn rooms but are not a survey of the
building.
