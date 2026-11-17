<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# MAK example buildings (guided tour)

The descriptions of the three example buildings of the guided tour
(DEMO-11, DEMO-12, DEMO-13). **The floor plans are not here and never
will be:** they are licensed CC BY-NC-ND 4.0 by the MAK - Museum of
Applied Arts, Vienna, which allows neither redistribution in Yantra nor
adaptation. The app downloads the unaltered image from MAK on first use,
after the user agrees, checks its SHA-256 and keeps it in the user's
cache folder. The projects reference the image by its SHA-256 (IND-03);
trims and overlays are applied at display time only.

| Description | Building | Sheet (MAK inventory number) | SHA-256 of the image |
|---|---|---|---|
| `mak_beer_house.json` | Beer house, Wenzgasse 12, Vienna 13 (1929-30), Josef Frank and Oskar Wlach | KI 23126-36, 1680 x 1565 px JPEG, 833309 bytes | `b98ccd953124e06c55252901358ba8bc4b4c2afcad09f3b4ea21df22f22373b3` |
| `mak_haus_claeson.json` | Haus Claeson, Falsterbo (1927), Josef Frank | KI 23497-359, 1274 x 1680 px JPEG, 199208 bytes | `d56bce9370653c20b4fe388c924c42cfc0c9fadfd86d206a3ec6b612de9e365e` |
| `mak_villa_bunzl.json` | Villa Bunzl, Chimanistrasse 18, Vienna 19 (1936), Josef Frank | KI 23126-41, 1680 x 1262 px JPEG, 694770 bytes | `35333c2e538c2fbcd536d1c52281ab260f9049c6e5ad070d31a5d513c9ee1a4b` |

Each description (format `yantra-demo-project`, [docs/DEMO.md](../../../docs/DEMO.md))
holds the image URL and catalogue page, its size and SHA-256, the licence
and credit ("MAK - Museum of Applied Arts, Vienna", designer, inventory
number), and per floor the trim rectangle on the sheet, name, level, an
approximate scale (marked `"approximate": true`, with how it was found),
the reference point and the demo walk route, traced for the demo. The
network (`cells.csv`, fictitious cells T1 and M1) and every measurement
are simulated.

**Credit:** MAK - Museum of Applied Arts, Vienna.
**Licence of the plans:** CC BY-NC-ND 4.0, https://creativecommons.org/licenses/by-nc-nd/4.0/.
The descriptions themselves are part of Yantra (GPL-3.0-or-later).
