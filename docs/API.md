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

    yantra.probe: api http port 8470; token file /home/.../api_tokens.json

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

| Method and path | Token | Reply |
|---|---|---|
| `GET /info` | none | `name`, `software`, `api_version`, `licence`, `source_url` |
| `GET /status` | read | `status` (the health summary, see below), `api` (`version`, `server`, `licence`, `source_url`, `access`), `demo`, `source_url` |
| `GET /commands` | read | `commands`: `command`, `path`, `args` (`name`, `required`), `help` |
| `POST /command` | owner | body `{"command":"record.start","args":{"name":"x"}}`: the command reply |
| `POST /<command path>` | owner | e.g. `POST /record/start` with an optional JSON object of arguments |
| `GET /cells` | read | `carriers`: `frequency_hz`, `technology`, `schema`, `role`, `synced`, `level_dbm`, `calibrated`, `simulated`, `age_s`, `sysinfo`, `levels`, `quality`, `neighbour_count` |
| `GET /neighbours` | read | `broadcast`: per broadcasting carrier its list (`entry_count`, `neighbours` with `entry_index` and the list's fields); `measured`: the measured neighbour carriers |
| `GET /levels` | read | `carriers`: `frequency_hz`, `role`, `level_dbm`, `calibrated`, `levels`, `quality`, `age_s` |
| `GET /summaries` | read | `health` (now) and `coverage` (the last one, or null) |
| `GET /recordings` | read | `files`: `name`, `size`, `modified` (UTC), `url` |
| `GET /recordings/<file>` | read | the file (`.yrec`, `.yrec.zst`) |
| `POST /auth/rotate` | owner | `token`: the new owner token |
| `POST /auth/lan` | owner | body `{"enabled":true|false}`; `lan_token` or null |
| `GET /live` (WebSocket) | read | the live view, below |

Carriers silent for 30 s drop out of the views. `sysinfo` holds the
SYSTEM_INFO fields of the newest synchronised record (enumerations by
their names); `levels` and `quality` map a field name to
`{"value": ..., "unit": "..."}`; `level_dbm` is the core `level` field,
in dBm through the calibration (dBFS and `calibrated: false` without
one).

### Commands

The commands of API-01, the same as on the runner's standard input
([PROBE.md](PROBE.md)) and through the plugin API's `control` service
([PLUGIN_API.md](PLUGIN_API.md)):

| Command | Path | Arguments | Does |
|---|---|---|---|
| `record.start` | `/record/start` | `name` (optional; default date and time) | starts recording (monitoring must run) |
| `record.stop` | `/record/stop` | | stops it; the reply lists the files |
| `monitor.on` | `/monitor/on` | | opens the configured radio and measures |
| `monitor.off` | `/monitor/off` | | closes the radio (a recording stops) |
| `play` | `/play` | `file` (a recording file or folder), `speed` (1 real time, 0 as fast as possible, 0.1 to 100) | replays a recording |
| `sound.on`, `sound.off` | `/sound/on`, `/sound/off` | | audible alerts on/off |
| `status` | `/status` (POST) | | the status as a command reply |
| `rotate` | `/rotate` | | closes the recording file and starts the next |
| `quit` | `/quit` | | stops recording and the radio and exits (headless runner) |

Command reply:

```json
{"format": "yantra-api", "version": 1, "type": "command", "ok": true, "command": "record.start",
 "result": {"recording": true, "name": "survey1", "file": "survey1_0000.yrec", "files": 1}}
```

or `"ok": false` with `"error"` (HTTP 422). The desktop app runs commands
on its UI thread: there a command reply says `"queued": true`, and the
status shows its effect.

### Status

The health summary of the runner (the app gives the same fields where it
has them): `application`, `version`, `source_url` (NFR-01), `state`
(`stopped`, `monitoring`, `recording`, `playing`), `monitoring`, `demo`,
`radio` (`plugin`, `device`, `label`, `simulated`, `state`, `records`,
`dropped`, `reopens`), `recording` (`state`, `name`, `file`, `files`,
`durable_inputs`, `dropped`, `ring_deleted`), `play` (`state`, `source`,
`position_s`, `duration_s`, `speed`), `pipeline` counters, `cycles`,
`carriers`, `synced_carriers`, `sound`, `plugins`, `recording_folder`,
`uptime_s`, `last_error`.

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

```sh
yantra-probe --demo --api 127.0.0.1:8470 &
T=$(python3 -c 'import json,os; print(json.load(open(os.path.expanduser("~/.local/share/yantra/plugin-data/yantra.probe/api_tokens.json")))["token"])')
curl -s http://127.0.0.1:8470/api/v1/info
curl -s -H "Authorization: Bearer $T" http://127.0.0.1:8470/api/v1/levels
curl -s -H "Authorization: Bearer $T" -X POST -d '{"name":"site1"}' http://127.0.0.1:8470/api/v1/record/start
curl -s -H "Authorization: Bearer $T" -X POST http://127.0.0.1:8470/api/v1/quit
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
