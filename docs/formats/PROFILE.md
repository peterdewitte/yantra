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

Version handling: the `format` text carries the version; any other text,
a profile without a `settings` object, settings that fail the settings
check, a `plugin_state` that is neither an object nor `null`, or a
`calibration` table that its reader refuses makes the whole profile
refused. The `plugin_state` object is written to the plugin state file as
it is and read by that file's reader when the plugins load again
([APP_FILES.md](APP_FILES.md)). Unknown members are ignored.

## Example

<!-- example: yantra-profile/1 -->
```json
{
  "format": "yantra-profile/1",
  "settings": {
    "format": "yantra-app-settings/1",
    "language": "fr",
    "layers": {"bin_size_m": 2.0, "hidden": ["binned"],
               "order": ["plan", "interpolation", "binned", "points"]},
    "sharing": {"folder": "", "mail_from": "", "mail_to": "", "method": "off", "on_walk_end": false,
                "smtp_server": "", "url": "", "user": ""},
    "sound": {"alert_popup": true, "alerts": [], "enabled": false, "level_interval_s": 10.0,
              "level_tone": false, "low_battery": true, "reselection": true, "sync_loss": true, "volume": 0.6},
    "survey": {"graph_span_min": 10.0, "ring": false, "ring_minutes": 10.0, "threshold_dbm": -90.0},
    "theme": "high_contrast",
    "tooltip_delay_s": 0.5,
    "tooltips": true,
    "touch": true,
    "ui_scale": 1.5,
    "window_system": "auto"
  },
  "plugin_state": {
    "format": "yantra-plugin-state",
    "version": 1,
    "enabled": {"yantra.probe": false},
    "settings": {},
    "devices": []
  },
  "calibration": {
    "format": "yantra-calibration",
    "version": 1,
    "entries": [
      {
        "driver": "hackrf",
        "serial": "0000000000000000a1b2c3d4e5f60001",
        "settings": {"gain_amp": "0", "gain_lna": "24", "gain_vga": "20"},
        "radio_plugin": "org.yantra.radios.soapysdr",
        "created": "2026-11-04T10:00:00Z",
        "valid_days": 365,
        "method": "generator",
        "ppm": 1.5,
        "simulated": false,
        "points": [{"frequency_hz": 390000000, "offset_db": -12.125}]
      }
    ]
  }
}
```
