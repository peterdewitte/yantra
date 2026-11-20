<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing to Yantra

Thank you for helping. This page lists the rules every change follows.
Requirement IDs in brackets (e.g. TST-61) refer to Yantra's requirements
specification; code, commits and documentation cite these IDs instead of
quoting the specification; the radio parts (radio API, radio plugins,
generator) also have RAD-xx IDs. Contributors are listed in
[CONTRIBUTORS.md](CONTRIBUTORS.md).

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
radio parts need their osmo-tetra submodule
(`git submodule update --init --recursive`). One CMake project builds
everything (PLG-29): the application, the radio API (`radio-api/`), the
radio plugins and Yantra's plugins (`plugins/`), their common code
(`radio-common/`), the generator (`gen/`) and the tools; one ctest runs
all tests, labelled by area (`app`, `core`, `radio-api`,
`plugin:<name>`, `radio-common`, `gen`, `docs`, `lint`, `packaging`).
`-DYANTRA_RADIO_PARTS_ONLY=ON` (preset `radio-parts`) builds the radio
parts alone (RAD-141). Builds and tests may use all cores:

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
- The radio parts' tests live in `tests/radio/<module>/` (the same glob,
  `yantra_radio_add_test_dir` in `tests/radio/CMakeLists.txt`, with
  `tests/radio/test_main.cpp`), their data in `tests/radio/fixtures/`.
- Test data lives in `tests/fixtures/` or next to the test; golden files
  are compared with `tests/golden.hpp`. Tests write only into their
  scratch folder in the build tree and never use the real home folder
  (set `YANTRA_USER_PLUGIN_DIR`, `XDG_*` as the existing tests do).
- A new parser of untrusted input gets a fuzz target in `tests/fuzz`
  (TST-12, BUILD.md), or in `tests/radio/fuzz` for the radio parts
  (RAD-96).
- A new window or dialog gets a UI lint visit
  (`tests/app/ui/test_ui_lint.cpp`), NL/FR/DE texts
  (`tools/ci/lint_ui.py`) and, when the manual shows it, a screenshot
  scene (`app/gui/screenshot.cpp`, SCR-03).
- Labels: `fast` (default), `slow`, `timing`, and `opt_in`. A `LABELS`
  file in a test folder (one label per line) replaces the default.
  Real-time checks carry `timing`; the sanitizer test presets leave them
  out (`ctest --preset asan-ubsan` = `-LE timing`), the default and scalar
  trees run them.
- GUI layout (TST-52): `test_app_overflow` (label `slow`, one ctest per
  UI language and layout, about 7 minutes each) draws every window,
  tooltip and report in EN/NL/FR/DE and the pseudo-language
  (`--language pseudo`: every text 40 % longer) at the supported sizes and
  UI scales and fails on any text that is clipped, runs past its window,
  column or control, or overlaps another. Its list of failures is also
  written to `build/tests/app/scratch/test_app_overflow/tst52_<lang>_<layout>.txt`
  with a picture per failing frame in `tst52_shots/`. A new dialog or page
  goes into `tests/app/ui/ui_visit.h` (the UI lint walks it too); a new
  text needs room in every language: let rows wrap
  (`gui_detail::same_line_if`), size by the font (`gui_detail::em`), or
  shorten with "..." and the whole text on hover (`gui_detail::text_ellipsis`).
  Known exceptions go in `tests/app/overflow/exceptions.txt` with a reason;
  a release needs it empty (`YANTRA_RELEASE_CHECK=1`).
- Radio parts, label `timing`: real-time and pacing checks (`test_simulator_timing/*`,
  `conformance_simulator` with the kit's one-record-per-second rule,
  `perf_engine`, `bench_limits`). The sanitizer trees run 4-13x slower
  than real time, so their test presets and the ci-full sanitizer jobs
  leave the label out; the default and scalar trees run it. The hang
  guards (ctest TIMEOUT of the per-case tests, the simulator tests' stream
  limits) are multiplied by `YANTRA_TEST_TIME_SCALE` (CMake cache, 5 in
  the ASan/UBSan tree, 10 in the TSan tree, 1 otherwise); the checks stay the same:

      ctest --preset asan-ubsan -j"$(nproc)"   # = ctest --test-dir build-asan -LE timing
      ctest --preset tsan -j"$(nproc)"

  A stream that runs as fast as the CPU allows (the fake SoapySDR module
  with `"paced": false`) gets no wall-clock limit: the time its records
  take grows with the machine's load (a full parallel ASan run made such
  a case over 4x slower than alone). Limit it in seconds of signal, with
  a stall guard for a hang (`tests/radio/stream_guard.hpp`).

  The time-limited ones (`perf_engine`, `bench_limits`, the
  `*_timing/*` cases) are RUN_SERIAL, so ctest runs no other test next to
  them. Load from outside ctest still counts: another tree building or
  testing at the same time makes them fail. On a CPU with hyper-threads
  or efficiency cores a busy sibling slows a thread down even when the
  other program is niced (`perf_engine`: load 0.25 alone, 0.44-0.48 with
  seven busy loops on a 12-thread i7-1365U, in CPU time as well as wall
  clock; the simulator at playback speed 2 then falls behind real time).
  Run the default and scalar trees' timing tests on an otherwise idle
  machine, and rerun a timing failure that came under outside load
  alone before calling it a regression.
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

## Adding a radio plugin

1. Copy the example: `plugins/example` (C++, the tutorial
   [docs/RADIO_PLUGINS.md](docs/RADIO_PLUGINS.md)) or `plugins/pei` (plain
   C). A TETRA SDR plugin is the SoapySDR layer plus an `Options` block, as
   `plugins/hackrf/hackrf_plugin.cpp` (driver filter, device profile with
   presets, gain stages, rates).
2. `plugins/<name>/CMakeLists.txt`: `yantra_add_radio_plugin(<name>
   SOURCES ... [LIBS ...])`, which builds `lib/yantra/plugins/radios/<name>.so`
   with the export map and adds the ctests `completeness_<name>` and
   `conformance_<name>`; add the folder and the plugin's list entry in
   `plugins/CMakeLists.txt` and `cmake/YantraRadioParts.cmake`, the
   plugin to the list of `completeness_plugins` (`tests/radio/completeness/CMakeLists.txt`)
   and the installed file
   to `tests/install/check_install.py` and
   `tests/radio/install/check_install.py`. Give the folder a short
   README.md.
3. Tests in `tests/radio/<name>/` against a fake device (`yantra_doctest_tests`
   for per-case ctests); real-time checks in `TEST_SUITE("timing")`.
4. A page `docs/plugins/<name>.md` with the generated block (add the
   plugin to `tools/ci/plugin_docs.py`, then `ninja update-plugin-docs`),
   its schemas in docs/TETRA_RECORDS.md (or its own schema page), a row
   in [docs/DEVICES.md](docs/DEVICES.md), and a CHANGELOG entry.

## Adding a SoapySDR module or a bundled library

1. Only libraries whose licence is GPL-3.0 compatible, from RAD-04's
   list (ask before adding another). Pin the release archive: add a row
   to `tools/deps/manifest.tsv` (name, version, SPDX, folder, URL,
   SHA-256), vendor the files the build needs unchanged under
   `third_party/<folder>/` with the licence file, and run
   `tools/deps/verify_sources.py`.
2. Build it statically with hidden symbols in `tools/deps/sdr.cmake` (or
   `deps.cmake`); a SoapySDR module joins `yantra::soapy_modules` and the
   module table of `cmake/YantraSoapy.cmake`.
3. Add its row to THIRD_PARTY.md (version, licence, source URL, what is
   built in, local replacements) and a REUSE.toml annotation; the ctest
   `licence_bundle` checks that manifest, folder, licence text and
   THIRD_PARTY.md agree, and the licence bundle picks it up.
4. A device needs udev rules (`udev/60-yantra-sdr.rules`) and a
   docs/DEVICES.md row; the fake module (`tests/radio/fake_soapy`) can imitate its profile.

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
- Radio plugins (`plugins/hackrf`, `soapy`, `pei`, `simulator`) process
  their own IQ samples and give Yantra measurement records only; no IQ,
  bursts or PDUs cross the radio API (RAD-20..26). The application has no DSP, decoder,
  SoapySDR or IQ code; it depends on the radio API, never the reverse
  (PLG-30).
- Code is written for Yantra from public standards. Do not copy code from
  other receivers, including OnAir (OA-04; its `dect2cli` may only be run
  as an external test oracle), or use material from closed products.
  Proprietary survey software, its files, databases and dongles must not
  be used, read or interfaced with.
- Do not commit recordings of real networks unless you are allowed to
  publish them and they contain broadcast information only.
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
- The plugin API (`include/yantra/plugin.h`) is plain C. So is the radio
  API (`radio-api/include/yantra/radio.h`): versioned structs that start
  with `struct_size`, `uint32_t` constants instead of enums, no
  exceptions or panics across the boundary (RAD-10).
- Keep the code portable to Windows (MSYS2 UCRT64): platform code behind
  small interfaces.
- Python tools use the standard library; shell scripts use `set -eu`
  (bash: `set -euo pipefail`) and pass `tools/ci/lint.sh`.
