<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Record files (`yantra-radio-records`, version 1)

Reference of the record file format of RAD-65 (RAD-97).

A record file holds what one radio plugin delivered through the radio API
(`include/yantra/radio.h`): records and stream events, nothing else (no IQ,
PDUs or context data apart from a simulated record's `sim_position`,
RAD-19, RAD-26). It is written by the radio test host
(`yantra-radio-host stream --out FILE`, RAD-91) and by yantra-gen with
ground-truth records (RAD-71; schemas `tetra_cell` and `tetra_neighbour`,
see [YANTRA_GEN.md](YANTRA_GEN.md)). Yantra's tests feed these files into its
record sink (Yantra TST-10); it is not a Yantra recording format.

Code: `common/include/yantra/radios/record_file.hpp` (writer, reader and
conversions from the C structs, library `yantra::radios_common`). Example:
`tests/fixtures/record_files/example_v1.records.jsonl`.

## Layout

- UTF-8 text, one JSON object per line (JSON Lines), `\n` after each line.
  Suggested extension `.records.jsonl`.
- Line 1, the **header**: `format` (`"yantra-radio-records"`), `version`
  (1), `producer`, `created_utc_ns`, `plugin` (id, name, version,
  technology_id, licence, api_version `"MAJOR.MINOR"`, capabilities),
  optional `device` and `plan`, and `schemas`: every record schema of the
  plugin with its fields (name, type, group, unit, label, help, flags,
  enum items).
- Then one line per **record**
  (`{"type":"record","schema":ID,"time":{...},"flags":[...],"groups":[...],"values":{NAME:VALUE,...}}`)
  or **stream event** (`{"type":"event","event":NAME,"value":N,"monotonic_ns":T}`).
- Optional last line **end**: `{"type":"end","records":N,"events":N,"dropped":N}`;
  a file without it was cut off (all complete lines are still valid).

## Header line

| Member | Type | Meaning |
|---|---|---|
| `format` | string | `"yantra-radio-records"` |
| `version` | uint | `1` |
| `producer` | string | Program and version, e.g. `"yantra-radio-host 0.1.0"` |
| `created_utc_ns` | int | When the file was started, ns since 1970-01-01 UTC; 0 if unknown |
| `plugin.id`, `name`, `version`, `technology_id`, `licence` | string | From the plugin's descriptor (RAD-12) |
| `plugin.api_version` | string | Radio API version of the plugin, `"1.0"` (RAD-11) |
| `plugin.capabilities` | array of names | `scan`, `hotplug`, `retune_live`, `simulated`, `scene`, `self_calibrated` (RAD-13) |
| `device` | object, optional | The device streamed: `key`, `label`, `driver`, `serial`, `module_version`, `flags` (`in_use`, `no_permission`, `simulated`) (RAD-30) |
| `plan` | object, optional | The plan of the stream: `type` (`serving`, `carriers`, `scan`), `serving_hz`, `carriers_hz` (array), `flags` (`measure_neighbours`), `scan_first_hz`, `scan_last_hz`, `scan_step_hz` (RAD-32) |
| `schemas` | array | Every record schema of the plugin, in descriptor order |

A schema: `id`, `kind` (`measurement`, `scan`, `neighbour`), `label`,
`version`, `groups` (the optional groups it may carry: `quality`, `c1`,
`frame`, `broadcast`, `neighbours`, `sim_position`, `other`) and `fields`.
A field: `name`, `type` (`bool`, `int`, `uint`, `double`, `string`,
`enum`), `group` (`core` or one optional group), `unit`, `label`, `help`,
`flags` (`key`, `default`) and, for an enum, `items` (`value`, `name`,
`label`). The first eight fields of a MEASUREMENT schema are the core
fields `frequency_hz`, `channel`, `role`, `level`, `mcc`, `mnc`, `la`,
`colour_code` (RAD-21).

## Record lines

| Member | Type | Meaning |
|---|---|---|
| `type` | string | `"record"` |
| `schema` | string | The schema id |
| `time.monotonic_ns` | int | Host monotonic clock when the measurement was made (RAD-27) |
| `time.utc_ns` | int | UTC, ns since 1970-01-01; meaningless when `utc_source` is `none` |
| `time.utc_source` | string | `none`, `system` (the computer's clock) or `device` |
| `flags` | array of names | `no_sync`, `overload`, `uncalibrated`, `simulated` |
| `groups` | array of names | The optional groups this record carries |
| `values` | object | Field name to value (see "Values") |

## Event lines

| Member | Type | Meaning |
|---|---|---|
| `type` | string | `"event"` |
| `event` | string | `sync_found`, `sync_lost`, `serving_changed`, `overflow`, `dropped`, `plan_applied`, `scan_done`, `end_of_stream`, `device_lost`, `device_back`, `error` (RAD-25) |
| `value` | int | The event's value (a frequency in Hz, a count, or 0; radio.h `YR_EVENT_*`) |
| `monotonic_ns` | int | Host monotonic time when it was reported |

## End line

`{"type":"end","records":N,"events":N,"dropped":N}`: the number of record
and event lines written and the records the producer dropped (they are
not in the file). Optional; a file without it was cut off.

## Example

`tests/fixtures/record_files/example_v1.records.jsonl` (header shortened):

```json
{"format":"yantra-radio-records","version":1,"producer":"hand-written example","plugin":{"id":"example",...},"schemas":[...]}
{"type":"event","event":"sync_found","value":392012500,"monotonic_ns":1000000}
{"type":"record","schema":"beacon","time":{"monotonic_ns":2000000,"utc_ns":1790000000002000000,"utc_source":"system"},"flags":["simulated"],"groups":["quality","sim_position"],"values":{"frequency_hz":392012500,"channel":3600,"role":1,"level":-48.5,"mcc":901,"mnc":1,"la":100,"colour_code":1,"snr_db":22.0,"sim_floor_id":"f1","sim_x_m":4.0,"sim_y_m":3.0}}
{"type":"end","records":2,"events":1,"dropped":0}
```

Read one with `yantra-radio-host read FILE`; write one with
`yantra-radio-host stream --plugin PLUGIN --duration 10 --out FILE`.

## Values

Values are stored by field name; an absent value has no member. Types:
`bool` true/false, `int` and `enum` signed 64-bit integers (an enum value
is one of its items), `uint` unsigned 64-bit integers, `double` finite
numbers, `string` UTF-8 text of at most 255 bytes. Fields of an optional
group appear only when the record lists that group in `groups`. Bit sets
(flags, groups, capabilities) are arrays of machine names (`no_sync`,
`simulated`, `quality`, `sim_position`, ...).

## Versioning

The reader refuses another `format` and a `version` newer than it knows.
Within version 1, members and line types may be added: a reader ignores
unknown members, unknown line types and unknown flag names. Records match
their schema by id and their fields by name (radio.h, "Versioning").
