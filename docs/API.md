<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Remote API reference (API-01, API-02, DOC-13)

The remote API is served by the **probe feature plugin** (`probe.so`,
plugin id `yantra.probe`, PLG-24). Without that plugin, or with it
disabled, Yantra opens no network socket. With it enabled the server still
stays off until an address is given: the setting `api.listen` (the
"Remote probes" window in the app, Tools menu, or `yantra-probe --api`).
The headless runner `yantra-probe` is described in [PROBE.md](PROBE.md).

The API serves broadcast/system information, levels and quality only:
the fields of the categories SYSTEM_INFO, LEVEL and QUALITY of the
technology schemas (PLG-08). There is no SDS, no subscriber address, no
voice and no decryption, and device internals and plan positions stay in
the probe.

Adapted from the API of yantra-v1 c24e8f5 (docs/API.md): the same paths,
formats and token file; no GPS commands (indoor only), no built-in TLS.

## Starting

| Setting of `yantra.probe` | Meaning | Default |
|---|---|---|
| `api.listen` | `[ADDRESS:]PORT`: `8470` or `127.0.0.1:8470` (this computer only), `0.0.0.0:8470` or `[::]:8470` (all interfaces); port `0` takes a free port | empty: no server |
| `api.allowed_origins` | comma-separated browser origins allowed to call the API (CORS, WebSocket) | none |
| `api.token_file` | the token file | `<plugin data folder>/api_tokens.json` |
| `summary.health_s`, `summary.coverage_s` | periods of the fixed-probe summaries ([PROBE.md](PROBE.md)), seconds; `0` = off | 60, 300 |

The plugin data folder is `$XDG_DATA_HOME/yantra/plugin-data/yantra.probe`
(`~/.local/share/...`; for `yantra-probe --data DIR`:
`DIR/plugin-data/yantra.probe`). On start the log says

    yantra.probe: api http port 8470; token file <data folder>/plugin-data/yantra.probe/api_tokens.json

HTTPS is not built in (no TLS library is bundled): for any address other
than loopback put the API behind a TLS reverse proxy or an SSH tunnel.

## Authentication

Every endpoint except `GET /api/v1/info` needs a bearer token:

    Authorization: Bearer <token>

- The **owner token** (full access: read, live view, commands, token
  changes) is created on the first start: 32 random bytes as 64 hex
  digits, in the token file, which only its owner may read (file 0600,
  folder 0700; a file readable by others is refused).
- The **LAN token** (read and live view only) is optional, made with
  `POST /api/v1/auth/lan {"enabled":true}`, and accepted only from
  loopback, private (RFC 1918, RFC 6598), link-local and IPv6
  unique-local addresses.
- `POST /api/v1/auth/rotate` makes a new owner token; the old one stops
  at once. The file is read again when it changes on disk.
- After 5 failed attempts from one address within 60 s that address is
  blocked for 60 s (`429` with `Retry-After`).

Token file (versioned JSON, NFR-05):

```json
{"format": "yantra-api-tokens", "version": 1, "token": "<64 hex>", "lan_token": null}
```

## Browsers: Origin and CORS

A request with an `Origin` header outside `api.allowed_origins` is refused
with `403`, the WebSocket upgrade included; `null` and `*` are never
allowed. An allowed origin gets `Access-Control-Allow-Origin`, and
`OPTIONS` pre-flight requests are answered without a token. Clients
without `Origin` (scripts, other probes) only need the token.

## Replies

Every reply is a JSON object of the format `yantra-api`, version 1:

```json
{"format": "yantra-api", "version": 1, "type": "cells", "demo": true, "carriers": [ ... ]}
```

Errors: `{"format":"yantra-api","version":1,"type":"error","ok":false,"status":401,"error":"..."}`
with the HTTP status `400` (bad request or filter), `401` (no or wrong
token), `403` (foreign origin, or the LAN token for a command), `404`,
`405` (`Allow` header), `413` (body over 64 KiB), `422` (a command Yantra
refused), `429` (blocked address), `503` (no commands available).
`demo` is true when the data comes from the simulator (DEMO-10).

## Endpoints

All under `/api/v1`:

| Method and path | Token | Reply `type` | Content |
|---|---|---|---|
| `GET /info` | none | `info` | `name`, `software`, `api_version`, `licence`, `source_url` |
| `GET /status` | read | `status` | `status` (the health summary, below), `api` (`version`, `server`, `licence`, `source_url`, `access`), `demo`, `source_url` |
| `GET /commands` | read | `commands` | `commands`: `command`, `path`, `args` (`name`, `required`), `help` |
| `POST /command` | owner | `command` | body `{"command":"record.start","args":{"name":"x"}}`: the command reply |
| `POST /<command path>` | owner | `command` | e.g. `POST /record/start` with an optional JSON object of arguments |
| `GET /cells` | read | `cells` | `carriers`: `frequency_hz`, `technology`, `schema`, `role`, `synced`, `level_dbm`, `calibrated`, `simulated`, `age_s`, `sysinfo`, `levels`, `quality`, `neighbour_count` |
| `GET /neighbours` | read | `neighbours` | `broadcast`: per broadcasting carrier its list (`entry_count`, `neighbours` with `entry_index` and the list's fields); `measured`: the measured neighbour carriers |
| `GET /levels` | read | `levels` | `carriers`: `frequency_hz`, `role`, `level_dbm`, `calibrated`, `levels`, `quality`, `age_s` |
| `GET /summaries` | read | `summaries` | `health` (now) and `coverage` (the last one, or null) |
| `GET /recordings` | read | `recordings` | `files`: `name`, `size`, `modified` (UTC), `url` |
| `GET /recordings/<file>` | read | (the file) | the file (`.yrec`, `.yrec.zst`) |
| `POST /auth/rotate` | owner | `token` | `token`: the new owner token |
| `POST /auth/lan` | owner | `token` | body `{"enabled":true|false}`; `lan_token` or null |
| `GET /live` (WebSocket) | read | | the live view, below |

Carriers silent for 30 s drop out of the views. `sysinfo` holds the
SYSTEM_INFO fields of the newest synchronised record (enumerations by
their names), null before a carrier is synchronised; `levels` and
`quality` map a field name to `{"value": ..., "unit": "..."}`;
`level_dbm` is the core `level` field, in dBm through the calibration
(dBFS and `calibrated: false` without one). The field names inside
`sysinfo`, `levels`, `quality` and the neighbour lists are those of the
technology plugin's schemas (for TETRA: [plugins/TETRA.md](plugins/TETRA.md)).
`role` is `serving`, `neighbour` or `other`.

The examples below are replies of `yantra-probe --demo` (simulated
network, so `demo` is true), shortened to one carrier where a list is
long; a block marked as an example is complete JSON and is checked by the
documentation tests (DOC-21).

### `GET /info`

Without a token: the name, the version, the licence and the source link
(NFR-01).

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "info", "api_version": 1, "name": "yantra",
 "software": "Yantra probe plugin 0.1.0",
 "licence": "GPL-3.0-or-later (the TETRA radio plugin includes osmo-tetra under AGPL-3.0-or-later)",
 "source_url": "https://github.com/peterdewitte/yantra"}
```

### `GET /status`

| Member | Type | Content |
|---|---|---|
| `status` | object | the health summary ([Status](#status)) |
| `api.version` | integer | API version (1) |
| `api.server` | string | the serving plugin and its version |
| `api.licence`, `api.source_url`, `source_url` | string | licence and source link (NFR-01) |
| `api.access` | string | `full` (owner token) or `read` (LAN token) |
| `demo` | boolean | simulated data (DEMO-10) |

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "status", "demo": true,
 "source_url": "https://github.com/peterdewitte/yantra",
 "api": {"access": "full", "version": 1, "server": "Yantra probe plugin 0.1.0",
         "licence": "GPL-3.0-or-later (the TETRA radio plugin includes osmo-tetra under AGPL-3.0-or-later)",
         "source_url": "https://github.com/peterdewitte/yantra"},
 "status": {"application": "yantra-probe", "version": "0.1.0", "state": "monitoring",
            "monitoring": true, "demo": true, "carriers": 12, "synced_carriers": 5, "cycles": 8,
            "pipeline": {"records": 162, "accepted": 162, "implausible": 0, "late": 0, "cycles": 8},
            "radio": {"plugin": "simulator", "device": "simulator:0", "label": "Demo (simulated network)",
                      "simulated": true, "state": "streaming", "records": 162, "dropped": 0, "reopens": 0},
            "recording": {"recording": false}, "play": {"playing": false}, "sound": false,
            "plugins": [{"id": "yantra.probe", "status": "enabled", "version": "0.1.0"},
                        {"id": "yantra.tetra", "status": "enabled", "version": "0.1.0"}],
            "recording_folder": "/var/lib/probe/recordings", "uptime_s": 8.9,
            "source_url": "https://github.com/peterdewitte/yantra"}}
```

### `GET /commands`

The command table (below) as data: `commands` is a list of
`{"command": string, "path": string, "args": [{"name": string, "required": boolean}], "help": string}`.

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "commands", "commands": [
 {"command": "record.start", "path": "/api/v1/record/start", "args": [{"name": "name", "required": false}],
  "help": "Start recording (name optional: date/time)."},
 {"command": "play", "path": "/api/v1/play",
  "args": [{"name": "file", "required": true}, {"name": "speed", "required": false}],
  "help": "Replay a recording file or folder (speed 1 = real time, 0 = fast)."}]}
```

### `GET /cells`, `GET /levels`

`carriers` is a list, one object per carrier heard in the last 30 s:

| Member | Type | Content |
|---|---|---|
| `frequency_hz` | integer | carrier frequency |
| `role` | string | `serving`, `neighbour`, `other` |
| `level_dbm` | number | level (dBm when `calibrated`, else dBFS) |
| `calibrated` | boolean | a calibration entry applied |
| `levels`, `quality` | object | field name to `{"value": number, "unit": string}` |
| `age_s` | number | seconds since the carrier's newest record |
| `technology`, `schema` | string | `/cells` only: technology plugin and record schema |
| `synced`, `simulated` | boolean | `/cells` only: decoded (synchronised), from a simulated radio |
| `sysinfo` | object or null | `/cells` only: broadcast system information |
| `neighbour_count` | integer | `/cells` only: entries of its broadcast neighbour list |

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "levels", "demo": true, "carriers": [
 {"frequency_hz": 390000000, "role": "serving", "level_dbm": -61.2, "calibrated": false, "age_s": 0.24,
  "levels": {"c1_db": {"value": 28.8, "unit": "dB"}},
  "quality": {"snr_db": {"value": 35.3, "unit": "dB"}, "ber_pct": {"value": 0.0, "unit": "%"},
              "freq_error_hz": {"value": -195.1, "unit": "Hz"}}}]}
```

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "cells", "demo": true, "carriers": [
 {"frequency_hz": 389950000, "technology": "tetra", "schema": "tetra_cell", "role": "other",
  "synced": false, "level_dbm": -64.4, "calibrated": false, "simulated": true, "age_s": 6.5,
  "sysinfo": null, "levels": {}, "quality": {}, "neighbour_count": 0}]}
```

### `GET /neighbours`

| Member | Type | Content |
|---|---|---|
| `broadcast` | list | per carrier that broadcasts a neighbour list: `frequency_hz`, `technology`, `entry_count`, `neighbours` (objects with `entry_index` and the list's fields) |
| `measured` | list | the carriers measured as neighbours: `frequency_hz`, `level_dbm`, `synced`, `sysinfo` |

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "neighbours", "demo": true,
 "broadcast": [{"frequency_hz": 390000000, "technology": "tetra", "entry_count": 2, "neighbours": [
   {"entry_index": 0, "cell_number": 2, "main_carrier": 3604, "neighbour_frequency_hz": 390100000,
    "synchronised": true, "cell_load": 0},
   {"entry_index": 1, "cell_number": 3, "main_carrier": 3612, "neighbour_frequency_hz": 390300000,
    "synchronised": true, "cell_load": 0}]}],
 "measured": [{"frequency_hz": 390100000, "level_dbm": -21.0, "synced": true,
               "sysinfo": {"mcc": 901, "mnc": 1, "la": 100, "colour_code": 2, "main_carrier": 3604,
                           "rxlev_access_min_dbm": -115, "ms_txpwr_max_cell_dbm": 35}}]}
```

### `GET /summaries`

`health`: the health summary now (`type` `health`, `utc`, `ok`,
`live_carriers`, `status`); `coverage`: the newest coverage summary
written by the fixed probe ([PROBE.md](PROBE.md)), or null.

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "summaries", "demo": true, "coverage": null,
 "health": {"type": "health", "utc": "2026-11-09T17:34:37Z", "ok": true, "live_carriers": 11,
            "status": {"application": "yantra-probe", "state": "monitoring", "monitoring": true}}}
```

### `GET /recordings`, `GET /recordings/<file>`

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "recordings", "files": [
 {"name": "site1_0000.yrec", "size": 48454, "modified": "2026-11-09T17:34:46Z",
  "url": "/api/v1/recordings/site1_0000.yrec"}]}
```

`GET` of a file's `url` returns the file as it is
([formats/RECORDING.md](formats/RECORDING.md)).

### `POST /auth/rotate`, `POST /auth/lan`

Owner token only. Both reply with `type` `token`: `/auth/rotate` with the
new owner `token` (the old one stops at once), `/auth/lan` with
`lan_token` (a new LAN token for `{"enabled":true}`, null for
`{"enabled":false}`).

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "token", "lan_token": null}
```

### Commands

The commands of API-01, the same as on the runner's standard input
([PROBE.md](PROBE.md)) and through the plugin API's `control` service
([PLUGIN_API.md](PLUGIN_API.md)). The table is
`core/src/control/command.cpp` (`command_table`); the probe serves it as
`GET /commands`. Each is `POST /api/v1/<path>` with an optional JSON
object of arguments, or `POST /api/v1/command` with
`{"command": ..., "args": {...}}`:

| Command | Path | Arguments | Does | `result` members |
|---|---|---|---|---|
| `record.start` | `/record/start` | `name` (string, optional; default date and time) | starts recording (monitoring must run) | `recording`, `state`, `name`, `file`, `files`, `durable_inputs`, `dropped`, `ring_deleted` |
| `record.stop` | `/record/stop` | | stops it | `recording` (false), `files` (list of file names) |
| `monitor.on` | `/monitor/on` | | opens the configured radio and measures | `monitoring`, `radio` (label), `simulated` |
| `monitor.off` | `/monitor/off` | | closes the radio (a recording stops) | `monitoring` |
| `play` | `/play` | `file` (string, required: a recording file name in the recording folder, a path, or a folder), `speed` (number: 1 real time, 0 as fast as possible, 0.1 to 100) | replays a recording | `playing`, `state`, `source`, `position_s`, `duration_s`, `speed` |
| `sound.on`, `sound.off` | `/sound/on`, `/sound/off` | | audible alerts on/off | `sound`, `output` |
| `status` | `/status` (POST) | | the status as a command reply | the health summary |
| `rotate` | `/rotate` | | closes the recording file and starts the next | as `record.start` |
| `quit` | `/quit` | | stops recording and the radio and exits (headless runner) | `quit` (true) |

Command reply (HTTP 200):

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "command", "ok": true, "command": "record.start",
 "result": {"recording": true, "state": "recording", "name": "site1", "file": "site1_0000.yrec",
            "files": 1, "durable_inputs": 0, "dropped": 0, "ring_deleted": 0}}
```

A refused command: `"ok": false` with `"error"` (HTTP 422):

<!-- example: json -->
```json
{"format": "yantra-api", "version": 1, "type": "command", "ok": false, "command": "",
 "error": "command play needs file"}
```

An unknown command is a bad request (HTTP 400, `type` `error`). The
desktop app runs commands on its UI thread: there a command reply says
`"queued": true`, and the status shows its effect; play, rotate and quit
are refused in the app.

### Status

The health summary of the runner (the app gives the same fields where it
has them):

| Member | Type | Content |
|---|---|---|
| `application`, `version`, `source_url` | string | the program, its version and the source link (NFR-01) |
| `state` | string | `stopped`, `monitoring`, `recording`, `playing` |
| `monitoring`, `demo`, `sound` | boolean | |
| `radio` | object | `plugin`, `device`, `label`, `simulated`, `state`, `records`, `dropped`, `reopens` |
| `recording` | object | `recording`; while recording also `state`, `name`, `file`, `files`, `durable_inputs`, `dropped`, `ring_deleted` |
| `play` | object | `playing`; while playing also `state`, `source`, `position_s`, `duration_s`, `speed` |
| `pipeline` | object | counters `records`, `accepted`, `implausible`, `late`, `cycles` |
| `cycles`, `carriers`, `synced_carriers` | integer | measurement cycles, carriers heard, carriers decoded |
| `plugins` | list | `id`, `version`, `status` |
| `recording_folder` | string | where recordings go |
| `uptime_s` | number | seconds since the start |
| `last_error` | string | the last error, when there was one |

## WebSocket live view (`GET /api/v1/live`)

The filter is given on connect (query) and changed later by a text
message:

    /api/v1/live?types=sync,sysinfo,neighbours,levels,summary&rate=2&carrier=390000000
    {"types": ["levels"], "rate": 5, "carrier": null}

`types` from `sync`, `sysinfo`, `neighbours`, `levels`, `summary` (or
`all`; default all), `rate` 0.1 to 10 rounds per second (default 1),
`carrier` one carrier in Hz (within 1 kHz) or all. Messages are JSON
objects of the format `yantra-api-live`, version 1, with `type` and
`demo`:

| `type` | When | Content |
|---|---|---|
| `hello` | after connecting | `filter`, `types` |
| `sync` | when new records came | `carriers`: `frequency_hz`, `role`, `synced`, `technology`, `age_s` |
| `levels` | when new records came | as `GET /levels` |
| `sysinfo` | when it changed, at least every 30 s | `carriers`: `frequency_hz`, `technology`, `role`, `sysinfo` |
| `neighbours` | when a broadcast list changed, at least every 30 s | as `GET /neighbours` |
| `summary` | when a fixed-probe summary is written | `summary` |
| `filter` | after a filter message | the new `filter` |
| `error` | after a bad message | `error` |

At most 8 live clients; messages over 4 KiB are refused.

## Examples

Start a probe on a free port and read its owner token:

```sh
yantra-probe --demo --api 127.0.0.1:8470 &
T=$(python3 -c 'import json,os; print(json.load(open(os.path.expanduser("~/.local/share/yantra/plugin-data/yantra.probe/api_tokens.json")))["token"])')
YANTRA_API=http://127.0.0.1:8470/api/v1
TOKEN=$T
```

The blocks marked as API examples below run in the documentation tests
(DOC-21) against `yantra-probe --demo --api 127.0.0.1:0` with the
environment variables `YANTRA_API` (the base URL, ending in `/api/v1`)
and `TOKEN` (the owner token), in document order, each with `sh -e`;
`curl -f` makes a refused request fail the example.

<!-- example: api-shell -->
```sh
curl -sf "$YANTRA_API/info"
curl -sf -H "Authorization: Bearer $TOKEN" "$YANTRA_API/status"
curl -sf -H "Authorization: Bearer $TOKEN" "$YANTRA_API/levels"
curl -sf -H "Authorization: Bearer $TOKEN" "$YANTRA_API/neighbours"
```

Record for a moment under a name, start the next file, stop, and list
the files:

<!-- example: api-shell -->
```sh
curl -sf -H "Authorization: Bearer $TOKEN" -X POST -d '{"name":"site1"}' "$YANTRA_API/record/start"
curl -sf -H "Authorization: Bearer $TOKEN" -X POST "$YANTRA_API/rotate"
curl -sf -H "Authorization: Bearer $TOKEN" -X POST "$YANTRA_API/record/stop"
curl -sf -H "Authorization: Bearer $TOKEN" "$YANTRA_API/recordings"
```

The same command through `/command`, and a LAN token made and withdrawn:

<!-- example: api-shell -->
```sh
curl -sf -H "Authorization: Bearer $TOKEN" -X POST -d '{"command":"status"}' "$YANTRA_API/command"
curl -sf -H "Authorization: Bearer $TOKEN" -X POST -d '{"enabled":true}' "$YANTRA_API/auth/lan"
curl -sf -H "Authorization: Bearer $TOKEN" -X POST -d '{"enabled":false}' "$YANTRA_API/auth/lan"
```

Stop the probe:

<!-- example: api-shell -->
```sh
curl -sf -H "Authorization: Bearer $TOKEN" -X POST "$YANTRA_API/quit"
```

The test `tests/api/api_client_test.py` (TST-51) is a complete client in
Python's standard library, the WebSocket included.

## Central viewer (Yantra app)

Tools > Remote probes shows this Yantra's API (start, stop, the port and
the token file) and follows other probes: give a probe's address
(`http://host:port`) and its owner or LAN token; the table shows each
probe's state, demo mark, carriers, serving level and the time of its
last answer, read every 2 s through `GET /status` and `GET /levels`
(plain HTTP only).

## Threat model

**Assets.** Control of the probe (start/stop recording, replay, quit: a
denial of the measurement), the recordings (broadcast data, levels), the
tokens and the host.

**Who can reach it.** Nobody until `api.listen` is set; on 127.0.0.1 only
local users and processes. On `0.0.0.0` everyone who can route to the
host: use a firewall, a VPN or a TLS reverse proxy.

| Threat | Mitigation |
|--------|------------|
| Unauthenticated control or reading | bearer token on every endpoint except `/info` (name, version, licence and source link only); 256-bit random tokens; constant-time comparison |
| Token guessing | 5 failures per address per minute, then a 60 s block (429) |
| Token theft from disk | token file 0600 (folder 0700), refused when others can read it; tokens never logged (CivetWeb's messages go to the debug log, without headers) |
| Token theft on the wire | plain HTTP is meant for loopback; use a TLS reverse proxy or a tunnel otherwise; a stolen token is revoked with `/auth/rotate` |
| A viewer that should only watch | the LAN token: read only, accepted only from LAN addresses |
| A malicious web page in the operator's browser (CSRF, DNS rebinding, cross-site WebSocket hijacking) | no cookies or ambient credentials; requests with an `Origin` outside the allow-list are refused, the WebSocket upgrade included; empty allow-list by default |
| Path traversal through downloads | only names of recording files listed in the recording folder (letters, digits, `.`, `_`, `-`; no `..`, no leading dot), regular files only, no symbolic links |
| Oversized or malformed requests | 64 KiB body limit, 4 KiB WebSocket messages, 10 s request timeout, JSON parsed without exceptions, at most 8 live clients |
| Exposure of protected data | only SYSTEM_INFO, LEVEL and QUALITY fields leave the probe; no subscriber addresses, SDS, voice or decrypted content exist in Yantra |
| Behind a reverse proxy every client appears as 127.0.0.1 | then the LAN token check sees loopback: do not hand out the LAN token where the proxy is public |

**Not covered.** A local attacker running as the same user, a
compromised host, a network flood. CivetWeb 1.16 is built without CGI,
file serving, Lua, Duktape, HTTP/2 and TLS, and its form handler is never
called.

## Source offer (NFR-01)

The TETRA radio plugins include osmo-tetra (AGPL-3.0). Whoever uses
Yantra over the network gets the source link in `GET /api/v1/info`,
`GET /api/v1/status` (`source_url`, `api.source_url`) and the status
command reply: the build setting `YANTRA_SOURCE_URL`, default
<https://github.com/peterdewitte/yantra>.
