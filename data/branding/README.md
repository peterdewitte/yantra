<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# curtat.be logo

Yantra is a curtat.be reference project (BRD-01). The About dialog and the
reports show this logo (BRD-02, BRD-03).

| File | Use |
|---|---|
| `curtat_logo.svg` | Master (64x64 viewBox); follows the light/dark colour scheme through CSS. The HTML report may embed it. |
| `curtat_logo_light_{64,256,1024}.png` | For light backgrounds (transparent). |
| `curtat_logo_dark_{64,256,1024}.png` | For dark backgrounds (transparent). |

The app (stb_image) and the PDF reports (libharu) use the PNGs and pick the
variant that matches the theme or page background; no SVG library is
linked (spec 16.2). The PNGs are renders of the SVG, supplied with it.

Licence: the logo files are a trademark of Curtat BV and are NOT covered by
Yantra's GPL-3.0 licence (BRD-04). See
`LICENSES/LicenseRef-Curtat-Trademark.txt` and `THIRD_PARTY.md`. Forks must
remove or replace them. This README itself is GPL-3.0-or-later.
