<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Requirements

The specifications Yantra was built from. Yantra was built by Claude,
Anthropic's AI assistant, from these two documents alone (plus the
earlier implementations it was allowed to read and copy from), in many
small, checked steps. This folder also describes the method and how many
tokens the build used.

The code, commits and docs of the repository cite the requirement IDs of
these documents (for example "PLG-29" or "RAD-21") instead of copying
their text. To see what the product is and how it is put together, start
with the [README](../README.md) and [ARCHITECTURE.md](../ARCHITECTURE.md).

## Files

| File | What it is |
|---|---|
| [requirements_yantra.md](requirements_yantra.md) | The application: indoor projects, walk tests, interpolation, calibration, UI, exports and reports, plugin hosting, tests, docs, branding (spec 0.6). |
| [requirements_radios.md](requirements_radios.md) | The radio parts: radio API, radio plugins, receiver and TETRA decoder, test-signal generator, test host, their tests and packaging (spec 0.4). |
| [BUILD_PROMPT.md](BUILD_PROMPT.md) | The build method, short version: an orchestrator that never writes code, one fresh subagent per step, a check after every step, the rules and the phases. |
| [RESTART_PROMPT.md](RESTART_PROMPT.md) | How a stopped build is resumed from the build log. |
| [TOKENS.md](TOKENS.md) | Tokens, tool uses and time of the build, per phase and in total. |

Both specifications are Markdown conversions of the original text files,
with every requirement, note, table and version history kept. Folder
paths of the machine they were written on were replaced by plain words
(for example "the requirements project" or "the former yantra-radios
repository"). The text about two repositories is kept as written: it is
history; the "Changes in 0.6" (Yantra) and "Changes in 0.4" (radios)
blocks describe the merge into this one repository.

## Versions

- Yantra requirements: version 0.6 (draft), 2026-11-20.
- Radio requirements: version 0.4 (draft), 2026-11-20.

Each document starts with its version history ("Changes in ..."), newest
first.

## Reading a requirement

Every requirement is one block that starts with its ID, its priority and
its source tag:

> **PLG-29** M [NEW] — One repository ...

- **ID:** a prefix for the area and a number, e.g. `HW` hardware, `RF`
  measurement, `UI` user interface, `REC` recording, `GEO` walk tests,
  `DB` cell database, `EXP` export and reports, `PP` post-processing,
  `API` automation, `NFR` non-functional, `OA` implementation platform,
  `TST` tests, `DOC` and `SCR` documentation and screenshots, `DEMO` demo
  mode, `PLG` plugins, `IND` indoor projects, `BRD` branding (Yantra);
  `RAD` radio parts, `DEC` decoding, `TRM` terminal mode (radios). A
  letter after the number (e.g. `HW-01b`, `DOC-02a`) marks a requirement
  added between two others.
- **Priority:** `M` must (part of the first full version), `S` should, `C`
  could (later).
- **Source tag:**
  - `[FUN]` from the AI-generated functional analysis of a TETRA
    monitoring and coverage survey tool (the functions such a tool
    offers);
  - `[OA]` follows OnAir, an open-source SDR receiver: the same
    technology stack, libraries and engineering practice, but none of its
    code;
  - `[CUR]` curtat.be: Yantra as a reference for building programs with
    Claude;
  - `[MAK]` the example floor plans of the MAK collection (Vienna) used in
    the guided tour;
  - `[NEW]` new: follows from the SDR approach, from common survey
    practice, or fills a gap.
- IDs that were moved, replaced or removed stay in the text with a note
  (e.g. "(moved to yantra-radios, RAD-110.)"); removed IDs are not reused.
- Ranges such as `RAD-20..26` mean every ID in between; `IND-xx` means the
  whole group.
