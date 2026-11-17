<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
Copied from yantra-v2 68ea20a: docs/PLANS.md, adapted for IND-02, IND-05..07, IND-10.
-->

# Floor plans: sources, trims and coordinates

An indoor project (`*.yantra`, [formats/PROJECT.md](formats/PROJECT.md))
keeps the plan files it uses and cuts its floors out of them. One sheet
can hold several floors (every floor and a cross-section on one page),
and a project can use several sheets (IND-02, IND-05..07, IND-10).

## Supported plan files

| Format | Notes |
| --- | --- |
| PDF | Any page. Each page is shown as printed (after its rotation). A page list with thumbnails is shown for files with several pages. Password-protected PDFs are refused: save an unprotected copy first. |
| PNG, JPEG, BMP, GIF, TGA, PSD, PNM (`.pgm`, `.ppm`), HDR, PIC | Everything stb_image reads: grey, grey with alpha, colour, 16-bit samples, HDR (tone mapped). Transparent areas show as white paper. GIF: the first frame. PSD: the flattened image. A JPEG photographed with a phone is turned upright (EXIF orientation). |

Not supported yet: TIFF and WebP (save the plan as PDF or PNG), SVG and
CAD drawings (export them to PDF). A file that cannot be read is refused
with the reason, and it is not kept in the project.

Limits: images up to 256 megapixels (for example an A0 sheet scanned at
600 dpi); PDF pages of any size. Large sheets (more than 15000 pixels) are
shown in tiles rendered in the background at the detail the zoom needs,
so the window stays responsive.

PDF support comes from PDFium (a pinned release, see `THIRD_PARTY.md`).
A Yantra built without it says so when a PDF is opened.

## Adding floors

(The window is part of the desktop UI, a later phase; the steps below
are implemented in its model, `yantra::plan::PlanImport`,
core/include/yantra/core/plan/import.h.)

File > Add floor (or "+" next to the floor tabs) asks for a plan file or
a sheet the project already uses, then opens the plan import window on the
whole page (zoom with the mouse wheel, pan by dragging):

1. Choose the page (PDF).
2. Turn the sheet by 90 degree steps if it lies on its side, and
   straighten a scanned sheet with the fine deskew: click two points on a
   wall or the sheet's border that should be horizontal or vertical.
3. Draw the floor: a rectangle, or a polygon for irregular outlines, so
   legends, title blocks, cross-sections and other floors stay out.
4. "Add as floor": give the name and level (for example "Ground floor",
   0). The window stays open for the next floor on the same sheet; floors
   already added are outlined and labelled, and can be edited or removed.

Trims never change the plan file. A floor stores which sheet and page it
comes from and the trim (outline, quarter turns, deskew), so the import
window can show and adjust the trim later.

## Scale and alignment

Scale per floor (IND-08) and the alignment point (IND-09) are set on the
trimmed floor in plan units and described with the building model in [BUILDING_MODEL.md](BUILDING_MODEL.md).

When a trim is edited later (IND-07: another outline, page, rotation or
deskew), everything drawn on the floor moves with it so it stays at the
same place on the sheet: the two points of a scale measurement and the
reference point are mapped from the old trim's plan units through the
page to the new trim's (`plan::reanchor_floor`, called by
`PlanImport::apply_to_floor`), and the project maps the floor's measured
points the same way. A trim only turns, so the metres per plan unit stay
the same. Points that fall outside the new trim are kept (the floor shows
them outside its plan) and are never deleted.

## Coordinates (for developers)

`yantra::plan` (core/include/yantra/core/plan/) uses three spaces:

- **Page units**: the source page. PDF: points (1/72 inch) of the page as
  displayed; images: pixels. Origin top-left, x right, y down.
- **Floor units** (plan units of a floor): the trimmed and turned floor.
  The same unit length as page units; the outline is turned by quarter
  turns x 90 + deskew degrees clockwise and the origin is the top-left
  corner of the turned outline's bounding box, whose size is the floor's
  `plan_width` x `plan_height`. Scale (metres per unit, IND-08),
  alignment and measured points use floor units.
- **Building metres**: `(floor units - reference point) x metres per
  unit`, the same frame on every floor (`building::Building::to_building_metres`).
- **Pixels**: either space times a render scale (pixels per unit); pixel
  (i, j) covers [i / s, (i + 1) / s) x [j / s, (j + 1) / s).

`TrimTransform::page_to_floor` / `floor_to_page`, `to_pixels` /
`from_pixels` and `PlanView::page_to_screen` / `screen_to_page` convert
between them. Quarter turns are exact; a deskew uses cos/sin, so round
trips agree to about 1e-9 units.

The building model (`yantra::building`, [BUILDING_MODEL.md](BUILDING_MODEL.md))
holds the trim (`building::Trim`, the same type as `plan::Trim`) and the
floor's `plan_width` x `plan_height`; the project file stores them in
`floors` and `floor_outline` (schema 2), the scale and reference point
in plan units from schema 3.

Example: a sheet with a trim rectangle from (20, 10) to (100, 50) turned
one quarter clockwise gives a floor of 40 x 80 units; the trim's top-left
page corner (20, 10) becomes the floor's top-right corner (40, 0).

## Rendering and tiles (for developers)

Rendering a floor (`render_floor`) rasterises the page at the requested
scale and resamples it: quarter turns copy pixels exactly, a deskew
samples bilinearly, and everything outside the outline is transparent.
Any window of a floor or page raster gives the same pixels as the same
part of a full render, so tiles fit together exactly, and the same input
gives the same pixels every time (tests/plan/test_plan_golden.cpp pins
them by SHA-256).

`TileLayout` cuts a page or a floor into square tiles (256 px by default)
at levels of detail: level 0 is the base scale (1:1 for images, 300 dpi
for PDF pages), each next level halves it, up to one tile for the whole
sheet. The view asks for the level that matches its zoom (`lod_for`) and
the tiles it shows; `TileRenderer` renders missing tiles on a worker
thread into a cache with a memory budget in bytes (least recently used
tiles go first), so a sheet of any size is shown with bounded memory.
The project file records each source's page sizes (`source_pages`) the
first time the import opens it, so the page list is known without
reading the file again.

Threading: PDFium is not thread-safe; every PDFium call in the process
holds one lock (`plan::pdf_lock()`), so PDF pages render one at a time.
Image sources render in parallel. libpdfium.so brings its own C++ runtime
and exports its `operator new`; Yantra links the C++ runtime first, so the
program keeps its own (checked by a test).
