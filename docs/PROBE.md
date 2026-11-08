<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Headless probe: `yantra-probe` (API-01, API-02, API-03, PLG-24)

`yantra-probe` runs Yantra's measurement engine without a window: the same
radio host, calibration, recording and replay as the app, with the same
plugins. It is the fixed probe of API-03 and, with the probe plugin's API,
the remote probe of API-02 ([API.md](API.md)). Adapted from yantra-v1
c24e8f5 (docs/PROBE.md): indoor only (no GPS), radios through the radio
plugins, no control socket (standard input and the API instead).

## Starting

```sh
yantra-probe --demo                                   # the simulator radio plugin (DEMO-10)
yantra-probe --radio yantra.radio.rtlsdr --record     # a radio plugin, recording at once
yantra-probe --demo --api 127.0.0.1:8470              # with the remote API
yantra-probe --help                                   # every option
yantra-probe commands                                 # the command table
```

The data folder (`--data`, default `$XDG_DATA_HOME/yantra`) holds the
plugin data (`plugin-data/<plugin id>/`) and, by default, the recordings
(`probe/recordings`). Plugin settings for one run are given with `--set
ID:KEY=VALUE`, a plugin is left off with `--disable ID`. Calibration:
`--calibration FILE` (RF-02).

The probe stops on `quit`, SIGTERM or SIGINT: a running recording is
closed, the plugins are stopped and the exit code is 0. Exit code 1: the
probe could not start (no radio, folder not writable); 2: invalid options.

## Commands (API-01)

On standard input (unless `--no-stdin`) one command per line, as text
(`record start survey1`, `record.start name=survey1`, `play
file=survey1_0000.yrec speed=0`) or JSON
(`{"command":"record.start","args":{"name":"survey1"}}`). Each command
gets one JSON reply line on standard output:
`{"ok":true,"command":"record.start","result":{...}}` or `{"ok":false,
"command":"...","error":"..."}`. The commands are listed in
[API.md](API.md#commands); the API maps its requests onto the same
commands. The log goes to standard error.

## Unattended fixed probe (API-03)

```sh
yantra-probe --radio yantra.radio.rtlsdr --record --split-minutes 60 --keep 168 --compress \
             --set yantra.probe:summary.health_s=60 --set yantra.probe:summary.coverage_s=900
```

- Rotating files: `--split-minutes` starts a new recording file every M
  minutes, `--keep N` deletes the oldest beyond N (REC-02 ring buffer),
  `rotate` starts the next file at once.
- Summaries (probe plugin, `yantra.probe`): every `summary.health_s`
  seconds a health line (state, radio, recording, counters, last error)
  and every `summary.coverage_s` seconds a coverage line (per carrier:
  role, records, synchronised records, minimum, maximum and mean level of
  that interval). They are appended as JSON lines to
  `<data>/plugin-data/yantra.probe/summaries/health.jsonl` and
  `coverage.jsonl` (a file is renamed to `.1` past 10 MB), logged in one
  line each, served at `GET /api/v1/summaries` and pushed to live clients.
- Sending the summaries elsewhere is not built in; the files can be
  collected with any tool, or read over the API.

## Remote probes in the app

The probe plugin is also loaded by the app. Tools > Remote probes starts
or stops this Yantra's API and follows other probes (the central viewer of
API-02, see [API.md](API.md#central-viewer-yantra-app)). Without the probe
plugin the app opens no network socket.
