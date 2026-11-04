<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing to Yantra

Thank you for helping. This page lists the rules every change follows.
Requirement IDs in brackets (e.g. TST-61) refer to Yantra's requirements
specification; code, commits and documentation cite these IDs instead of
quoting the specification. Radio-plugin work happens in the yantra-radios
project (`third_party/yantra-radios`), which has its own CONTRIBUTING.md.

## Commits

- **Small and logical** (TST-61): one commit per coherent piece of work,
  for example the radio host, the record pipeline, one dialog or one test
  suite. Do not mix unrelated changes.
- **Every commit builds and passes its tests**: `ninja -C build` without
  warnings, the full `ctest` run passing, and `reuse lint` compliant.
- **The message says what changed and why** (TST-60): a short subject
  line, then a body with the reason, the requirement IDs it covers and,
  for a fixed or changed test, why the test was wrong.
- **Real dates only** (TST-62): commit dates are the dates the work was
  done. Never backdate commits or rewrite published history to suggest a
  different development period. Work imported from elsewhere keeps its
  original dates and says in the message that it was imported.
- **AI assistance is credited** (TST-63): a commit written with an AI
  assistant ends with a `Co-Authored-By:` trailer naming the assistant,
  for example

      Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>

- Milestones and releases are recorded with their real dates in
  `CHANGELOG.md` (DOC-16).

## Pull requests

Each pull request says how it was tested: which tests ran, on which
operating system, and with which radio (or "no hardware", using the
simulator radio plugin or recorded measurement files) (TST-60).

## Building and testing

The yantra-radios submodule is needed for every build
(`git submodule update --init --recursive`). Builds and tests may use all
cores:

    cmake --preset default            # or: cmake -S . -B build -G Ninja
    ninja -C build
    ctest --test-dir build -j"$(nproc)"

Presets: `default` (build/), `scalar` (build-scalar/: `YANTRA_SIMD=OFF`,
`YANTRA_BUILD_APP=OFF`, the plain C++ fallback must give the same results,
TST-03), `ci` and `ci-scalar` (warnings are errors), `asan-ubsan` and
`tsan` (TST-05).

- Every test runs under ctest (TST-01). Tests live in `tests/<module>/`;
  every `*.cpp` and `*.c` of a folder is linked into one doctest binary
  `test_<module>`, picked up by a glob. A folder with its own
  `CMakeLists.txt` is added as a subdirectory instead.
- Labels: `fast` (default), `slow`, `timing`, and `opt_in`. A `LABELS`
  file in a test folder (one label per line) replaces the default.
  Real-time checks carry `timing`; the sanitizer test presets leave them
  out (`ctest --preset asan-ubsan` = `-LE timing`), the default and scalar
  trees run them.
- Long doctest binaries get one ctest per test case
  (`yantra_doctest_tests()` in `cmake/YantraTests.cmake`, named
  `<group>/<case>`), so `ctest -j` runs their cases in parallel.
- Opt-in suites (label `opt_in`) exist only when `YANTRA_TESTS_OPT_IN` is
  set when ctest runs:

      YANTRA_TESTS_OPT_IN=1 ctest --test-dir build -L opt_in -j"$(nproc)"

- New features come with tests. Never delete, skip or weaken a test, or
  loosen a tolerance, to make it pass. If a test is wrong, fix it and
  explain why in the commit message. Golden files change only on purpose,
  with the reason in the commit message.
- Tests wait on observable conditions, not on fixed sleeps, and must pass
  under parallel load (`ctest -j"$(nproc)"`).
- `tools/ci/lint.sh` checks shell, Python and YAML syntax (TST-02); it
  also runs as the ctest `repo_lint`, and `reuse lint` as `reuse_lint`.

## Licensing

<!-- REUSE-IgnoreStart -->
- Yantra is GPL-3.0-or-later (NFR-01). Every file carries an SPDX header:

      SPDX-FileCopyrightText: 2026 The Yantra contributors
      SPDX-License-Identifier: GPL-3.0-or-later

  in the file's own comment syntax. Files that cannot hold a comment
  (images, binary fixtures, JSON) get an entry in `REUSE.toml`. Run
  `reuse lint` before committing.
<!-- REUSE-IgnoreEnd -->
- Third-party code, fonts and data are added only with an entry in
  `THIRD_PARTY.md`, a `REUSE.toml` entry where needed and the licence text
  in `LICENSES/`. Only the libraries and tools of the specification's
  section 16.2 are used.
- Data whose licence forbids redistribution or adaptation (for example
  the MAK floor plans, DEMO-12) is never committed, packaged or shown in
  screenshots.
- The curtat.be logo (`data/branding/`) is a trademark of Curtat BV, not
  GPL; forks remove or replace it (BRD-04). The Yantra logo (`data/logo/`)
  is GPL-3.0-or-later and is used as supplied (BRD-05, BRD-07).

## Scope and sources

- Yantra handles broadcast and system information, levels and quality
  only. Contributions for short data messages, subscriber addresses,
  voice or decryption are not accepted.
- Yantra is an indoor tool: no drive tests, GPS, geographic coordinates
  or maps, not even as a plugin.
- Radio plugins (yantra-radios) process their own IQ samples and give
  Yantra measurement records only. Yantra has no DSP, decoder, SoapySDR
  or IQ code; it depends on yantra-radios, never the reverse (PLG-26).
- Code is written for Yantra from public standards. Do not copy code from
  other receivers, including OnAir (OA-04), or use material from closed
  products.
- The core stays technology-neutral: TETRA- and radio-specific code lives
  in plugins (PLG-01).
- No personal data (names, e-mail or home addresses, local paths) in code,
  documentation, fixtures, package metadata or requests to external
  services.

## Code style

- C++20, 4-space indent, lines up to about 120 characters; `snake_case`
  functions and variables, `PascalCase` types, `kName` constants, a
  trailing `_` on private members.
- Headers start with a comment saying what the file is for and which
  requirement IDs it implements; `#pragma once` (C headers: include
  guards).
- The plugin API (`include/yantra/plugin.h`) is plain C.
- Keep the code portable to Windows (MSYS2 UCRT64): platform code behind
  small interfaces.
- Python tools use the standard library; shell scripts use `set -eu`
  (bash: `set -euo pipefail`) and pass `tools/ci/lint.sh`.
