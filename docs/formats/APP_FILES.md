<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Application files: settings, plugin state, probe, UI strings, test reports

The files Yantra keeps outside projects (NFR-05). `<config>` is
`$XDG_CONFIG_HOME/yantra`, or `.config/yantra` in the home folder;
`<data>` is the user data folder, `$XDG_DATA_HOME/yantra` or
`.local/share/yantra` in the home folder. Every
JSON file here is UTF-8, one object, and carries its format identifier and
version; a newer version is refused, unknown members are ignored, and the
files Yantra writes are written atomically (a temporary file renamed).

## App settings (`yantra-app-settings/1`)

`<config>/app.json`, written by the Settings window with one-space
indentation. Code: `app/model/include/yantra/app/settings.h`
(`settings_to_json`, `settings_from_json`, `check_settings`).

The reader is lenient per member: a missing member, one of the wrong type
or out of range keeps its default and the rest is read; only text that is
not a JSON object is refused ("defaults used"). A `format` naming
another file kind or a version above 1 is refused the same way; a file
without `format` is read leniently.

| Member | Type, range | Default |
| --- | --- | --- |
| `format` | `"yantra-app-settings/1"` | |
| `recent_projects`, `recent_folders` | lists of paths, newest first; at most 10 and 8 | `[]` |
| `layers.order` | the four layers `plan`, `interpolation`, `binned`, `points` in drawing order (missing ones are added in their default place; the `walls` and `antennas` of 0.3 are ignored) | as listed |
| `layers.hidden` | layers switched off | `["binned"]` |
| `layers.bin_size_m` | 0.5..50 m (PP-05) | `2` |
| `window_system` | `auto`, `x11`, `wayland` | `auto` |
| `tooltips`, `tooltip_delay_s` | bool; 0..5 s | `true`, `0.5` |
| `theme` | `dark`, `light`, `high_contrast` | `dark` |
| `language` | `en`, `nl`, `fr`, `de` | `en` |
| `touch`, `ui_scale` | bool; 0.75..2.5 | `false`, `1` |
| `sound` | `enabled` (false), `volume` 0..1 (0.6), `level_tone` (false), `level_interval_s` 2..120 (10), `sync_loss`, `reselection`, `low_battery`, `alert_popup` (true), `alerts`: at most 8 `{"cell": key, "threshold_dbm": -150..-20}` | |
| `survey` | `threshold_dbm` -150..-20 (-95), `graph_span_min` 1..60 (10), `ring` (false), `ring_minutes` 2..60 (10) | |
| `tour` | `offered`, `building` (the tour's project), `step`, `done` (at most 64 step ids), `finished` | not offered |
| `sharing` (EXP-07) | `method` `off`, `folder`, `http`, `smtp`; `folder`, `url`, `smtp_server`, `mail_from`, `mail_to`, `user`, `on_walk_end`; a password is never stored | `off` |

<!-- example: yantra-app-settings/1 -->
```json
{
 "format": "yantra-app-settings/1",
 "language": "nl",
 "layers": {"bin_size_m": 2.0, "hidden": ["binned"],
            "order": ["plan", "interpolation", "binned", "points"]},
 "recent_folders": [],
 "recent_projects": [],
 "sharing": {"folder": "", "mail_from": "", "mail_to": "", "method": "off", "on_walk_end": false,
             "smtp_server": "", "url": "", "user": ""},
 "sound": {"alert_popup": true, "alerts": [{"cell": "la=100;mcc=901;mnc=1", "threshold_dbm": -100.0}],
           "enabled": true, "level_interval_s": 10.0, "level_tone": false, "low_battery": true,
           "reselection": true, "sync_loss": true, "volume": 0.6},
 "survey": {"graph_span_min": 10.0, "ring": false, "ring_minutes": 10.0, "threshold_dbm": -95.0},
 "theme": "light",
 "tooltip_delay_s": 0.5,
 "tooltips": true,
 "touch": false,
 "tour": {"building": "", "done": [], "finished": false, "offered": true, "step": ""},
 "ui_scale": 1.25,
 "window_system": "auto"
}
```

## Plugin state (`yantra-plugin-state`, version 1)

`<config>/plugin_state.json` (RAD-47): which plugins are on, the
plugins' own settings (text), and the stored settings of each radio
device. Two-space indentation. Code:
`app/model/include/yantra/app/plugin_state_file.h` (`PluginStateFile`:
the constructor reads the file, `load_error()` says why it could not;
an unreadable file is never overwritten).

| Member | Content |
| --- | --- |
| `format`, `version` | `"yantra-plugin-state"`, `1` |
| `enabled` | plugin id to `true`/`false`; a plugin not named uses its default |
| `settings` | plugin id to an object of key to text value |
| `devices` | one per radio device: `plugin` (radio plugin id), `device` (device key), `settings`: list of `{"key", "type", "value"}`, type `bool`, `int`, `uint`, `double`, `string` or `enum` (the enum's integer value); only settings that differ from the plugin's default, or were set before |

<!-- example: yantra-plugin-state -->
```json
{
  "format": "yantra-plugin-state",
  "version": 1,
  "enabled": {"yantra.probe": false, "yantra.tetra": true},
  "settings": {"yantra.probe": {"summary.health_s": "60"}},
  "devices": [
    {
      "plugin": "simulator",
      "device": "simulator:0",
      "settings": [
        {"key": "playback_speed", "type": "double", "value": 2.0},
        {"key": "seed", "type": "uint", "value": 7}
      ]
    }
  ]
}
```

## Configuration profile (`yantra-profile/1`)

The app settings, plugin state and calibration table in one file to move
to another PC: [PROFILE.md](PROFILE.md).

## Remote API tokens (`yantra-api-tokens`, version 1)

`<data>/plugin-data/yantra.probe/api_tokens.json`, or the file named by
the probe setting `api.token_file` (API-02, PLG-24). Created on first use with
mode 0600; a file that group or others can read is refused. Code:
`plugins/probe/api_auth.h` (`TokenStore`).

| Member | Content |
| --- | --- |
| `format`, `version` | `"yantra-api-tokens"`, `1` |
| `token` | the token for local clients: 64 hexadecimal characters when generated; at least 32 characters |
| `lan_token` | the token for LAN clients, or `null` (no LAN access) |

```json
{"format": "yantra-api-tokens", "version": 1,
 "token": "<64 hexadecimal characters>", "lan_token": null}
```

## Probe summaries (JSON Lines)

The probe plugin (API-03, PLG-24) appends one line per period to
`<data>/plugin-data/yantra.probe/summaries/health.jsonl` (every
`summary.health_s`, default 60 s) and `coverage.jsonl` (every
`summary.coverage_s`, default 300 s). Past 10 MB a file is renamed to
`health.1.jsonl` / `coverage.1.jsonl` (one older file kept). The same
objects are published on the API ([API.md](../API.md)). No version: a
reader takes `type` and ignores unknown members. Code:
`plugins/probe/probe_plugin.cpp`.

| `type` | Members |
| --- | --- |
| `health` | `utc` (ISO 8601), `ok`, `status` (the host's status object, [API.md](../API.md)) or `error` (text), `live_carriers` (count) |
| `coverage` | `utc`, `period_s`, `carriers`: per carrier `frequency_hz`, `records`, `synced`, `technology`, `role`, `level_min_dbm`, `level_max_dbm`, `level_mean_dbm`; `demo` (true when the data is simulated) |

```json
{"type":"coverage","utc":"2026-11-05T09:50:00Z","period_s":300.0,"carriers":[{"frequency_hz":390012500,"records":3000,"synced":true,"technology":"tetra","role":"serving","level_min_dbm":-74.5,"level_max_dbm":-61.0,"level_mean_dbm":-66.8}],"demo":true}
```

## UI translations (`yantra-ui-strings/1`)

`data/i18n/<language>.json` for `nl`, `fr` and `de` (UI-12), built into
the program. `format`, `language` (must match the file) and `strings`:
the English text to its translation; an empty or missing translation
shows the English text. Checked by `tools/ci/lint_ui.py`; read by
`app/model/src/i18n.cpp`.

```json
{"format": "yantra-ui-strings/1", "language": "nl",
 "strings": {"  (connected)": "  (verbonden)"}}
```

## Plugin test report (`yantra-plugin-test-report`, version 1)

Written by `yantra-plugin-test check --json <file>` (PLG-13,
[PLUGINS.md](../PLUGINS.md)). Code: `tools/yantra-plugin-test/kit.cpp`.

| Member | Content |
| --- | --- |
| `format`, `version` | `"yantra-plugin-test-report"`, `1` |
| `plugin`, `plugin_id` | the file tested and its id |
| `result` | `pass` or `fail` |
| `checks` | per check `name`, `result` (`pass`, `fail`, `skip`), `messages` (texts) |
| `warnings` | texts |

```json
{"format": "yantra-plugin-test-report", "version": 1,
 "plugin": "example_beacon.so", "plugin_id": "yantra.example.beacon", "result": "pass",
 "checks": [{"name": "load", "result": "pass", "messages": []},
            {"name": "golden", "result": "skip", "messages": ["no golden file given"]}],
 "warnings": []}
```

The golden files of `--golden` are the plugin's output as JSON, compared
by the tool itself; they have no format identifier.
