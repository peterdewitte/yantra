<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Contributing to yantra-radios

Thank you for helping. Please read this page before opening a pull
request. Build steps follow in BUILD.md once the CMake project exists.

## Licence rules (RAD-05, Yantra NFR-01)

- yantra-radios is GPL-3.0-or-later. By contributing you agree that your
  change is published under that licence.
<!-- REUSE-IgnoreStart -->
- Every file starts with an SPDX header:
  `SPDX-FileCopyrightText: <year> <name>` and
  `SPDX-License-Identifier: GPL-3.0-or-later` (in a comment of the file's
  language; Markdown uses an HTML comment at the top). Files that cannot
  carry a header (binary fixtures, golden files, JSON data, vendored code)
  get an annotation in `REUSE.toml` instead.
<!-- REUSE-IgnoreEnd -->
- `reuse lint` must report the project as compliant before every commit.
  A new licence needs its text in `LICENSES/` (`reuse download <SPDX-id>`).
- Only the libraries of RAD-04 are used. A new library or tool is added
  to [THIRD_PARTY.md](THIRD_PARTY.md) with its version, licence, source
  URL, what is built in and any local patch, after checking its own
  licence file. Bundled libraries are pinned under `third_party/`
  (submodule, or release archive with its SHA-256) and built statically
  by the scripts in `tools/deps/` (RAD-136).
- osmo-tetra is AGPL-3.0: the plugin descriptor states the AGPL parts
  (RAD-12). Its `crypto/` directory is never built or used.

## Sources that must not be used

- No code from OnAir may be copied, not even under the shared GPL
  licence. OnAir's `dect2cli` may only be run as an external test oracle.
- Proprietary survey software, its files, databases and dongles must not
  be used, read or interfaced with.
- Do not commit recordings of real networks unless you are allowed to
  publish them and they contain broadcast information only.

## Scope and radio boundary

- The plugins decode only broadcast and system information, levels and
  quality. Changes that decode or store speech, short data (SDS) content
  or subscriber identities, or that decrypt anything, will not be
  accepted.
- A radio plugin processes its own IQ samples and gives Yantra
  measurement records only; no IQ, bursts or PDUs cross the radio API
  (RAD-20..26). This repository never depends on Yantra.

## Commits and pull requests (Yantra TST-60..63)

- Small, logical commits, one coherent step each (for example
  "receiver: channelizer", "tests: channelizer"). Every commit builds
  without warnings and passes the full `ctest` suite.
- The message says what changed and why: a short title, a blank line,
  then the reason. A change to decoded output says why the output
  changes; a changed golden file names the reason.
- Code, commits and docs refer to requirements by their ID (for example
  "RAD-40") instead of copying the requirement text.
- Commit dates are the real dates on which the work was done. History is
  never backdated or rewritten. Imported work keeps its original dates
  and says so in the message.
- AI-assisted commits carry a `Co-Authored-By:` trailer naming the
  assistant (model and vendor) as the last line of the message.
- Every pull request says how it was tested: on which OS, with which
  radio (or "simulator/IQ file only"), and which tests were added.
- No personal data (names or e-mail addresses of people other than the
  authors, positions of real sites) in code, test data, docs or any
  request to an external service.

## Building and testing

- Local builds and tests may use all cores (`ninja`, `ctest -j"$(nproc)"`,
  `CARGO_BUILD_JOBS="$(nproc)"`), as the CI runners do.
- Tests run under `ctest` with the labels `fast`, `slow` and `timing`
  (RAD-03). They wait on observable conditions, never on a fixed sleep.
- The long doctest binaries (robustness, simulator, conformance kit,
  engine, decoder, decode-iq, generator) are one ctest per test case,
  named `<binary or group>/<case>` (cmake/YantraTests.cmake,
  `yantra_doctest_tests()`), so `ctest -j` runs their cases in parallel;
  the cases are listed after every link. `tests/ctest_costs.txt` gives
  them a COST so the longest start first in a fresh tree; refresh it
  with `tools/ci/test_costs.py build` after a full run.
- Default run, the check before every commit (opt-in suites left out):

      ctest --test-dir build -j"$(nproc)"

- Opt-in suites, label `opt_in`: the long robustness suite
  (`test_robustness_slow/*`) and the full sensitivity tier (`sens_full`).
  They exist only when `YANTRA_TESTS_OPT_IN` is set when ctest runs; the
  ci-full and sens workflows set it. Run them after a receiver, decoder or
  engine change and before a release:

      YANTRA_TESTS_OPT_IN=1 ctest --test-dir build -L opt_in -j"$(nproc)"
      YANTRA_TESTS_OPT_IN=1 ctest --test-dir build -j"$(nproc)"   # everything

- Label `timing`: real-time and pacing checks (`test_simulator_timing/*`,
  `conformance_simulator` with the kit's one-record-per-second rule,
  `perf_engine`, `bench_limits`). The sanitizer trees run 4-13x slower
  than real time, so their test presets and the ci-full sanitizer jobs
  leave the label out; the default and scalar trees run it. The hang
  guards (ctest TIMEOUT of the per-case tests, the simulator tests' stream
  limits) are multiplied by `YANTRA_TEST_TIME_SCALE` (CMake cache, 5 in
  the ASan/UBSan tree, 10 in the TSan tree, 1 otherwise); the checks stay the same:

      ctest --preset asan-ubsan -j"$(nproc)"   # = ctest --test-dir build-asan -LE timing
      ctest --preset tsan -j"$(nproc)"
- Never delete, skip or weaken a test, or loosen a tolerance, to make it
  pass. If a test is wrong, fix it and say why in the commit message.
  Golden files change only on purpose, with the reason in the commit.

## Code style

- C++20, 4-space indent, lines up to about 120 characters; `snake_case`
  functions and variables, `PascalCase` types, `kName` constants, a
  trailing `_` on private members.
- Headers start with a comment saying what the file is for and which
  requirement IDs it implements; `#pragma once`.
- The radio API (`include/yantra/radio.h`) is plain C: versioned structs
  that start with `struct_size`, `uint32_t` constants instead of enums, no
  exceptions or panics across the boundary (RAD-10).
- Keep the code portable to Windows (MSYS2 UCRT64): no POSIX-only calls
  outside platform files.
- Python tools use the standard library (the CI lint also uses PyYAML);
  bash scripts use `set -euo pipefail`, POSIX sh scripts `set -eu`, and
  all pass `tools/ci/lint.sh`.

## Adding a plugin

1. Copy the example: `plugins/example` (C++, the tutorial
   [docs/RADIO_PLUGINS.md](docs/RADIO_PLUGINS.md)) or `plugins/pei` (plain
   C). A TETRA SDR plugin is the SoapySDR layer plus an `Options` block, as
   `plugins/hackrf/hackrf_plugin.cpp` (driver filter, device profile with
   presets, gain stages, rates).
2. `plugins/<name>/CMakeLists.txt`: `yantra_add_radio_plugin(<name>
   SOURCES ... [LIBS ...])`, which builds `bin/plugins/<name>.so` with the
   export map and adds the ctests `completeness_<name>` and
   `conformance_<name>`; add the folder in `plugins/CMakeLists.txt`, the
   plugin to the list of `completeness_plugins` (`tests/completeness/CMakeLists.txt`)
   and the installed file
   to `tests/install/check_install.py`.
3. Tests in `tests/<name>/` against a fake device (`yantra_doctest_tests`
   for per-case ctests); real-time checks in `TEST_SUITE("timing")`.
4. A page `docs/plugins/<name>.md` with the generated block (add the
   plugin to `tools/ci/plugin_docs.py`, then `ninja update-plugin-docs`),
   its schemas in docs/TETRA_RECORDS.md (or its own schema page), a row
   in DEVICES.md, and a CHANGELOG entry.

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
4. A device needs udev rules (`udev/60-yantra-sdr.rules`) and a DEVICES.md
   row; the fake module (`tests/fake_soapy`) can imitate its profile.

## Releases

Version in `CMakeLists.txt` (`project(... VERSION x.y.z)`), the CHANGELOG
section `## [x.y.z] - <date>` with real dates, the full test set of
"Building and testing" plus `reuse lint` and `tools/deps/verify_sources.py`,
then `ninja -C build release` and an annotated tag `vx.y.z`
([BUILD.md](BUILD.md#release)).
