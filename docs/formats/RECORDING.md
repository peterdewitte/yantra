<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Recording format (`yantra-recording/1`)

A Yantra recording (REC-03) keeps a walk such that replay (REC-07) runs
the same measurement pipeline again and gets the same cycles and
statistics as the live run (docs/MEASUREMENT_PIPELINE.md, "Recording and
replay"). It is an open, documented format: JSON Lines (one JSON object
per line, UTF-8, `\n` after every line), optionally compressed with zstd.
The format name and version are in the header line; this document
describes version 1. The test "recording: docs/formats/RECORDING.md
documents ..." (tests/recording) checks this document against the code.

Recordings are files of their own, next to the project; the project file
does not hold them; it lists them (name, size, SHA-256, header) in its
`recordings` table ([PROJECT.md](PROJECT.md), schema 4).

## Files

| Item | Value |
|------|-------|
| Name | `<session>_<nnnn>.yrec`, or `.yrec.zst` when compressed; `nnnn` is the file index from `0000` (REC-02) |
| Quick-record session name | `rec_YYYYMMDD_HHMMSS` (UTC), then `_2`, `_3` ... when it exists (REC-01) |
| Session name | 1..120 bytes, no `/`, `\`, control characters, not `.` or `..` |
| Split | by time (1..60 min of cycle time) and/or by message count (`rec` + `event` lines per file) (REC-02) |
| Compression | none, or zstd: one complete zstd frame (level 3, content checksum) per flush |
| Durability | flushed and `fdatasync`ed every 0.25 s (at most 0.5 s): a crash loses at most that (REC-10) |
| Line limit | 16 MiB; longer lines are skipped as damaged |

Every file replays on its own: it begins with a header, then a `state`
line (a keyframe of the pipeline) and declares every `source` and `cal`
before it is referred to. A session's files follow each other on one
time line; a paused session (marker "new file + pause", REC-05) leaves a
gap there. "Continue session" (REC-02) opens the next file number with
the same session id and `"continued": true`; marker numbers go on.

## Times

`mono` values are nanoseconds on the live pipeline's monotonic clock (the
clock of the cycle grid); replay sets its own clock to them, so cycles
come at the same instants. Replay time is the time since the first
file's `file_started_mono`; later files follow at their own start
offsets. `utc` values are nanoseconds since 1970 (UTC). `time` of a
record is `[monotonic_ns, utc_ns, utc_source]` as the radio stamped it.

## Line types

Each line is an object with its type in `t`.

| `t` | When | Content |
|-----|------|---------|
| `header` | first line, once | format, tool, session, file start, header information (REC-03), pipeline configuration, split, compression |
| `source` | before the first record of a radio schema | the radio plugin and its schema |
| `cal` | before the first record with that level correction | a level correction (calibration) |
| `rec` | every record the pipeline took in | the record, its arrival time and its correction |
| `event` | every radio event the pipeline took in | the event |
| `cycle` | every measurement cycle | its tick time and a summary of its samples |
| `state` | at the start of every file and every `keyframe_s` | the pipeline's state, for seeking |
| `pos` | a plan position (IND-20) | floor and position |
| `floor` | the walk changes floor | floor id and name |
| `walk` | a walk-test action (IND-20, GEO-02, GEO-05) | what the surveyor did on the plan |
| `marker` | a marker (REC-05) | number, label, action |
| `end` | last line of a cleanly closed file | why it was closed |

A reader skips lines of a type it does not know (a later Yantra may add
types; the version only goes up when old readers would read a file
wrongly) and counts them.

### `header`

| Key | Content |
|-----|---------|
| `format` | `"yantra-recording/1"` |
| `tool` | `name` ("Yantra"), `version` |
| `session` | `id` (16 hex digits, the same in all files of a session), `name`, `file_index`, `started_utc` (ISO 8601), `started_ns` (UTC ns), `continued` |
| `file_started_utc`, `file_started_ns` | this file's start (UTC; ISO 8601 and ns) |
| `file_started_mono` | this file's start on the pipeline clock |
| `next_marker` | the number of the next marker at the file's start |
| `radios` | per radio: `plugin`, `plugin_version`, `device`, `driver`, `serial`, `label` |
| `calibration` | per calibration table entry used: `key`, `created_unix`, `valid_days`, `method`, `simulated` |
| `antenna` | per radio: `plugin`, `device`, `gain_dbi`, `cable_loss_db`, `attenuator_db` |
| `cell_db` | the cell database used (name), or null |
| `notes` | free text |
| `project` | `file` (project file name), `floor` (floor id), or null |
| `pipeline` | the cycle configuration: `period_ns`, `stale_after_ns`, `drop_after_ns`, `scan_stale_after_ns`, `scan_drop_after_ns`, `late_after_ns`, `max_catch_up`, `min_level`, `max_level`, `ms_power_dbm` |
| `split` | `minutes` (0 = off), `messages` (0 = off) |
| `keyframe_s` | seconds between `state` lines |
| `compression` | `"none"` or `"zstd"` |
| `cut` | for a file cut from another (REC-08): `from_ns`, `to_ns`, `source`, `source_session`; else null |

### `source`

| Key | Content |
|-----|---------|
| `id` | number from 1, per file |
| `plugin`, `plugin_version` | the radio plugin |
| `technology` | its technology id |
| `capabilities` | its capability bits (`YR_CAP_*` of yantra/radio.h) |
| `schema_index` | the schema's index in the plugin |
| `schema` | the radio schema, in the shape of the yantra-radios record files: `id`, `label`, `kind`, `version`, `groups`, and `fields` with per field `name`, `label`, `unit`, `help`, `type`, `group`, `flags` and, for enums, `items` (`value`, `name`, `label`) |

### `cal`

| Key | Content |
|-----|---------|
| `id` | number from 1, per file |
| `calibrated` | whether the level is calibrated dBm |
| `offset_db`, `correction_db` | the calibration offset and the antenna/cable correction |
| `source` | where it came from (`"table"`, `"radio"`, ...) |
| `key` | the calibration key |
| `date_unix` | the calibration date |
| `expired`, `extrapolated` | its flags |

### `rec`

| Key | Content |
|-----|---------|
| `n` | the input number (inputs are records and events, numbered by the pipeline in the order it took them in) |
| `mono` | its arrival time on the pipeline clock |
| `src` | its `source` id |
| `dev` | device key |
| `stream` | stream id |
| `groups` | the groups present (`YR_GROUP_*` bits) |
| `time` | `[monotonic_ns, utc_ns, utc_source]` |
| `flags` | record flags (`YR_RECORD_*` bits) |
| `v` | the values, one per schema field in order; null when absent; enums as their number |
| `cal` | the `cal` id of its level correction; absent when uncorrected |

### `event`

| Key | Content |
|-----|---------|
| `n`, `mono` | as for `rec` |
| `kind` | `stream`, `started`, `stopped`, `lost`, `reopened` or `failed` |
| `event` | for `stream`: the event's name (`dropped`, `sync_lost`, ...) or number |
| `plugin`, `dev`, `stream` | where it came from |
| `value`, `time`, `message` | its value, time and text |

### `cycle`

| Key | Content |
|-----|---------|
| `seq` | the cycle number |
| `mono`, `utc` | the cycle's end (the grid time) |
| `built` | the tick time it was built at |
| `inputs` | the number of the last input taken in before it |
| `period` | the period (ns) |
| `flags` | cycle flags |
| `samples` | per carrier: `[device, schema, role, Hz, entry, level, flags, records in cycle, age ns]`; level null when none |

Replay compares its cycles with these summaries and reports
differences (another technology plugin or Yantra version).

### `state`

A keyframe: what the pipeline holds, so replay can start there.

| Key | Content |
|-----|---------|
| `mono` | when it was taken |
| `next_end` | the end of the next cycle |
| `seq` | the next cycle number |
| `inputs` | the number of the last input it includes |
| `stats` | the pipeline statistics: `records`, `accepted`, `generic`, `unmapped`, `schema_mismatch`, `implausible`, `out_of_order`, `duplicates`, `late`, `events`, `cycles`, `carrier_samples`, `held`, `stale`, `expired`, `skipped_cycles`, `c1_radio`, `c1_estimated`, `c1_failed` |
| `devices` | per device: `[plugin, device, lost, stopped, data_loss]` |
| `carriers` | per carrier: its newest record (keys of `rec` without `n`), `mono` its arrival, `cal`, `fresh` (records in the open cycle), `late`, `sync_lost` |

Inputs and cycles that the pipeline took in while a new file was being
opened can land in the new file although that file's first `state`
already includes them, or in the old file after it. Replay therefore
reads files that follow on directly (see "Replay") as one stream, merged
by input and cycle number.

### `pos`, `floor`, `marker`, `end`

| Line | Keys |
|------|------|
| `pos` | `mono`, `floor` (floor id), `x`, `y` (plan units), `xm`, `ym` (metres, or null), `kind` (`click`, `point`, `simulated`, `cursor`), `label` |
| `floor` | `mono`, `floor`, `name` |
| `walk` | `mono`, `floor` (floor id), `action`, `x`, `y` (plan units, or null), `xm`, `ym` (metres, or null), `mode` (`point_and_click` or `continuous`), `kind` (`walk`, `manual` or `simulated`), `seconds` (averaging window of a manual point), `label` |
| `marker` | `mono`, `n` (number, through the session), `label`, `action` (`mark`, `new_file`, `new_file_pause`) |
| `end` | `mono`, `reason` (`stop`, `split_time`, `split_count`, `new_file`, `new_file_pause`), `messages` (`rec` + `event` lines in the file), `inputs` (the last input number) |

### Walk-test actions (`walk`)

A walk test (core/survey/walk_test.h, [SURVEY.md](../SURVEY.md)) writes every action
of the surveyor as a `walk` line, so a replay of the recording places the
samples exactly as the live walk test did (the same points, IND-20,
IND-30). `action` is one of:

| `action` | Meaning | Keys used |
|----------|---------|-----------|
| `start` | a walk test begins on `floor` | `kind`, `mode`, `label` (its name) |
| `click` | a click on the plan (GEO-02 point and click) | `x`, `y`, `xm`, `ym` |
| `pause` | pause: the open segment is dropped | |
| `delete_last` | delete last segment | |
| `mode` | the walk mode changed | `mode` |
| `point` | a manual point (GEO-05): measure `seconds` at `x`, `y` | `x`, `y`, `xm`, `ym`, `seconds`, `label` |
| `floor` | the walk moved to `floor` (a new walk test on that floor follows it) | |
| `end` | the walk test ended | |

Replay feeds the actions in time order together with the cycles; a walk
test is replayed from the file it started in (a file of a split walk
holds only the actions made while it was written).

## Reading

- A file without a valid header, or with no `state` line, is refused.
- A newer `format` version is refused ("made by a newer Yantra").
- A last line without its `\n`, or a cut last zstd frame, is a crash
  while recording: every complete line before it is kept and the file is
  marked truncated (REC-10).
- A damaged line in the middle (not JSON, or wrong content) is skipped
  and counted; so is a damaged zstd frame and everything after it.
- A file without `end` was not closed cleanly; it still replays.

## Replay

Replay (core/include/yantra/core/recording/replay.h) builds a
measurement pipeline with the header's configuration and the live run's
technology plugins. Files that follow on directly (the same session id,
the next file index, not `continued`, the previous one ended by
`split_time`, `split_count` or `new_file`) form a run. A run starts from
the first `state` of its first file; then every input of the run is fed
in number order at its arrival time with its recorded correction, and
the pipeline ticks at every cycle's `built` time. Inputs numbered up to
that `state`'s `inputs`, and cycles before its `seq`, are already in it.
The next run (after a pause, or a continued session) starts from its own
first `state`. Seeking to an instant restores the newest `state` before
the newest cycle at or before that instant and plays on silently to it.

## Example

```
{"t":"header","format":"yantra-recording/1","tool":{"name":"Yantra","version":"0.1.0"},"session":{"id":"3f0c9a1b2d4e5f60","name":"walk","file_index":0,"started_utc":"2026-09-21T10:00:00Z","started_ns":1790000000000000000,"continued":false},"file_started_utc":"2026-09-21T10:00:00Z","file_started_ns":1790000000000000000,"file_started_mono":100000000000,"next_marker":1,"radios":[],"calibration":[],"antenna":[],"cell_db":null,"notes":"","project":null,"pipeline":{"period_ns":1000000000,"...":0},"split":{"minutes":0,"messages":0},"keyframe_s":10,"compression":"none","cut":null}
{"t":"state","mono":100000000000,"next_end":101000000000,"seq":1,"inputs":0,"stats":{"records":0,"...":0},"devices":[],"carriers":[]}
{"t":"source","id":1,"plugin":"test.radio","plugin_version":"1.2.3","technology":"xtech","capabilities":0,"schema_index":0,"schema":{"id":"x_cell","label":"Cell","kind":"measurement","version":1,"groups":["core"],"fields":[{"name":"frequency_hz","label":"Frequency","unit":"Hz","help":"","type":"uint","group":"core","flags":["key"]}]}}
{"t":"cal","id":1,"calibrated":true,"offset_db":-20.0,"correction_db":1.5,"source":"table","key":"drv|sn|","date_unix":1790000000,"expired":false,"extrapolated":false}
{"t":"rec","n":1,"mono":100250000000,"src":1,"dev":"dev:0","stream":1,"groups":0,"time":[100247000000,1790000000247000000,1],"flags":0,"v":[390012500],"cal":1}
{"t":"cycle","seq":1,"mono":101000000000,"utc":1790000001000000000,"built":101000000000,"inputs":4,"period":1000000000,"flags":0,"samples":[["dev:0","x_cell","serving",390012500,0,-74.5,0,4,0]]}
{"t":"end","mono":102000000000,"reason":"stop","messages":8,"inputs":8}
```
