<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Changelog

Releases of yantra-radios (RAD-98) and the radio API changelog (RAD-11).
Release notes are this file's section of the tag
(`tools/ci/release_notes.py CHANGELOG.md v0.1.0`).

## How the radio API is versioned

The radio API (`include/yantra/radio.h`) has its own version
`MAJOR.MINOR` (`YR_API_VERSION_MAJOR`, `YR_API_VERSION_MINOR`), separate
from the release version and from Yantra's plugin API. Yantra loads a radio
plugin only when the plugin's major version equals its own and the
plugin's minor version is not newer than its own; otherwise it refuses
the plugin with a message that names both versions (RAD-11).

Every change to the radio API is listed below under "Radio API" and
marked:

- **compatible**: an addition that older plugins and hosts survive (a new
  member at the end of a versioned struct, a new optional callback, a new
  constant, schema field or event). Raises the minor version.
- **breaking**: anything else (a removed or reordered member, a changed
  meaning, type or calling rule). Raises the major version; plugins must
  be rebuilt.

Record file (RAD-65) and scene JSON (RAD-36) versions are listed here as
well; they change independently of the radio API version.

## [Unreleased]

### Radio API

- **Radio API 1.1** (compatible, RAD-21, RAD-30): a device can report
  calibrated dBm without being SELF_CALIBRATED. New device flag
  `YR_DEVICE_LEVEL_DBM` (in enumerate and device_info: the device's
  current level unit) and record flag `YR_RECORD_LEVEL_DBM` (on every
  record of such a device; a unit change takes effect with the next
  record); `YR_DEVICE_ALL_MASK`, the 1.0 masks `YR_RECORD_ALL_MASK_1_0` and
  `YR_DEVICE_ALL_MASK_1_0`, and `yr_record_level_is_dbm()`. A
  SELF_CALIBRATED plugin of 1.1 sets both flags on every device and record
  (a 1.0 one is dBm by its capability). Hosts of 1.0 refuse 1.1 plugins
  (newer minor version); 1.1 hosts load 1.0 plugins.
- The record validator (`radio.hpp`) checks the flags against the
  plugin's API version and LEVEL_DBM on SELF_CALIBRATED records; the
  conformance kit checks the device flags of the plugin's version, LEVEL_DBM
  on SELF_CALIBRATED devices, and that every record's LEVEL_DBM matches the
  device entry (fault `level_dbm` of the fake plugin).
- Record files stay version 1: new flag name `level_dbm` for records and
  devices (readers ignore flag names they do not know); `plugin.api_version`
  is `"1.1"`. The terminal plugin, `decode-iq --calibrated` and
  `yantra-gen`'s ground truth records carry `level_dbm`.
- Documentation of `get_floor_plan` names every bundled example id
  (`fraunces-N`, `mak-beer-house-N`, `mak-haus-claeson-N`,
  `mak-villa-bunzl-N`) and the scene's plan cell ids (RAD-18).

### Added

- Cross SDK (RAD-139): `tools/sdk/yantra_sdk.py make` builds a
  git-ignored SDK folder (`sdk/` or `$YANTRA_SDK_DIR`) from pinned,
  signature-checked downloads: LLVM 23.1.5 (clang, lld, llvm-rc,
  llvm-objdump, compiler-rt), the Windows x64 UCRT sysroot from MSYS2
  UCRT64 packages (mingw-w64 14.0.0, GCC 16.2.0 runtime) and Rust 1.93.1
  for x86_64-unknown-linux-gnu and x86_64-pc-windows-gnu; `check`
  compares every lock file with what is unpacked. CMake toolchain files
  for Linux (clang) and Windows x64 (`x86_64-w64-windows-gnu`, static GCC
  runtime, winpthreads in every link, llvm-rc); `tools/sdk/wsl_run.sh`
  runs a Windows program from WSL2. ctests `sdk_check` and
  `sdk_windows_smoke` (a C++20 + Rust program cross-compiled, its DLL
  imports checked, run on Windows through WSL interop).
- Windows x64 (RAD-131, RAD-132, RAD-133, RAD-136): the whole tree is
  cross-compiled with the cross SDK (presets `windows`, `windows-scalar`,
  `ci-windows`; tree `build-win`): the plugins as `.dll` exporting only
  `yantra_radio_entry`, `yantra-radio-host.exe`, `yantra-gen.exe`,
  `yantra-hackrf.exe`, every test, the Rust code for
  `x86_64-pc-windows-gnu`, the bundled libraries static from the same
  sources (also as a prefix: `build_deps.sh` with the Windows toolchain
  file). ctest runs the `.exe` on Windows through WSL interop
  (`tools/sdk/wsl_run.sh --in-place`); Linux-only tests are reported as
  skipped with their reason (`yantra_skip_test()`). The completeness
  check reads llvm-objdump's PE output and checks programs too
  (`--programs`; tests `completeness_programs`,
  `completeness_programs_shipped`); `msvcrt.dll` and the GCC runtime
  DLLs are refused. Windows golden files for the generator tests
  (MinGW-w64's math functions round some results differently). Release
  package `yantra-radios-<version>-windows-x86_64.zip`
  (`package_release.sh`, `make_release.sh`). CI: `windows-cross` builds
  the tree on Linux, `windows-x86-64-tests` runs its tests natively on a
  Windows runner (`tools/ci/run_windows_tests.py`); `windows-check.yml`
  builds natively with MSYS2 UCRT64's gcc; the release job cross-compiles.
  The terminal plugin's serial ports on Windows are not done yet.

### Changed

- clang of the cross SDK is the default compiler (RAD-03, RAD-139): the
  presets `default`, `scalar`, `ci`, `ci-scalar`, `asan-ubsan`, `tsan` and
  `fuzz` use `tools/sdk/toolchain-x86_64-linux-gnu-clang.cmake` (with the
  SDK's lld, Rust and llvm-symbolizer). The system gcc keeps a second tree:
  presets `gcc`, `gcc-scalar` (`build-gcc`, `build-gcc-scalar`) and
  `ci-gcc`. CI builds and tests both on Linux (`linux-x86-64` with the SDK
  cached per lock-file version, `linux-x86-64-gcc`); `ci-full.yml` runs
  the sanitizer, scalar and fuzz trees with clang; the workflow YAML is
  checked with PyYAML. `tools/ci/run_linux.sh` takes
  `YANTRA_CI_COMPILER=clang|gcc`; `tools/deps/build_deps.sh` rebuilds when
  the compiler changes. No behaviour change.
- The Windows tree's ctest runs natively on Windows (RAD-131, RAD-133;
  BUILD.md "Windows x64"): the presets have no emulator; every test
  command is a `.exe`, `cmake` or `python3`, so the tree runs in a Windows
  test VM that mirrors it to the same paths. The doctest cases of a
  cross-compiled program are listed when ctest runs on Windows
  (`doctest_discover.py --at-test-time`; the build writes one
  `<group>/all` test). The command-line tests that used `sh -c` and
  `with_remote_server.sh` use Python drivers on every platform
  (`tests/test_command.py`, `decode_iq_sigmf.py`, `fixtures_selftest.py`,
  `install_check.py`); the conformance tests unplug and replug with
  `cmake -E touch/rm`. Build-machine checks (SDK, lint, cargo, Doxygen,
  dependency and release scripts) carry the label `build_host`: skipped
  on Windows, run with `ctest -L build_host` on Linux;
  `fuzz_proto_rust` is skipped in the Windows tree. WSL2 interop stays
  optional (`-DYANTRA_WINDOWS_TEST_RUNNER=wsl`; ignored with a warning
  without WSL).
- The completeness check reads the PE import and export tables itself
  (`--objdump` stays optional) and takes the build machine's folders
  with `--build-folder` when it runs elsewhere (RAD-133).
- The cross SDK and its cache may be links to folders outside the tree
  (git-ignored either way; BUILD.md); the SDK's folder is mapped to
  `sdk/` in debug information (RAD-139).

### Fixed

- Builds without warnings under clang: a C plugin no longer gets
  `-static-libstdc++`; a test expression clang read as a declaration.
- Windows: the GSMTAP output starts Winsock before resolving its address;
  `yantra-gen` finds its scenarios next to the `.exe`; the conformance
  kit's cleanup check counts threads on Windows too; plugins load with
  `LoadLibraryExW` from their own folder first.
- Tests: paths written into JSON text use forward slashes; the generator
  tests link the bundled nlohmann-json (they used the system's headers).
- Windows: `yantra-radio-host conformance --unplug/--replug` commands
  with a quoted program path work (`cmd.exe` quoting).
- Windows: `yantra-gen` and the test programs turn the UCRT's
  CPU-dependent FMA3 math paths off (`_set_FMA3_enable(0)`), so the
  generator's output and its golden digests are the same on every
  Windows machine; the Windows golden files now come from the test VM
  (the `cells_cs16_impaired` digest equals the Linux one).
- Tests: `yantra_gen_stdout_endless` drops the generator's standard error
  after the reader closes the pipe (on Windows it reports the closed pipe,
  where Linux ends it silently by SIGPIPE).

### Removed

- The real-signal fixture framework (`tests/real_fixtures`,
  `tests/fixtures/real`, ctests `real_fixtures` and
  `real_fixtures_selftest`): its requirement was removed from the spec
  (no real recordings are available); the real-world checks stay in
  REAL_WORLD_CHECKLIST.md and DEVICES.md. Likewise the independent decoder
  cross-check and the terminal's call control are no longer requirements:
  the terminal plugin stays receive-only.

## [0.2.7] - 2026-11-09

A patch release (tag v0.2.7, RAD-98) from the release check of Yantra's
phase 12: the release files name no folder of the machine that built
them, and the documentation is current again. No API, format or
behaviour change: radio API 1.0, record files 1, scene JSON 1.1 and
scenario files 1 as in 0.2.0; plugin versions as in 0.2.6.

### Radio API

- **Radio API 1.0**: no change.

### Fixed

- Plugins and tools (RAD-133, RAD-137): the files of the release held
  the builder's home folder, in debug information, in libosmocore's
  `__FILE__` and in the Rust crate. Source paths are now relative
  (`-ffile-prefix-map`, `--remap-path-prefix`). The Rust library has its
  own cargo target folder, `<build>/cargo-lib`.

### Documentation

- README: screenshots of the radio windows Yantra generates (RAD-135).
- DEVICES.md: the test rows measured again on these plugins, with their
  versions; the hardware rows stay open (RAD-95).
- ARCHITECTURE.md: a read reaches the engine in pieces (0.2.2).
  SCENARIO_FORMAT.md: a host's scene cell replaces a scenario cell on its
  carrier (0.2.3). A file path and an example version corrected.

### Tests

- New: the completeness check has the finding `build_paths`, so a plugin
  naming the source tree or the builder's home folder fails it (ctest
  `completeness_build_paths` proves the finding is raised);
  `package_release.sh` looks for the same folders in every packaged file.
  The asan-ubsan tree is left out of both: gcc's AddressSanitizer keeps
  each source file's full path, and that tree is never shipped.

## [0.2.6] - 2026-11-06

A patch release (tag v0.2.6, RAD-98) that takes back the simulator
change of 0.2.5. No API or format change: radio API 1.0, record files 1,
scene JSON 1.1 and scenario files 1 as in 0.2.0.

### Radio API

- **Radio API 1.0**: no change.

### Fixed

- Simulator plugin 0.2.4 (RAD-61, RAD-19, RAD-27): 0.2.5 stamped the
  records of a slowly simulated interval near the interval's end, but
  their levels are calculated for the position read at its start
  (sim_position): a record claimed a later time than its position's, and
  Yantra's walk test saw the simulated position lag the walker by more
  than the record's age allows. The time base moves with the lag only at
  the interval start again (as in 0.2.4): on a computer slower than real
  time the records arrive late, by up to an interval's processing time,
  and the host flags them late.

### Tests

- The 0.2.5 test (records reach the host soon after their time) is
  replaced by "simulator: slower than real time, a record's time is when
  its position was taken": at ten times real time the cursor jumps
  between two points after every second record, and every record's time
  lies within the cursor's stay at its own position. It fails with the
  0.2.5 simulator.

## [0.2.5] - 2026-11-06

A patch release (tag v0.2.5, RAD-98): the simulator's records keep the
host's time when the computer simulates slower than real time. No API
or format change: radio API 1.0, record files 1, scene JSON 1.1 and
scenario files 1 as in 0.2.0.

### Radio API

- **Radio API 1.0**: no change.

### Fixed

- Simulator plugin 0.2.3 (RAD-61, RAD-27): a computer slower than real
  time (sanitizer builds, a loaded machine, a high playback speed) moved
  the simulation's time base only when a record interval started, so
  every record carried a time from the start of its interval and reached
  the host the whole interval's processing time later. Yantra flagged
  such records late and stale, and once an interval took about 5 s to
  simulate, its measurement cycles lost the serving carrier between two
  records (its 10 s drop limit counts from the record time) although
  the stream still delivered. The lag now moves the time base before
  every block; the interval being simulated moves with it, so its
  records keep their position and ground truth (RAD-19). In real time
  nothing changes.

### Tests

- New: "simulator: slower than real time, the records keep the host's
  time and their position" runs ten times real time at 2 Msps: a record
  reaches the host at most a quarter of an interval's processing time
  (plus 0.2 s) after its time, in time order and with its sim_position.
  It fails before the fix (delays of the whole interval).

## [0.2.4] - 2026-11-05

A patch release (tag v0.2.4, RAD-98): the TETRA terminal plugin no
longer takes the late reply of a stopped session as the answer to the
next session's first command, and the SDR tests that failed only under
load have their cause fixed. No API or format change: radio API 1.0,
record files 1, scene JSON 1.1 and scenario files 1 as in 0.2.0.

### Radio API

- **Radio API 1.0**: no change.

### Fixed

- TETRA terminal plugin pei 0.1.1 (RAD-80, RAD-46, TRM-01): a stream
  stopped while a command was out left the terminal's reply on the line.
  When it came after the next session had flushed the port and sent its
  first command, it was taken as that command's answer: a self-test
  reported "The terminal answers AT commands" as passed for a terminal
  that did not answer. A session stopped with a command out now notes
  when that reply is due, and the next session (stream or self-test)
  reads it away before its first command.

### Tests

- The soapysdr and hackrf cases stream from the fake SoapySDR module
  unpaced, as fast as the CPU allows; their limit was wall-clock time
  and a loaded machine (a full parallel ASan run) stopped them before
  their records came. They now stop after a number of seconds of signal,
  read from the record times, with a stall guard for a stream that
  hangs (`tests/stream_guard.hpp`; RAD-90, RAD-92).
- yantra-hackrf "throughput per sample rate", subcase "an overflow
  fails": the fake now injects its overflow at the second read instead of
  after 0.1 s of signal, which the unpaced stream under ASan did not
  always reach within the command's wall-clock duration under load
  (RAD-66, RAD-24).
- New: the stream guard stops after its signal and reports a stall; a
  terminal's late reply to a stopped stream does not pass the next
  self-test's at_reply item. The latter fails before the fix.

## [0.2.3] - 2026-11-05

A patch release (tag v0.2.3, RAD-98): an own plan's cell on a carrier
the simulator's default scenario already uses is simulated from the
plan's scene. No API or format change: radio API 1.0, record files 1,
scene JSON 1.1 and scenario files 1 as in 0.2.0.

### Radio API

- **Radio API 1.0**: no change.

### Fixed

- Simulator plugin 0.2.2 and yantra-gen (RAD-61, RAD-73): a cell of the
  host's scene on a carrier of the default scenario (390.0 or 390.1 MHz)
  was transmitted next to the scenario's cell on the same carrier; the
  scenario's fixed-level cell (D0, -60 dBm) won the decoder and the
  records carried its truth (cell_id D0, walls 0) instead of the plan's
  multi-wall level. `gen::add_scene_cells()` checked ids only. A scene
  cell on the carrier of a scenario cell the scene does not name now
  replaces that cell: it keeps the old cell's place and neighbour cell
  identifier, the other cells announce it instead, and the old cell's
  events and scenario-floor antennas go. Carriers 6.25 or 12.5 kHz apart
  stay different carriers (RADIO_PLUGINS.md).

### Tests

- New: add_scene_cells replaces a scenario cell on the same carrier
  (neighbours, cell number, events, floor antennas; a cell 6.25 kHz away
  is kept); the simulator with an own plan whose cell is on D0's carrier
  gives that cell's identity, walls 0 and 1 and the multi-wall level
  within 0.5 dB in front of and behind a wall. Both fail before the fix.

## [0.2.2] - 2026-11-05

A patch release (tag v0.2.2, RAD-98): the SDR plugins return from
stop_stream within the RAD-15 limit also on a slow or loaded machine,
and tests that failed only under load have their causes fixed. No API
or format change: radio API 1.0, record files 1, scene JSON 1.1 and
scenario files 1 as in 0.2.0.

### Radio API

- **Radio API 1.0**: no change.

### Fixed

- SDR plugins soapysdr 0.1.2 and hackrf 0.1.2 (shared SoapySDR layer;
  RAD-15, RAD-33): the stream thread handed a whole read (up to the
  module's MTU, 131072 samples) to the receiver in one call, and
  stop_stream waits for that thread; on a loaded machine that took
  seconds. The engine now takes a read in pieces of 16384 samples and
  the thread stops between two pieces.
- Fake SoapySDR module (tests, RAD-92): an unpaced read generated its
  whole request whatever its timeout (up to 15 s under ASan with
  parallel tests), so a stop_stream waiting for it missed the RAD-15
  limit (Yantra's unplug/replug test: 21 of 24 parallel ASan runs). A
  read now returns at its timeout with the samples generated so far, as
  a real module does.

### Tests

- The HackRF records case checks the content of the serving cell's
  neighbour list instead of waiting more than 10 s of signal for its
  repeat (it reached its 600 s hang guard under ASan and load).
- New: the fake module's reads with timeout 0 return one piece each and
  together the same samples as one long read.
- perf_engine is RUN_SERIAL like the other time limits; CONTRIBUTING.md
  says why timing tests need an otherwise idle machine.

## [0.2.1] - 2026-11-04

A patch release (tag v0.2.1, RAD-98): the simulator measures every cell
of its scenario, and a given serving carrier gets one decoder. No API or
format change: radio API 1.0, record files 1, scene JSON 1.1 and
scenario files 1 as in 0.2.0.

### Radio API

- **Radio API 1.0**: no change.

### Fixed

- Simulator plugin 0.2.1 (RAD-61, RAD-102, RAD-112): the carrier search
  held only the middle 80 % of the capture, less half a channel at each
  edge, while the plugin offered plans the middle 80 %; cells near the
  edge of a scenario's capture were neither found nor decoded when pinned
  (failed_site gave records of N2 only, at the capture centre, also with
  N1 asked for as the serving carrier). The search now uses 90 % of the
  rate (the generated I/Q has no analogue anti-alias roll-off; as
  `decode-iq`), and `plan_ranges` and the plan check offer exactly the
  span the search holds (`usable_half_width()` in carrier_search.hpp).
- TETRA receiver, all TETRA plugins (simulator, soapysdr 0.1.1, hackrf
  0.1.1; RAD-32): a SERVING plan with a given carrier pinned that carrier
  twice (as a plan carrier and as the serving carrier), so two decoders
  followed it and each interval gave a second record of it with role
  "other". A frequency pinned twice now gets one decoder.
- `yantra-radio-host` 0.2.1 (same receiver; `decode-iq` was not affected:
  it pins a given serving carrier once).

### Tests

- Every bundled scenario with two or more decodable cells (all but
  weak_signal, whose second cell is below the noise by design): each cell
  in the capture measured within 0.5 dB of the ground truth, the
  strongest serving; failed_site with N1 or N2 given as serving; one
  decoder per carrier in the search and with the fake SoapySDR module.

## [0.2.0] - 2026-11-04

Scene format 1.1 for Yantra's building model (tag v0.2.0, RAD-98). A new
minor release because a format changes (scene 1.1); the radio API stays
1.0, so plugins and hosts of 0.1.0 load unchanged.

### Radio API

- **Radio API 1.0**: no change. `include/yantra/radio.h` only gains
  documentation of scene 1.1 next to `YR_SCENE_FORMAT`
  (`YR_SCENE_VERSION` stays 1, the major version).

### Formats

- Scene JSON `yantra-radio-scene` **1.1** (compatible, RAD-36,
  docs/SCENE_FORMAT.md): `minor_version`; floor `storeys` (level and slab
  sum per other floor); wall `polyline` (segments of one wall polyline);
  wall `bands` (attenuation per frequency). 1.0 scenes are read as before
  and give the same levels; 1.0 readers read 1.1 scenes and ignore the new
  members. Readers read any minor of version 1 and refuse version 2. Not
  in 1.1: "EIRP unknown" and outdoor donor directions (the scenario's
  antenna EIRPs and outdoor site cover them, RAD-61, RAD-74).

### Changed

- Multi-wall model (RAD-73, docs/CHANNEL_MODEL.md): per-band wall
  attenuation at the cell's carrier; per-floor levels and slabs from the
  scene's storeys.
- Simulator plugin 0.2.0 and `yantra-radio-host` 0.2.0 read scene 1.1
  (conformance kit run on the simulator with a 1.1 scene).

### Fixed

- Multi-wall model: a path exactly through a vertex of a wall polyline at
  an angle counted the wall twice; with polyline ids (scene 1.1) it counts
  once per crossing point, as Yantra's building model does. Scenes without
  polyline ids keep the 1.0 rule, so all golden results are unchanged.
- Tests: the fake SoapySDR control files are written atomically (a rare
  overflow-count race under `ctest -j`).

## [0.1.0] - 2026-11-04

The first version, tag v0.1.0 (RAD-98). Linux x86-64 `.so` plugins
`hackrf`, `soapysdr`, `simulator` and `pei`, the tools `yantra-radio-host`,
`yantra-gen` and `yantra-hackrf`, the licence bundle and the complete
corresponding source. Not yet in this release: the Windows `.dll` build
(written, not run), radios measured on real hardware (DEVICES.md), real
recorded signal fixtures (RAD-121).

### Radio API

- **Radio API 1.0** (new): the first version, defined in
  `include/yantra/radio.h` (RAD-10..49):
  - entry symbol `yantra_radio_entry` and the descriptor with id, version,
    licence, capabilities, technology id, record schemas and translations
    (RAD-12, RAD-13, RAD-48);
  - versioned structs with `struct_size` (RAD-10); status codes and
    `last_error` texts (RAD-16);
  - the host callbacks: log, clocks, record sink `push_record`,
    `stream_event`, `devices_changed`, `settings_changed`, and for SCENE
    plugins the context callbacks `get_floor_plan`, `get_cursor`,
    `get_walk_position` and `get_scene` with `on_context_event`
    (RAD-14, RAD-18, RAD-19);
  - record schemas of kind MEASUREMENT, SCAN and NEIGHBOUR with eight core
    fields at fixed indexes and optional field groups (RAD-20..27);
  - the common stream control: enumerate, open, close, device info, state,
    plan ranges, plans, start, stop, stream statistics (RAD-30..35);
  - device control described as data: settings, actions, presets,
    sensors, self-test (RAD-40..49).

### Formats

- Record files `yantra-radio-records` version 1 (RAD-65,
  docs/RECORD_FILES.md).
- Scene JSON `yantra-radio-scene` version 1 (RAD-36,
  docs/SCENE_FORMAT.md).
  Compatible optional members for the multi-wall model (RAD-73): floor
  `slab_attenuation_db` and `storey_height_m`, wall class
  `reflection_loss_db`, antenna `floor_offset`.
- Scenario files `yantra-radios-scenario` version 1 (RAD-74,
  docs/SCENARIO_FORMAT.md), in data/scenarios.

### Added

- Radio test host `yantra-radio-host` with emulated host callbacks and the
  conformance kit (RAD-91); every plugin gets a conformance test
  (`yantra_add_radio_plugin`).
- Example radio plugin `beacon` (RAD-64) and the plugin tutorial
  docs/RADIO_PLUGINS.md.
- TETRA test-signal generator library `yantra::gen` (gen/, RAD-70), first
  part: downlink channel coding (EN 300 392-2 clause 8), the broadcast
  PDUs (SYNC, SYSINFO, ACCESS-ASSIGN, MAC-RESOURCE, D-NWRK-BROADCAST with
  neighbours) and the slot schedule of a cell's main carrier, with golden
  vectors and osmo-tetra cross-checks (RAD-93). Test binaries take
  `--update` to rewrite golden files on purpose.
- Generator second part (RAD-70): synchronization and normal continuous
  downlink bursts (clause 9), pi/4-DQPSK modulator with root-raised-cosine
  shaping at any sample rate, `CellTransmitter` (one cell's continuous
  carrier with level, frequency offset and timing offset) and
  `MultiCarrierTransmitter` (several carriers in one span), with burst and
  IQ golden vectors and an osmo-tetra burst-sync round trip (RAD-93).
- Generator third part: the channel model (RAD-72: path loss, correlated
  shadowing, Rayleigh/Rician fading, TU50/HT200, Doppler, CFO and clock
  errors, AWGN from a noise figure, IQ imbalance, DC, clipping,
  quantisation, sample drops), the indoor multi-wall model on the scene
  (RAD-73, docs/CHANNEL_MODEL.md), scenario files with the DEMO-09
  scenarios and one per bundled example plan, and the scenario engine
  with ground truth per time step (RAD-74); statistics, formula and
  seeded golden-digest tests (RAD-93).
- `yantra-gen` (RAD-71): scenario file, example id or command-line cells
  to cf32/cs16/cs8/cu8 samples with SigMF metadata (`yantra:simulated`,
  capture segments at sample drops, annotations per cell and event), a
  ground-truth CSV, a cell CSV and a ground-truth record file (RAD-65,
  schemas `tetra_cell` and `tetra_neighbour`); impairment options of
  RAD-72; streaming to stdout; deterministic for a seed
  (docs/YANTRA_GEN.md). Install rules for yantra-gen and data/scenarios.
  Tests: CLI and outputs against the scenario engine, SigMF validity,
  seeded golden digests, and a round trip of its IQ files through
  osmo-tetra's receive side (RAD-93).
- Receiver library `yantra::receiver` (receiver/, RAD-100): FFT on
  pocketfft, window functions and spectrum, Kaiser and root-raised-cosine
  filter design, NCO, polyphase resampler, channelizer (25 kHz channels
  from a wideband capture), pi/4-DQPSK demodulator with carrier-offset and
  symbol-timing recovery, soft bits, MER and lock; SIMD kernels with a
  plain C++ fallback (`YANTRA_SIMD=OFF`); the TETRA parameter set, the
  training sequences and a bit-error-tolerant burst synchroniser
  (DEC-01). Tests on generator IQ (exact bits, BER, offset and clock
  ranges) and a benchmark (docs/RECEIVER_DSP.md).
- Static dependency builds (RAD-132, RAD-136): talloc 2.4.3 and the
  libosmocore 1.11.0 subset osmo-tetra needs, vendored unchanged from
  their release archives (`tools/deps/manifest.tsv` with versions and
  SHA-256, `tools/deps/verify_sources.py`) and built with CMake
  (`tools/deps/deps.cmake`, or `tools/deps/build_deps.sh <prefix>` and
  `-DYANTRA_DEPS_PREFIX`); osmo-tetra's lower layers as the static
  `yantra::osmo_tetra` (`cmake/OsmoTetra.cmake`, never `crypto/`); the
  dependency manifest compiled in (`yantra::receiver::bundled_libraries()`).
  The osmo-tetra cross-check tests no longer need the system libosmocore.
- TETRA decoder `yantra::tetra_decoder` (receiver/, RAD-101): lower MAC
  on osmo-tetra (burst split, descrambling, de-interleaving, Viterbi, CRC,
  RM(30,14) AACH decoding with up to 3 corrections), MAC-SYNC/D-MLE-SYNC,
  MAC-SYSINFO/D-MLE-SYSINFO, ACCESS-ASSIGN and MAC PDU classification as
  typed structs (DEC-02, DEC-05), the serving-cell parameter set (DEC-03),
  encryption state and encrypted block counts (DEC-10), the H-M-F-S time
  base from SYNC with UTC stamps (DEC-11), and the per-carrier
  `CarrierDecoder` (IQ to slots, cell events and per-period counters).
  Instances share no state. Tests with osmo-tetra's test PDUs, generator
  signals with noise and offsets, parallel carriers and a bundled scenario
  (RAD-123, docs/TETRA_DECODER.md).
- Rust protocol crate `yantra-proto` (receiver/rust, RAD-101, RAD-03):
  MAC PDUs, LLC, D-MLE-SYSINFO, D-MLE-SYNC, D-NWRK-BROADCAST with every
  neighbour element and MM broadcast PDUs, behind a fixed-width C ABI
  (FFI width and layout lint, RAD-90; cargo-deny in the lint); golden
  digests run by ctest (RAD-120). The decoder uses it for fragment
  reassembly (DEC-04), neighbour lists and re-select parameters (DEC-02,
  DEC-03) and channel assignment counts per carrier (DEC-06).
- Decoder quality windows (RAD-113): message erasure rate from the AACH,
  CRC error rates per logical channel, BER of CRC-good blocks, MER/EVM,
  M2M4 SNR and frequency error per `CarrierDecoder` report.
- TETRA carrier search `yantra::tetra_engine` (RAD-102, RAD-110, RAD-111):
  25 kHz raster with the four carrier offsets in the TETRA bands, energy
  candidates, one decoder per carrier, loss and re-acquisition, pinned
  carriers, retune, and a parallel executor hook.
- Shared TETRA record schemas `tetra_cell`, `tetra_neighbour` and
  `tetra_scan` (`yantra/radios/tetra_schemas.hpp`, docs/TETRA_RECORDS.md)
  with `SchemaView` (a SchemaDef as a `yr_schema`). yantra-gen's ground
  truth uses them; its schema ids changed from `tetra.cell` and
  `tetra.neighbour` (not valid plugin schema ids) and it adds
  `freq_error_ppm`, `c1_db` and the dBm values of the SYSINFO codes.
- TETRA measurement engine `MeasurementEngine` (yantra::tetra_engine,
  RAD-102, RAD-110..114): wideband IQ of a capture to `tetra_cell`,
  `tetra_neighbour` and `tetra_scan` records and SYNC_FOUND/SYNC_LOST,
  SERVING_CHANGED and SCAN_DONE events; per carrier the 25 kHz channel
  level (dBFS, or dBm with a calibration offset), the quality window, C1
  and its inputs, broadcast parameters, frame number and allocation
  counts; serving choice, neighbour roles from the serving cell's list,
  record time from the caller's time base, overload and non-finite
  samples, an optional worker pool (docs/MEASUREMENT.md).
- `yantra-radio-host decode-iq` (RAD-63): cf32, cs16, cs8 and cu8 raw
  files and SigMF recordings (capture segments as discontinuities,
  yantra-gen's calibration, clipping as OVERLOAD) through the measurement
  engine into a record file; `--plugin` is no longer needed for it.
  `capture-iq` stays a hook (needs the plugins' device layer). Tests
  compare decoded records of yantra-gen scenarios with the ground truth.
- `perf_engine` ctest (RAD-114, labels slow/perf/timing): four carriers
  in a 1 MS/s capture through the whole engine must take less than half
  of real time on one thread.
- GSMTAP output (RAD-115, DEC-12): `EngineConfig::gsmtap` and
  `decode-iq --gsmtap ADDR [--gsmtap-port PORT]` send the decoded
  broadcast blocks (BSCH, BNCH, AACH, broadcast D-NWRK-BROADCAST, scope
  RAD-17) as GSMTAP over UDP for Wireshark; off by default.
- Fuzz targets (RAD-96, docs/FUZZING.md) for TETRA PDUs, soft-bit bursts
  through the lower MAC and GSMTAP filter, IQ file content through the
  measurement engine, SigMF metadata, scenario, scene and record files:
  replayed with seeded mutations under ctest (label fuzz, seeds from the
  generator), libFuzzer builds with the `fuzz` preset and
  `tools/ci/fuzz.sh`; cargo-fuzz targets `tetra_pdu` and `tm_sdu` for the
  Rust crate (receiver/rust/fuzz).
- Benchmarks with CPU-calibrated limits (RAD-127, docs/BENCHMARKS.md):
  channelizer, demodulator, decoder and level meter; ctest bench_limits
  (labels slow, perf) fails on a regression beyond tests/bench/limits.csv.
- Real-signal fixture framework (RAD-121, BLOCKED: no recordings yet):
  tests/fixtures/real/<radio>/NAME.{sigmf-data,sigmf-meta,expected.json},
  checked by the ctest real_fixtures (yantra-check-fixtures through
  decode-iq); real_fixtures_selftest checks it on a yantra-gen recording.
- Real-world robustness checklist (RAD-124, docs/ROBUSTNESS.md) with the
  end-to-end tests of tests/robustness (radio, air, transmitter and
  program impairments through the measurement engine) and the level
  linearity tests (RAD-125: +/-0.5 dB from -5 to -100 dBFS, 1 Hz records
  without gaps over long runs).
- Level meter (RAD-112): RRC filter matched to the TETRA pulse on the
  carrier centred by the demodulator's estimate, instead of a 14 kHz
  Kaiser low-pass that let a carrier 25 kHz away through at -28 dB (a
  neighbour 40 dB stronger read 11.6 dB high). Noise alone now reads in
  a 19.7 kHz noise bandwidth.
- Sensitivity sweep `yantra-sens` (RAD-126, docs/SENSITIVITY.md): decode
  rates, CRC error rate, BER, lock time and MER against SNR for awgn,
  cfo2/20/50, offcentre, clock50, tu50, ht200, phasenoise and combined;
  quick tier per scenario under the ctest label `sens` (sens-quick
  workflow), full tier `sens_full` (labels slow, release), baselines in
  tests/sens/baseline.
- Simulator radio plugin `simulator` (RAD-61), shown as "Demo (simulated
  network)", with SIMULATED and SCENE: the generator's scenario engine
  makes IQ inside the plugin and the same measurement engine decodes it.
  The scenario comes from the example id of the plan shown (RAD-18) or a
  setting; levels follow the multi-wall model (RAD-73) at the cursor,
  walk or a fixed position, records carry `sim_position`; scene changes
  reload; without plan or position the scenario levels are used. Device
  settings, presets, sensors, self-test, practice calibration (a
  generator at a known level, true offset as a sensor), MS maximum power
  for C1 (default 30 dBm), real-time pacing with playback speed, and
  OVERFLOW/DROPPED when the sink is slow (RAD-24, RAD-25). The bundled
  scenarios are embedded in the plugin. Tests through the test host's
  emulated context callbacks (RAD-94).
- Shared TETRA plugin glue `yantra::tetra_plugin` (plugins/tetra_common):
  schema table, plan checks, record delivery counters, C1 with a known
  calibration, setting helpers and the GSMTAP settings, for the
  simulator and the SoapySDR plugins.
- `EngineConfig::playback_speed`: record times run this many times faster
  than the signal (the simulator's playback speed).
- `gen::parse_scenario(text, reader)`: scene files from a reader (the
  simulator's embedded scenarios).
- Tests (RAD-90, Yantra TST-01): the long doctest binaries run one ctest
  per test case (`yantra_doctest_tests()`, cmake/YantraTests.cmake; COST
  from tests/ctest_costs.txt), so `ctest -j` finishes in about a minute
  instead of three and a half. The long robustness suite and the full
  sensitivity tier are opt-in (label opt_in, `YANTRA_TESTS_OPT_IN=1`);
  the real-time checks carry the label timing, which the sanitizer
  presets and the ci-full sanitizer jobs leave out; their hang guards
  scale with YANTRA_TEST_TIME_SCALE (5).
- SDR dependencies (RAD-132, RAD-136): SoapySDR 0.8.1 with SoapyHackRF
  0.3.6/libhackrf 2026.01.3, SoapyRTLSDR 0.3.4/librtlsdr 2.0.3 and
  SoapyAirspy 0.2.1/libairspy 1.0.10 on libusb 1.0.30, vendored from
  their release archives and built statically into the plugins; the
  module loader is replaced so nothing is loaded from the system.
- Shared SoapySDR layer `yantra::soapy_plugin` (RAD-54) and the generic
  plugin `soapysdr` (RAD-62): enumeration with stable keys, settings and
  sensors generated from the module (RAD-51), external reference
  (RAD-53), diagnostics (RAD-52), overflow and drop events, hot-plug
  (RAD-35). A fake SoapySDR module for the tests (RAD-92); the conformance
  kit runs against it, unplug and replug included.
- `yantra-radio-host capture-iq` (RAD-63): SDR capture to SigMF in four
  formats with a rolling buffer and capture segments at drops.
- HackRF plugin `hackrf` (RAD-60) on the SoapySDR layer's device
  profiles: 2-20 Msps, LNA/VGA gains and the RF amplifier switch, antenna
  port power (off at every open and replug, never in a preset), offset
  tuning, presets, sensors and self-test items, band scans wider than
  the span (RAD-23) and C1 from a calibration offset (RAD-22). All
  SoapySDR plugins gain clipping (OVERLOAD), saturation warning, noise
  floor, receiver load, tuned frequency and build info sensors.
- `yantra-hackrf` (RAD-66): HackRF One test tool through the plugin
  (list, info, selftest, settings, measure, capture, noise, level, ppm,
  throughput, replug, report), text or JSON, exit codes for scripts,
  docs/YANTRA_HACKRF.md.
- Install rules (RAD-99): plugins, tools, udev rules and the RTL-SDR
  DVB-T blacklist (udev/), the licence bundle (RAD-137) and the docs,
  tested by an install into a DESTDIR.
- TETRA terminal plugin `pei` (RAD-80, TRM-01, TRM-04), in plain C11: a
  terminal in TMO read over its PEI (AT commands, EN 300 392-5) on a
  serial port (POSIX termios; Windows DEFERRED): serving and neighbour
  cell records in dBm with C1 from the terminal's cell lists (vendor
  commands set as settings, the documented example format by default),
  AT+CSQ and AT+CREG, the radio info (AT+GMI/GMM/GMR/GSN) as sensors, the
  AT command log, hot-plug of the port (RAD-35) and only reading commands
  (RAD-17). Tested against `yantra-pei-sim`, a simulated terminal on a
  pseudo-terminal answering from a generator scenario (errors, timeouts,
  garbage, unplug/replug), with the conformance kit and the fuzz target
  `pei` for the AT parser (RAD-96).
- SoapyRemote 0.5.3 (RAD-50, network radios) built into the SDR plugins:
  the generic plugin lists the devices of the SoapySDRServers named in
  `YANTRA_SOAPY_REMOTE` (host[:port], comma-separated) as
  `remote:<serial>`; the module's multicast discovery never runs in a
  plugin. Tested end to end with SoapyRemote's server side on the
  loopback serving the fake module (records match the generator's
  truth) and with the conformance kit. SoapyBladeRF is not built: libbladeRF
  needs the ADI no-OS AD9361 driver, whose licence is not GPL-3.0
  compatible (THIRD_PARTY.md).
- Documentation (RAD-97, RAD-135): ARCHITECTURE.md (layers, data flow,
  threads), BUILD.md (Linux, MSYS2 UCRT64, static dependency builds,
  completeness checks, release), DEVICES.md (RAD-95: the test devices
  measured, the real radios open), CONTRIBUTORS.md, docs/IQ-FORMATS.md,
  one page per plugin in docs/plugins/ (devices, settings, presets,
  sensors, self-test, known issues), the record schemas with the source
  of every field (message and EN 300 392-2 clause, or how it is measured)
  incl. the terminal schema, and the robustness checklist moved to
  REAL_WORLD_CHECKLIST.md. Docs checks: `doc_links` (relative links and
  anchors) and `plugin_docs` (the plugin pages' generated parts and the
  schema tables against the built plugins).
- Licence bundle (RAD-137) made from tools/deps/manifest.tsv by
  `tools/ci/licence_bundle.py` (checked against third_party/, LICENSES/
  and THIRD_PARTY.md, ctest `licence_bundle`), with the copyright notices
  of the libraries whose licence is only in their source headers, the Rust
  standard library and the index BUNDLE.md; installed to
  share/licenses/yantra-radios and in every release package.
- Release (RAD-98, RAD-134, RAD-138): `ninja release`
  (`tools/ci/make_release.sh`) writes build/release/v<version>/ with the
  plugin package (the install tree), the corresponding source with the
  upstream release archives of the bundled libraries and their SHA-256,
  the release notes and SHA256SUMS; the packaged plugins pass the
  completeness check. release.yml does the same per platform on a tag.
