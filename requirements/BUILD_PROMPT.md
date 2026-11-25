<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Build prompt (short version)

This is the method Yantra was built with: an AI assistant (Claude) builds
the whole product from the two specifications,
[requirements_yantra.md](requirements_yantra.md) and
[requirements_radios.md](requirements_radios.md), unattended, in many
small checked steps. The full prompt that was used also held the rules of
one particular build machine; this version keeps only the method.

## Roles

- **Orchestrator.** The main session plans and checks, and never writes
  code, tests or docs itself. It plans each phase as a list of 3-6 steps
  (each a few hours of work that ends in working, committed code), runs
  the steps one at a time, checks each one and decides: next step, a
  fix-up step, or BLOCKED.
- **One fresh subagent per step.** Every step runs in a new subagent with
  a clean context, never a fork of the orchestrator. Its brief is short
  and complete: the requirement IDs of the step, what to build, the
  tests and docs expected (screenshots for UI), what earlier steps left
  behind, known pitfalls, the rules below, and what to report.
- **Report.** The subagent ends with at most about 30 lines: STEP DONE or
  STEP INCOMPLETE, the commits, the requirement IDs done / partly done /
  not done (with the reason), test totals, changed tests or golden files
  and why, known issues and open questions. A subagent that runs out of
  room stops at a clean commit and reports what is left.

## Unattended

The build does not stop to ask. When a choice is needed it takes the
option the spec recommends (otherwise the simplest one that keeps the
requirements), writes the decision in the build log and carries on. After
two failed attempts at the same item it is marked BLOCKED with the reason,
and the build moves on.

## Build log (the resume point)

The orchestrator keeps a build log outside the code repository. At the
top: a **Status** block (current phase, last commit, next step, test
baseline, open BLOCKED items) and a short **Current state** (commit table
per phase, the plan in progress, decisions, rejected options, gotchas,
open questions). Below it, one entry per step: what was done, commits,
test totals, decisions, problems and what is left. Old entries move to an
archive. Every subagent run also gets one row in a token log (see
[TOKENS.md](TOKENS.md)).

## Checking a step

The subagent's report is model output, not proof. After every step the
orchestrator checks for itself:

1. the working tree is clean and every new commit has the trailer;
2. the build has no warnings; the fast test suite and the timing tests
   pass;
3. test totals did not drop without a logged reason; changed golden files
   are explained in their commit message;
4. the build log has the step entry and an updated Status block;
5. no stray processes, test servers or untracked files;
6. no forbidden content: no licence-restricted images in the tree, no
   personal data, no radio or IQ code outside the radio plugins (the
   radio boundary);
7. for UI steps, a look at one or two new screenshots;
8. claims such as "library X is not allowed" or "feature Y is impossible"
   are checked against the spec before they are accepted.

At the end of each phase: a requirement coverage check (every ID of the
phase's spec sections is done, scheduled later, or recorded as partial or
blocked with a reason).

## Rules for every step

- **Commits:** small and logical; each one builds and passes its tests;
  the message says what changed and why and cites requirement IDs instead
  of copying spec text; real dates only; history is never rewritten or
  backdated; an AI co-author trailer on every commit; no push by the
  build.
- **Tests:** per commit a warning-free build and the targeted tests of
  what it touches; per step the fast suite and the timing tests; slow
  tests only when the step changes what they test; at the end of a phase
  the full suite, the other build trees (gcc, scalar, plugins off), the
  sanitizer builds and the Windows tests. A test is never deleted,
  skipped or loosened to make it pass; tests wait on observable
  conditions, not on time.
- **Platforms:** build and test on Linux; Windows is cross-built on Linux
  and its tests run on Windows.
- **Reference code:** an earlier implementation may be read, built and
  copied from (the commit message names the source), adapted to the
  specs; it is never changed.
- **Excluded sources:** no code of OnAir or any other excluded project, no
  proprietary survey software, file formats or dongles; only the
  libraries and tools the specs list.
- **Scope:** broadcast and system information, levels and quality only;
  no SDS, subscriber addresses, voice or decryption; indoor only (no
  drive tests, GPS or maps). Radio plugins hand Yantra measurement
  records only; no IQ samples or PDUs reach the application.
- **Housekeeping:** stop every process you start; leave no files outside
  the repository's ignored build folders; no spec, prompt or assistant
  files in the code repository (this folder was added afterwards, on
  purpose, to publish the specs).

## Phases

The radio parts were built first, then the application on top of them,
indoor first. Each later phase follows a new version of the specs.

1. Repositories: skeletons, toolchain, licensing, CI and test layout.
2. Radio API and plugin API.
3. TETRA test-signal generator and building model.
4. Receiver and decoder inside the radio parts.
5. Radio plugins (HackRF, simulator, generic SoapySDR, example).
6. Measurements in Yantra: record pipeline, calibration, TETRA plugin.
7. Indoor core: projects, plan import, walk tests, recording, cell database.
8. Ray tracing and demo projects (later replaced in phase 14).
9. Desktop app, indoor first: UI, menus, generated radio windows, tour.
10. Export and reports: CSV, DXF, coverage and acceptance reports.
11. Feature and extra plugins: probe, remote API, TETRA terminal.
12. Documentation and release: manual, quick starts, screenshots, packages.
13. Clang everywhere, Windows cross build and tests (specs Yantra 0.3, radios 0.2).
14. Simpler indoor workflow: walk test and IDW interpolation (Yantra 0.4).
15. Weak demo area (a FAIL report), report popup, "About building" tab, plan notes, spectrum, packages workflow (Yantra 0.5, radios 0.3).
16. One repository with a plugin-first tree (Yantra 0.6, radios 0.4).

After these: a final report (status of every requirement ID, tests, open
questions), release 0.3.0, the GitHub CI and the release 0.3.1, and a demo
video made outside the repository.
