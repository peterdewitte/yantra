<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Record files (`yantra-radio-records`, version 1)

Reference of the record file format of RAD-65. *Stub: the full reference
with every member follows with the radio API documentation (RAD-97).*

A record file holds what one radio plugin delivered through the radio API
(`include/yantra/radio.h`): records and stream events, nothing else (no IQ,
PDUs or context data apart from a simulated record's `sim_position`,
RAD-19, RAD-26). It is written by the radio test host
(`yantra-radio-host stream --out FILE`, RAD-91) and by yantra-gen with
ground-truth records (RAD-71). Yantra's tests feed these files into its
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
