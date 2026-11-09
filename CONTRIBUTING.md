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

[BUILD.md](BUILD.md) has the dependencies, every build tree and the
options; [ARCHITECTURE.md](ARCHITECTURE.md) the layers and threads. The
yantra-radios submodule is needed for every build
(`git submodule update --init --recursive`; its URL is relative,
`../yantra-radios`, so yantra-radios is cloned next to yantra). Yantra
compiles only its radio API header (`yantra::radio_api`) and builds its
radio plugins as a separate build (`cmake/YantraRadiosBuild.cmake`).
Moving the pin is its own commit and needs Yantra's full test suite and
the radio test kit to pass first (PLG-26). Builds and tests may use all
cores:

    cmake --preset default            # or: cmake -S . -B build -G Ninja
    ninja -C build
    ctest --test-dir build -j"$(nproc)"

Before a commit: `ninja -C build` without warnings, the full `ctest`
passing and `reuse lint` clean. A change to the measurement code, the ray
tracer or build flags also runs the scalar tree (`cmake --preset scalar`,
TST-03) and the sanitizer trees (`asan-ubsan`, `tsan`, TST-05) with
`ctest --preset <tree>`; a change of the plugin set runs the plugins-off
tree (PLG-01).

### Adding a test

- Every test runs under ctest (TST-01). Tests live in `tests/<module>/`;
  every `*.cpp` and `*.c` of a folder is linked into one doctest binary
  `test_<module>` with `tests/test_main.cpp`, picked up by a glob
  (`yantra_add_test_dir` in `tests/CMakeLists.txt`; per-case ctests and
  labels in `cmake/YantraTests.cmake`). A folder with its own
  `CMakeLists.txt` is added as a subdirectory instead; test plugins and
  other fixtures go in a subfolder so the glob does not pick them up.
- Test data lives in `tests/fixtures/` or next to the test; golden files
  are compared with `tests/golden.hpp`. Tests write only into their
  scratch folder in the build tree and never use the real home folder
  (set `YANTRA_USER_PLUGIN_DIR`, `XDG_*` as the existing tests do).
- A new parser of untrusted input gets a fuzz target in `tests/fuzz`
  (TST-12, BUILD.md).
- A new window or dialog gets a UI lint visit
  (`tests/app/ui/test_ui_lint.cpp`), NL/FR/DE texts
  (`tools/ci/lint_ui.py`) and, when the manual shows it, a screenshot
  scene (`app/gui/screenshot.cpp`, SCR-03).
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
  The documentation checks (label `docs`) check relative links, the
  images of the manual and README (SCR-07), code quoted in
  docs/PLUGINS.md and the Doxygen reference of the plugin API.

### CI

GitHub Actions (`.github/workflows/`, BUILD.md "CI") runs the lint and a
Linux build with warnings as errors and the fast tests on every push and
pull request, and all trees with all tests nightly. The Windows workflow
is written but not run (DEFERRED (Windows)).

### Documentation

- A change that users see updates the manual (`docs/manual/`), the
  in-app help texts (DOC-07) and `CHANGELOG.md` (DOC-16).
- A change of the plugin API updates `docs/PLUGIN_API_CHANGELOG.md`
  (DOC-18) and the Doxygen comments of `include/yantra/plugin.h`.
- A change of a stored file format updates its page in `docs/formats/`
  (NFR-05) and its version number.
- A complete example in the format or API docs gets the line
  `<!-- example: <kind> -->` right before its code block (DOC-21): the
  ctest `doc_examples` reads it back with the format's reader (the kinds
  are listed in `tests/doc_examples/test_doc_examples.cpp`; an unknown
  kind fails), and `doc_api_examples` runs the `api-shell` blocks of
  `docs/API.md` against `yantra-probe --demo`. Shortened examples carry
  no mark.
- A change of the report or the demo regenerates `docs/samples/` with
  `ninja -C build sample_report` in the same commit; a changed screen
  regenerates its screenshots with `tools/screenshots/screenshots.py`.
- Commit a document together with the files it links to, so every
  commit passes the doc checks.

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
