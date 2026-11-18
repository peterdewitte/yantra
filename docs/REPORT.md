<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Report engine

How Yantra lays out its coverage and acceptance reports as PDF and HTML
(EXP-05, EXP-06, BRD-02, BRD-06, DEMO-07, DEMO-12). Code:
`core/report/` (namespace `yantra::report`). This page describes the
engine: the document model, the two back ends, the branding, the plan
credits and reproducible output. What a report contains (one section
per floor, every measured point, statistics, acceptance) is the report
content, built on this engine; the measured points come from the export
core ([EXPORT.md](EXPORT.md)).

In the app, File > Report (docs/APP.md "Export, report and sharing", manual 10.2) fills
`CoverageOptions` (surveyor typed in, floors, thresholds, chosen profiles) and runs it in the background;
the acceptance profile editor stores the profiles (manual 10.3).

## Document model

`Document` (document.h) is format neutral. Its text is final: the
composer translates it and formats its numbers; the back ends only lay
it out.

| Part | Use |
| --- | --- |
| `title`, `subtitle`, `cover` | the cover page: title, subtitle and a key/value list (project, site, operator, date, equipment, ...) |
| `demo_label` | DEMO-07: on the cover, at the top and in the footer of every page, in the PDF information and the HTML title; empty for a real project |
| `footer` | a line in every page footer (e.g. the Yantra version and the date) |
| `generator` | PDF creator, HTML `generator` meta element |
| `created_utc_ns` | PDF creation date (0: none) |
| `words` | the fixed words of the layout ("Page", "of", "Plan", "Licence", "Plan credits"), translated by the composer |
| `blocks` | `heading`, `paragraph` (with a tone: normal, note, pass, fail, demo), `key_values`, `table`, `image`, `legend`, `chart` (histogram bars or a CDF step line with a threshold marker), `page_break` |

A table column marked `numeric` is right-aligned and set in the
monospaced font. An image may carry the `PlanCredit` of the plan it
shows.

## PDF (libharu)

`render_pdf(document, assets, PdfOptions)` gives an A4 portrait PDF made
with libharu (spec 16.2):

- Text in Inter (regular and semi-bold), numeric table columns in
  JetBrains Mono (`data/fonts/`, installed in `share/yantra/fonts/`).
  libharu embeds the TrueType fonts as a subset: only the glyphs used
  are kept (Inter's 411 kB shrink to about 90 kB before compression).
  Text goes through libharu's UTF-8 encoder (Type0 fonts, Identity-H;
  the character code is the Unicode code point), so every character of
  names and notes that the fonts hold prints: Latin with accents, Greek,
  Cyrillic. Control characters become spaces; characters beyond the
  Basic Multilingual Plane and invalid UTF-8 become U+FFFD. JetBrains
  Mono is only embedded when a table has a numeric column.
- libharu 2.3.0 writes a wrong ToUnicode map for these fonts (a CID map
  that sends every code to 0); the engine replaces it with the identity
  map over the Basic Multilingual Plane, so the text can be searched and
  copied. It uses libharu's object headers (`hpdf_objects.h`,
  `hpdf_doc.h`), which `hpdf.h` includes.
- Page 1 is the cover: the curtat.be logo, "Made with Yantra -
  curtat.be" and https://curtat.be (all three linked), the title, the
  subtitle, the DEMO label in a red box and the cover details. The
  blocks start on page 2.
- Every page: the title (and in demo mode a red DEMO band) at the top;
  the footer holds the curtat.be logo with the linked reference, the
  DEMO label in demo mode, "Page n of N" and the footer line.
- A heading stays with the picture that follows it, a picture with its
  legend; a table repeats its header row on a new page. Pictures are
  stored as RGB images (composited on white), the logos once each.
- The document information: the title (with the DEMO label in front in
  demo mode), the creator, in demo mode the label as subject and
  keywords, all as UTF-16 text strings; the creation date when
  `created_utc_ns` is set.
- `PdfOptions.compress` (default on) deflates the streams; off, the
  content is readable text (the tests use it).

## HTML

`render_html(document, assets)` gives one self-contained HTML5 file
(decision: a single file is easy to send or upload, EXP-07): inline CSS,
pictures as PNG `data:` URIs, charts as inline SVG, the curtat.be logo
as its SVG and the Yantra logo (`logo/png/yantra_logo_32.png`) as the
favicon, both as `data:` URIs (BRD-06). The markup is XHTML-compatible
(every element closed, attributes quoted, text escaped). The cover is a
`header` with the logo, the linked reference, the title, the DEMO label
and the details; the `footer` repeats the logo and reference with the
DEMO label and the footer line, and is fixed to the bottom of every
printed page (print media). In demo mode a banner is fixed at the top,
the title starts with the label and a `yantra-label` meta element holds
it. The CSS names Inter and JetBrains Mono without embedding them.

## Assets

`find_assets(data_folders)` finds the files in Yantra's data folders
(`data/` in the source tree, `share/yantra/` installed; the first folder
holding a file wins) and names every missing file in a `ReportError`:

| File | Use |
| --- | --- |
| `fonts/Inter-Regular.ttf`, `fonts/Inter-SemiBold.ttf` | PDF text |
| `fonts/JetBrainsMono-Regular.ttf` | PDF numeric columns |
| `branding/curtat_logo_light_256.png`, `branding/curtat_logo_light_64.png` | PDF cover and footer logo (the light variant: report pages are white, BRD-03) |
| `branding/curtat_logo.svg` | HTML logo |
| `logo/png/yantra_logo_32.png` | HTML favicon (BRD-06) |

## Plans and their credits (DEMO-12)

`floor_figure(project, table, FigureOptions)` (figure.h) makes a floor's
picture for a report: the export core's floor image (the shared point
list, IND-30) as an `ImageBlock`, with the `PlanCredit` of its plan
sheet. Sheets are known from the demo descriptions Yantra ships
(`described_sheets(data_folders)` reads `demo/*/*.json`), matched by the
source's SHA-256; `sheet_credit` gives holder, designer, year, inventory
number, licence and its URL, or nothing for a sheet without a licence
or credit (the public-domain Fraunces sheets).

A licence-restricted sheet (a MAK sheet) is never stored in a project,
package or file of Yantra's own. Its plan is drawn only from the user's
cached, unaltered copy: `FigureOptions.sheet_cache` names the cache
folder, and `demo::check_sheet_file` checks the copy (SHA-256, size,
page size) when the picture is made; the floor image then reads it with
`Project::read_source_from`, which checks the hash again. When there is
no cache folder, no copy, no description, or the copy differs, the
picture has no plan (and no credit) and a warning says why. The floor
image export (EXP-08) never draws such a plan
(`ImageOptions.reference_file` stays empty there), not even when the
project's path hint points at a copy.

A picture with a plan credit prints "Plan: <title>, <credit>. Licence
<licence> (<url>)." under it, and the report ends with a "Plan credits"
list of every credit once (with the catalogue page).

## Files and reproducible output

`write_report(file, document, assets)` renders by the extension
(`.pdf`, `.html`, `.htm`, any case; another is a `ReportError`) and
replaces the file atomically. The output depends only on the document
and the assets: no clock, no random document ID (libharu writes an
`/ID` only for encrypted files). `creation_time(now)` gives the time for
`created_utc_ns`: the `SOURCE_DATE_EPOCH` environment variable (seconds,
the reproducible-builds convention) when it is set, else `now`, so a
committed sample report can be regenerated byte for byte.
`check_pdf(bytes)` checks a PDF's structure (header, trailer,
cross-reference offset, page tree count) and gives its page count.

## Report content

Code: `core/report/coverage.h`. `coverage_report(project, options)`
composes the indoor coverage and acceptance report on the engine above
(EXP-05, EXP-06, IND-30, IND-43, DB-04, DB-05); `write_coverage_report`
also writes it as PDF and HTML in one call (the report dialog, File >
Report and the sample report). The composer is technology-free and has
no clock: the same project and options give the same bytes. Its texts
go through the caller's `translate` (the app's tr()). Content, in order:

- **Cover**: project, site, the building's address (IND-47), network
  operator, the operator field (who measured: typed in the dialog and
  never stored, else the About building surveyor), the measurement dates,
  floors, measured points, equipment and the report time.
- **About the building** (IND-47, first section): address, position
  (WGS 84), surveyor, survey date, notes, the OpenStreetMap map from
  cached tiles only (`CoverageOptions::tile_cache`; caption with
  "© OpenStreetMap contributors" and ODbL; without tiles a "No map" note,
  as in the sample) and the photo.
- **Measurement metadata**: equipment (the walk tests' radio and the
  radios of the recordings with driver, serial and plugin), calibration,
  antenna correction, cell database, plugins, metric, date, operator,
  and the walk tests per floor with their points. Uncalibrated (dBFS)
  levels are noted.
- **Building summary**: every floor's points together, a row per floor
  (points, median, % of points and % of floor area at the threshold, map
  fit, serving-cell changes) and the acceptance profiles over the
  building with PASS or FAIL. A failing profile names its weak areas
  (DEMO-16): `survey::weak_areas` groups each floor's measured points
  below the profile's first failed threshold (points at most 3 m apart,
  directly or through other weak points, are one area); the verdict line
  names the three largest ("Indoor acceptance: FAIL (weak area: First
  floor, centre of the plan)": the floor and the points' most frequent
  label, else the third of the plan it lies in) and a table gives each
  area's extent in metres from the plan's top-left corner, its points and
  its lowest value.
- **Per floor** (a new page each): the plan with every measured point of
  the project window's list (`survey::point_list` with the window's
  metric and scale, IND-30) with its legend and count; the newest stored
  interpolated map of the report's metric (else the newest) drawn over
  the plan, with the share of the assessed floor area per colour band,
  its label, method (IDW with its point count, power and search radius,
  IND-41), grid and areas, and notes when it
  is stale or of another metric; the floor statistics (min, median, max,
  means, percentiles) and a table of "Share of measured points (%)" next
  to "Share of floor area (interpolated) (%)" per threshold, never mixed
  (IND-43; the same wording as the figures in the Map tab's Advanced dropdown and in NL/FR/DE); the
  acceptance table; the histogram and CDF of the values with the
  threshold marked; per-cell statistics; the serving-cell changes per
  pair of cells; optionally the point list as a table.
- **Network verification**: the cells received in the walks
  (`observed_cells`, replaying the project's recordings through the
  technology plugins) against the cell database, every difference,
  unknown and missing cell as a row (`celldb::log_lines`), and the
  neighbour cross-check (`celldb::cross_check`, on the database alone
  when no received cells are given).

Acceptance rows: each profile on the measured points and on the floor
area (`survey::evaluate_area` of the stored map; "not assessed" without
a map), plus the tested area when it is the profile's basis. The basis
marked * decides the profile's PASS or FAIL.

## Sample report (DEMO-06)

`docs/samples/fraunces_report.pdf` and `fraunces_report.html` are the
report of the Fraunces demo project (two floors, the simulated walks,
the stored interpolations, the default acceptance profile, the
verification of the replayed recordings, the DEMO label, no surveyor;
its verdict is FAIL with the weak stairwell of the first floor named,
DEMO-16),
composed as the report dialog does with its defaults and dated by a
fixed `SOURCE_DATE_EPOCH` (2026-11-08 00:00 UTC). Made by

    ninja -C build sample_report     # yantra-demo --sample-report docs/samples ...

The ctest `sample_report_compare` (and the CI step "Sample report")
makes it again from the built demo project and compares it with the
committed files (`yantra-demo --sample-report DIR ... --compare
docs/samples`, `core/report/compare.h`): the HTML text must be equal
except for numbers within 0.25 or 0.5 %, each picture may have at most
2 % of its pixels off by more than 24 in a channel, and the PDF must be
valid with the same page count. On the build machine both files are
byte-identical (the tool says "identical"). A difference beyond the
tolerance fails with the lines that differ; an intended change commits
the new sample.

## Tests

`tests/report` (one ctest per case): a valid PDF with the cover first
and identical bytes for the same document; a name with accented and
Greek letters drawn as codes of the embedded Inter subset that map to
real glyphs, with the corrected ToUnicode map; numeric columns in
JetBrains Mono; the curtat.be reference on the cover and in every
footer with its links and the DEMO label (counted per page); the HTML
favicon, logo, self-containment and DEMO marks; atomic file output and
the assets; `SOURCE_DATE_EPOCH`; a licence-restricted plan (a copy of a
Fraunces sheet stands in for it; the MAK images are never used in
tests) drawn only from the checked cached copy with its credit, never
from a missing or altered copy or by the image export; the credits of
the shipped MAK descriptions. `tests/report/test_coverage.cpp`: the report's
statistics, per-cell rows, changes and acceptance on both bases against
hand-computed values; stored, given and default profiles; the
verification and cross-check tables; PDF and HTML in one call with the
same bytes again; TST-34 on generated five-floor projects with dense
walks: each floor's points equal the project window's list one for one,
are all drawn and listed in the HTML point table.
`tests/measure_host/test_measure_report.cpp`: the report of the Fraunces
demo.
