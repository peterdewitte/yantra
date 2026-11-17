<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# File formats

Every file Yantra stores or exchanges is documented (NFR-05, DOC-12).
This index lists each format with its identifier, current version, how a
version change is handled, where the file lives, the code that owns it
and the page that describes it field by field.

Common rules, unless a row says otherwise:

- JSON formats carry their identifier and version (`"format"` and
  `"version"`, or one text such as `"yantra-profile/1"`). A reader refuses
  another identifier and a newer version ("saved by a newer Yantra") and
  ignores unknown members, so a version can gain optional members without
  a new number; a change that older readers would misread raises the
  version.
- Files are UTF-8. Yantra writes its own JSON files atomically (a
  temporary file in the same folder, then a rename).
- `<config>` is `$XDG_CONFIG_HOME/yantra` (or `.config/yantra` in the
  home folder), `<data>` is `$XDG_DATA_HOME/yantra` (or
  `.local/share/yantra`); `<project folder>` is wherever the user keeps a
  project.
- A code block preceded by `<!-- example: <kind> -->` is a complete
  example that a test reads back with the format's reader (DOC-21).

## Project and survey data

| Format, identifier | Version, change | File, location | Code | Description |
| --- | --- | --- | --- | --- |
| Indoor project, SQLite, `application_id` `0x594E5452`, `meta.format` `yantra-project` | schema 7 (`PRAGMA user_version`); older files are migrated step by step when opened, a newer one is refused | `<project folder>/<name>.yantra` | `core/include/yantra/core/project/project.h`, `core/src/project/schema.cpp` | [PROJECT.md](PROJECT.md) |
| Acceptance profile, `yantra-acceptance-profile` | 1; newer refused | JSON in the project table `acceptance_profiles` | `core/include/yantra/core/survey/acceptance.h` | [ACCEPTANCE_PROFILE.md](ACCEPTANCE_PROFILE.md) |
| Stored interpolation map | JSON parts `"format": 1`, cell BLOB of 3 float32; another number is refused per row | project table `interpolations` | `core/include/yantra/core/survey/interpolation.h` | [INTERPOLATION.md](INTERPOLATION.md) |
| Building model JSON, `yantra-building` | 1; newer refused | the building as JSON (tests, tools; inside a project it is tables) | `core/include/yantra/core/building/model_json.h` | [BUILDING_MODEL.md](../BUILDING_MODEL.md) "JSON form" |
| Recording, JSON Lines, header `yantra-recording/1` | 1 in the header line; newer refused, unknown members and line types skipped | `<project folder>/<session>_<nnnn>.yrec` or `.yrec.zst` (zstd), voice notes `.wav` beside them | `core/include/yantra/core/recording/` (`reader.h`, `writer.h`) | [RECORDING.md](RECORDING.md) |
| Cell database CSV, first line `#yantra-cells,1` | 1; a file without the line is read as 1, newer refused | any `.csv` (import, export, shared database) | `core/include/yantra/core/celldb/csv.h` | [CELLS.md](CELLS.md), [CELL_DATABASE.md](../CELL_DATABASE.md) "CSV" |
| Cell verification difference log, CSV | no version: the header line is the format; new columns go at the end | a `.csv` the user names (appended) | `core/include/yantra/core/celldb/verify.h` | [CELLS.md](CELLS.md) |
| Radio record file, `yantra-radio-records` | 1 (yantra-radios RAD-65); newer refused | `*.records.jsonl` (plugin tests, `--records`) | `core/include/yantra/core/measure/record_file.h` | [RECORD_FILES.md](../../third_party/yantra-radios/docs/RECORD_FILES.md) |
| Radio scene, `yantra-radio-scene` | 1, with `minor_version` 1 when a 1.1 member is used (yantra-radios) | the fixed demo scenes of the example floors, `data/demo/scenes/<example id>.json`, served to the simulator by `get_scene` (DEMO-05); data files since Yantra 0.4 | `core/include/yantra/core/demo/demo_project.h`, `core/include/yantra/core/context/building_context.h` | [SCENE_FORMAT.md](../../third_party/yantra-radios/docs/SCENE_FORMAT.md), [DEMO.md](../DEMO.md) "Fixed demo scenes" |
| Radios scenario, `yantra-radios-scenario` | as defined by yantra-radios | a scenario of the radios' scenario folder with a floor's walk, written by `yantra-demo` | `core/include/yantra/core/demo/demo_project.h` (`walk_scenario`), `tools/yantra-demo/main.cpp` | [SCENARIO_FORMAT.md](../../third_party/yantra-radios/docs/SCENARIO_FORMAT.md) |

## Exports

| Format | Version, change | File | Code | Description |
| --- | --- | --- | --- | --- |
| Points CSV (EXP-01, EXP-04) | none; columns chosen by the user, changes in the [CHANGELOG](../../CHANGELOG.md) | any `.csv`; batch: one per session or merged | `core/include/yantra/core/export/csv.h`, `batch.h` | [EXPORT_FILES.md](EXPORT_FILES.md), [EXPORT.md](../EXPORT.md) |
| DXF (EXP-03) | AutoCAD R12 (`AC1009`) | any `.dxf`, one floor | `core/include/yantra/core/export/dxf.h` | [EXPORT_FILES.md](EXPORT_FILES.md) |
| Floor image (EXP-08) | PNG or JPEG | `.png`, `.jpg`, `.jpeg` | `core/include/yantra/core/export/raster.h`, `image.h` | [EXPORT_FILES.md](EXPORT_FILES.md) |
| Report (EXP-05, EXP-06) | PDF 1.x and HTML | `.pdf` and `.html` | `core/include/yantra/core/report/` | [REPORT.md](../REPORT.md), [EXPORT_FILES.md](EXPORT_FILES.md) |
| Exporter plugin rows, table schema `yantra_export_rows` | 1 (plugin API) | in memory, to the plugin | `core/include/yantra/core/export/` | [EXPORT.md](../EXPORT.md), [PLUGIN_API.md](../PLUGIN_API.md) |

## Settings and calibration

| Format, identifier | Version, change | File, location | Code | Description |
| --- | --- | --- | --- | --- |
| App settings, `yantra-app-settings/1` | 1; read member by member, a bad member keeps its default | `<config>/app.json` | `app/model/include/yantra/app/settings.h` | [APP_FILES.md](APP_FILES.md) |
| Plugin state, `yantra-plugin-state` | 1; newer refused, an unreadable file is never overwritten | `<config>/plugin_state.json` | `app/model/include/yantra/app/plugin_state_file.h` | [APP_FILES.md](APP_FILES.md) |
| Level calibration, `yantra-calibration` | 1; any other version refused | `<data>/calibration.json` | `core/include/yantra/core/calibration/table.h` | [CALIBRATION.md](../CALIBRATION.md) "File format" |
| Configuration profile, `yantra-profile/1` | 1 in the format text; another text refused | any `.yprofile` | `app/model/include/yantra/app/config_profile.h` | [PROFILE.md](PROFILE.md) |

## Probe and remote API

| Format, identifier | Version, change | File, location | Code | Description |
| --- | --- | --- | --- | --- |
| API tokens, `yantra-api-tokens` | 1; newer refused, mode 0600 required | `<data>/plugin-data/yantra.probe/api_tokens.json` | `plugins/probe/api_auth.h` | [APP_FILES.md](APP_FILES.md) |
| Probe summaries, JSON Lines (`type` `health`, `coverage`) | none; unknown members ignored | `<data>/plugin-data/yantra.probe/summaries/*.jsonl` | `plugins/probe/probe_plugin.cpp` | [APP_FILES.md](APP_FILES.md), [PROBE.md](../PROBE.md) |
| Remote API, `yantra-api` and live stream `yantra-api-live` | `format` and `version` in every reply | HTTP/JSON, WebSocket | `plugins/probe/api_routes.h` | [API.md](../API.md) |

## Bundled data and tools

| Format, identifier | Version, change | File | Code | Description |
| --- | --- | --- | --- | --- |
| Demo project description, `yantra-demo-project` | 1; newer refused | `data/demo/<id>/<id>.json` | `core/include/yantra/core/demo/demo_project.h` | [DEMO.md](../DEMO.md) "The description" |
| Demo scenario list, `yantra-demo-scenarios` | 1; newer refused | `data/demo/scenarios.json` | `core/include/yantra/core/demo/demo_project.h` | [DEMO.md](../DEMO.md) "Demo scenarios" |
| UI translations, `yantra-ui-strings/1` | 1 in the format text; built into the program | `data/i18n/<language>.json` | `app/model/src/i18n.cpp`, `tools/ci/lint_ui.py` | [APP_FILES.md](APP_FILES.md) |
| Plugin test report, `yantra-plugin-test-report` | 1 | `--json <file>` of `yantra-plugin-test` | `tools/yantra-plugin-test/kit.cpp` | [APP_FILES.md](APP_FILES.md), [PLUGINS.md](../PLUGINS.md) |

Temporary files (`*.tmp` beside a file being saved, the batch export's
scratch project `yantra-batch-<process>-<n>.yantra`) are deleted after
use and are not formats.
