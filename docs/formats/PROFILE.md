<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Configuration profile format (`yantra-profile/1`)

A configuration profile (NFR-06) carries one way of working from one PC
to another: Settings > General > Profiles > "Save profile..." writes it,
"Load profile..." applies it. It is one UTF-8 JSON object, by default
with the extension `.yprofile`
(app/model/include/yantra/app/config_profile.h).

| Member | Content |
|---|---|
| `format` | `"yantra-profile/1"` |
| `settings` | the app settings as in the settings file (`yantra-app-settings/1`): theme and palettes, layer order and visibility (UI layout), touch layout and UI scale, language, survey defaults and thresholds, sound and level alerts, sharing. Without `recent_projects`, `recent_folders` and `tour`, which stay on the PC. |
| `plugin_state` | the plugin state file as it is (plugins on or off, plugin settings, the stored settings of each radio device, RAD-47), or `null` |
| `calibration` | the level calibration table (`yantra-calibration`, docs/CALIBRATION.md), or `null` |

Loading checks the whole profile first; a damaged one changes nothing.
Then the open project closes, the plugin state and calibration files are
replaced (a `null` part removes the file, so its defaults apply), the
settings are written with this PC's recent lists and tour progress, and
the plugins load again with the new state. Cell databases are not part
of a profile: they belong to their projects (DB-01).
