<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Building yantra-radios

How to build the plugins, the tools and the tests, how the bundled
libraries are built in statically, the completeness checks and the
release (RAD-01..05, RAD-131..138). Tests and their labels:
[CONTRIBUTING.md](CONTRIBUTING.md#building-and-testing).

## Linux (x86-64; arm64 is optional)

Needs CMake >= 3.20, Ninja, Python 3, cargo/rustc (the protocol crate,
RAD-101) and binutils (readelf, for the completeness check). The compiler
of the default trees is clang of the cross SDK (below; `tools/sdk/yantra_sdk.py
make` once, nothing installed in the system); the system gcc (>= 13) builds
the second tree `build-gcc` (presets `gcc`, `gcc-scalar`, `ci-gcc`). Optional: Doxygen (the radio API reference), reuse
(licence lint), bubblewrap (the clean-environment kit run). No SoapySDR,
vendor library or libusb package is needed: they are built from
`third_party/`.

    git submodule update --init
    tools/sdk/yantra_sdk.py make
    cmake --preset default            # clang of the SDK, tree build/
    ninja -C build
    ctest --test-dir build -j"$(nproc)"
    cmake --preset gcc && ninja -C build-gcc && ctest --test-dir build-gcc -j"$(nproc)"

A plain `cmake -S . -B <tree>` without a preset or toolchain file takes the
system's compiler. `tools/ci/run_linux.sh [build-dir]` runs the same steps
as the CI job (static dependency build, warnings as errors, fast tests,
completeness, lint); `YANTRA_CI_COMPILER=gcc` for the gcc job.

### Presets (CMakePresets.json)

All presets except the gcc ones use the SDK's clang
(`CMAKE_TOOLCHAIN_FILE=tools/sdk/toolchain-x86_64-linux-gnu-clang.cmake`,
with the SDK's lld, Rust and llvm-symbolizer).

| Preset | Tree | What |
|---|---|---|
| `default` | `build` | RelWithDebInfo, SIMD kernels |
| `scalar` | `build-scalar` | `-DYANTRA_SIMD=OFF`: the plain C++ fallback, same results (goldens identical) |
| `ci`, `ci-scalar` | `build` | warnings are errors (`YANTRA_WERROR`) |
| `asan-ubsan` | `build-asan` | AddressSanitizer + UBSan, hang guards x5; tests `-LE timing` |
| `tsan` | `build-tsan` | ThreadSanitizer, hang guards x10; tests `-LE timing` |
| `fuzz` | `build-fuzz` | clang + libFuzzer targets (RAD-96, [docs/FUZZING.md](docs/FUZZING.md)) |
| `gcc` | `build-gcc` | the system gcc/g++, RelWithDebInfo, SIMD kernels |
| `gcc-scalar` | `build-gcc-scalar` | the system gcc, `-DYANTRA_SIMD=OFF` |
| `ci-gcc` | `build-gcc` | the system gcc, warnings are errors (CI job `linux-x86-64-gcc`) |

Both compilers build with zero warnings; a warning of either is fixed in
the code, never silenced globally (third-party sources keep their own
flags).

### Options

| Option | Default | Meaning |
|---|---|---|
| `YANTRA_SIMD` | ON | SIMD kernels of the receiver |
| `YANTRA_WERROR` | OFF | warnings are errors (ON in CI) |
| `YANTRA_SANITIZER` | "" | `asan-ubsan` or `tsan` |
| `YANTRA_DEPS_PREFIX` | "" | use the dependency build of `tools/deps/build_deps.sh` instead of building them in-tree |
| `YANTRA_RADIOS_BUILD_TESTS` | ON | the test suite |
| `YANTRA_RADIOS_SOURCE_URL` | the public repository | source URL in the plugin descriptors (RAD-12, RAD-137) |

## Bundled libraries, built statically (RAD-132, RAD-136)

Every library a plugin uses is pinned under `third_party/` (a git
submodule, or an unchanged subset of a release archive whose SHA-256 is in
`tools/deps/manifest.tsv`) and compiled with only the features used, as
position-independent code with hidden symbols, into each plugin:
osmo-tetra, libosmocore and talloc (`tools/deps/deps.cmake`), SoapySDR with
the HackRF, RTL-SDR, Airspy and Remote modules, libhackrf, librtlsdr,
libairspy and libusb (`tools/deps/sdr.cmake`), pocketfft, nlohmann/json and
the Rust protocol crate. SoapySDR's module loader is replaced so no system
SoapySDR or module is ever loaded. The list, versions, licences and local
replacements: [THIRD_PARTY.md](THIRD_PARTY.md), [tools/deps/README.md](tools/deps/README.md).

- In-tree (default): the main build compiles them, so sanitizer trees
  instrument them too.
- Prefix: `tools/deps/build_deps.sh <prefix>` builds and installs them once
  (reused while `tools/deps` and the vendored sources are unchanged, the
  CI cache key); configure with `-DYANTRA_DEPS_PREFIX=<prefix>`.
- `tools/deps/verify_sources.py [cache]` downloads each release archive
  (or takes it from `build/deps-cache`), checks its SHA-256 and that every
  vendored file is byte-identical to the archive's (CI lint job).

Each artefact embeds the dependency manifest (name, version, licence):
`yantra-radio-host --version` and the plugins' "Build info" sensor
(RAD-45).

## Completeness checks (RAD-133)

- `tools/ci/check_completeness.py <dir or .so>`: the NEEDED entries hold
  only the allowed system libraries (glibc parts, libudev), no RPATH, no
  text relocations or executable stack, and only `yantra_radio_entry` is
  exported (version script `cmake/radio_plugin.map`). The file also names
  no folder of the machine that built it (this source tree, the builder's
  home folder): `tools/deps/deps.cmake` and `cmake/RustProto.cmake` make
  source paths relative with `-ffile-prefix-map` and `--remap-path-prefix`,
  so a debugger needs `directory <source tree>`. `package_release.sh` looks
  for the same folders in every file of the package (not in an `asan-ubsan`
  tree, never shipped: AddressSanitizer keeps the full paths). ctests
  `completeness_<plugin>` and `completeness_plugins`.
- `tools/ci/clean_env.py`: the conformance kit on the plugins in an
  environment without SoapySDR or vendor packages (bubblewrap locally,
  ctest `clean_env_conformance`; a bare ubuntu:24.04 container in CI with
  `--sandbox none`).

## Install (RAD-99)

`cmake --install build --prefix /usr` (or any prefix; `DESTDIR` works):
plugins to `lib/yantra-radios/plugins`, tools to `bin`, scenarios to
`share/yantra-radios`, udev rules to `lib/udev/rules.d`, the RTL-SDR DVB-T
blacklist to `lib/modprobe.d`, the licence bundle to
`share/licenses/yantra-radios`, docs to `share/doc/yantra-radios`.
Nothing is written to `/etc` and nothing runs udevadm: for a prefix
other than `/usr`, copy the udev rules to `/etc/udev/rules.d` and reload
udev as root. ctest `install_destdir` checks the result.

## Cross SDK (RAD-139)

One folder with everything the clang trees and the Windows cross build
need, made from pinned downloads only (no sudo, nothing installed in the
system, no MSYS2 or Windows needed; about 1.2 GB of downloads, 1.8 GB
unpacked):

| Folder | What | Lock file |
|---|---|---|
| `llvm/` | one LLVM release: clang, clang++, lld, llvm-ar/ranlib, llvm-rc, llvm-objdump, llvm-strip, llvm-symbolizer and more, compiler-rt (sanitizers, libFuzzer) | `tools/sdk/llvm.lock` |
| `sysroots/x86_64-w64-mingw32/` | the Windows x64 (UCRT) target sysroot: MSYS2 UCRT64 binary packages (mingw-w64 headers, CRT and import libraries, winpthreads, the GCC runtime with libstdc++ and libgcc), only unpacked | `tools/sdk/msys2.lock` |
| `rust/` | rustc and cargo with the standard libraries of x86_64-unknown-linux-gnu and x86_64-pc-windows-gnu | `tools/sdk/rust.lock` |
| `manifest/` | the lock files it was made from and every unpacked file with its SHA-256 | |

    tools/sdk/yantra_sdk.py make     # download (cached), check, unpack
    tools/sdk/yantra_sdk.py check    # every lock file against what is unpacked

- Location: `sdk/` in the repository (git-ignored), or `$YANTRA_SDK_DIR`
  (or `--sdk-dir`; CMake: `-DYANTRA_SDK_DIR`); the downloads are cached in
  `sdk-cache/` or `$YANTRA_SDK_CACHE` (`--cache`), so a re-run downloads
  nothing. Every path inside the SDK is relative: the folder can be moved
  or shared (Yantra uses the same SDK through its yantra-radios submodule
  with `YANTRA_SDK_DIR`; `--msys2-lock <file>` adds the MSYS2 packages of
  another lock file to the sysroot).
- Outside the source tree (1.8 GB SDK, 1.4 GB cache; a tool that copies
  the tree, such as the Windows test VM's runner below, then leaves them
  out): either `YANTRA_SDK_DIR`/`YANTRA_SDK_CACHE` (and `-DYANTRA_SDK_DIR`
  for every tree), or links in place of the folders, which every tree and
  script then uses unchanged:

      mv sdk ~/yantra-sdk && ln -s ~/yantra-sdk sdk
      mv sdk-cache ~/yantra-sdk-cache && ln -s ~/yantra-sdk-cache sdk-cache

  Compiled files never name the SDK's folder: its paths (the given and
  the real one) are mapped to `sdk/` in debug information
  (`-ffile-prefix-map`, `tools/deps/deps.cmake`).
- Checks: each download's SHA-256 against the lock file and its
  publisher's signature with a private, temporary gpg home: LLVM's `.sig`
  with llvm.org's release keys (the signer's fingerprint is in
  `llvm.lock`), Rust's `.asc` with the Rust release key, every MSYS2
  package's `.sig` with the keys of the `msys2-keyring` package pinned in
  `msys2.lock`. `check` fails on any difference between the lock files and
  the SDK (ctest `sdk_check`); the toolchain files refuse an SDK made from
  other lock files.
- New versions: `tools/sdk/yantra_sdk.py update --llvm latest` (the
  newest stable LLVM release with a signed Linux x86-64 archive),
  `--msys2` (today's packages), `--rust` (the version of the `rustc` on
  PATH); then `make`. MSYS2 removes old package versions after a while:
  keep `sdk-cache/` to remake an older SDK.
- Rust: the Windows `rust-std` loads only into the official `rustc` build
  of the same version (a distribution's own `rustc` build is refused,
  E0514), so the SDK carries the official `rustc` and `cargo` of the
  system's version, and the toolchain files make the build use them
  (`YANTRA_CARGO_EXECUTABLE`, `RUSTC`) for every target and test. The
  system Rust stays as it is (the gcc trees use it).
- Toolchain files (`-DCMAKE_TOOLCHAIN_FILE=`; the SDK folder from
  `-DYANTRA_SDK_DIR`, `$YANTRA_SDK_DIR` or `sdk/`):
  - `tools/sdk/toolchain-x86_64-linux-gnu-clang.cmake`: Linux with the
    SDK's clang and lld, the system's glibc and libstdc++;
  - `tools/sdk/toolchain-x86_64-w64-mingw32-clang.cmake`: Windows x64,
    target `x86_64-w64-windows-gnu`, `--sysroot` the SDK's sysroot, lld,
    libstdc++, `llvm-rc` for resources, the GCC runtime linked statically
    (`YANTRA_WIN_STATIC_RUNTIME`, ON) and winpthreads added to every link
    (`tools/sdk/mingw-clang-rules.cmake`: clang's MinGW driver does not
    link it, MSYS2's gcc does).
- On WSL2 (optional), Windows programs run on the PC's Windows through
  Windows interop: `tools/sdk/wsl_run.sh [--with <dll-folder>]
  <program.exe> [args]` copies the program into a scratch folder under
  the Windows `%TEMP%`, runs it and deletes the folder; with `--in-place`
  it runs the program where it is (the optional ctest runner of the
  Windows tree, below); `--check` exits 77 without interop. ctest
  `sdk_windows_smoke` (label slow; skipped without SDK or WSL interop)
  cross-compiles a C++20 program with threads, iostream, std::filesystem,
  exceptions, a version resource and a Rust static library, checks with
  `llvm-objdump -p` that it imports only Windows system DLLs and the UCRT,
  and runs it that way.

## Windows x64 (RAD-131)

The Windows tree is cross-compiled on Linux with the cross SDK (above):
`.dll` plugins, `yantra-radio-host.exe`, `yantra-gen.exe`,
`yantra-hackrf.exe` and every test program, the bundled libraries built
statically from the same sources (RAD-136), the Rust code with the SDK's
`cargo` for `x86_64-pc-windows-gnu`. Its ctest runs natively on a
Windows machine, the Windows test VM:

    python3 tools/sdk/yantra_sdk.py make        # once: the SDK (above)
    cmake --preset windows                      # build-win/
    ninja -C build-win
    wintest.sh test build-win                   # ctest in the Windows VM

Presets: `windows` (build-win), `windows-scalar` (build-win-scalar,
`YANTRA_SIMD=OFF`), `ci-windows` (warnings are errors). No emulator: every
test command is a Windows program (`.exe`), `cmake` or `python3` (no shell
scripts), so the tree's `CTestTestfile.cmake` runs as it is on Windows.

The VM runner (`wintest.sh test <build-dir> [-j N] [ctest -R/-E/-L/-LE
args]`; the maintainers' VirtualBox Windows 11 VM) needs on the guest only
a portable CMake and Python 3 that `/usr/bin/cmake` and `/usr/bin/python3`
resolve to (`C:\usr\bin`), no MSYS2, no shell. It mirrors the source tree
(without `.git` and `build*`; keep the SDK outside, above) and the build
folder (without objects and archives) to the same absolute paths on `C:`
(`/home/...` is `C:\home\...`: Windows reads a rooted path on the
current drive, so the Linux paths in the test list and compiled into the
tests resolve there), restores a clean snapshot, runs ctest, copies
`junit.xml` and `LastTest.log` back to `<build-dir>/wintest-results/<time>/`
and powers the VM off; its exit code is ctest's. About 1.1 GB is pushed
per run (the build-win programs with their debug information). Expected:
339 tests: 319 passed and 20 skipped with the reason (the 6
Linux-only tests below, the 13 build-machine checks and `fuzz_proto_rust`,
below), build-win and build-win-scalar alike; the `timing` tests are best
run on their own (`-L timing -j 1`, the rest with `-LE timing`).

How the cases are listed: the build cannot run a Windows program, so it
writes one ctest per doctest group (`<group>/all`); ctest on Windows runs
each `.exe --list-test-cases` when it reads the tests
(`cmake/doctest_discover.py --at-test-time`) and registers one ctest per
case, as in the Linux trees (`ctest -N` on Linux shows the groups only).

Build-machine checks (label `build_host`): the tests that check the build
machine or run its tools (`sdk_check`, `sdk_windows_smoke`, `deps_build*`,
`deps_manifest`, `repo_lint`, `reuse_lint`, `radio_api_doxygen`,
`radio_api_header_*`, `rust_cargo_test`, `release_package`) run through
`cmake/build_host_test.py`: skipped (77) on Windows, run on Linux with
`ctest --test-dir build-win -L build_host`. `fuzz_proto_rust` is skipped
in this tree (its cargo runs on the build machine, its corpus comes from
the Windows `yantra-fuzz-seeds.exe`; the Linux trees run it).

On WSL2 the tree can run its ctest on the PC's Windows instead
(optional): `cmake --preset windows -DYANTRA_WINDOWS_TEST_RUNNER=wsl`
makes `tools/sdk/wsl_run.sh --in-place` the emulator
(`CMAKE_CROSSCOMPILING_EMULATOR`): ctest runs on Linux, each `.exe` where
it was built (`\\wsl.localhost\<distribution>`), with PATH holding only
the Windows system folders and the `YANTRA_*`/`SOAPY_*` variables of its
ctest (through `WSLENV`); the cases are listed at build time. Without WSL
interop the option is ignored with a warning. The CI's Linux job runs
only the checks of the tree (`ctest -L 'completeness|release'`); its
Windows runner runs the programs with `tools/ci/run_windows_tests.py`.

What runs on Windows: every doctest program, the CLI tests of the three
tools, the conformance kit on every plugin `.dll` (beacon, simulator,
soapysdr through its fake module, hackrf through its fake; the terminal
plugin needs a terminal), and the checks:

- `completeness_<plugin>`, `completeness_plugins`: imports only Windows
  system DLLs and the UCRT (no `libstdc++-6.dll`, `libgcc_s_seh-1.dll`,
  `libwinpthread-1.dll`, nothing of MSYS2, no `msvcrt.dll`), exports only
  `yantra_radio_entry` (the `.def` file), no build folder inside
  (`tools/ci/check_completeness.py --platform windows`, which reads the
  PE import and export tables itself, so it runs on Windows too, or with
  `--objdump` from GNU or llvm-objdump output; `--build-folder` names the
  build machine's folders when it runs on another one);
- `completeness_programs` (every `.exe`: imports) and
  `completeness_programs_shipped` (the three tools: also no build folder);
- `completeness_negative` (a plugin importing a non-system DLL and
  exporting everything is caught), `completeness_build_paths`;
- `release_package`: the `.zip` package (below; a build-machine check).

Skipped on Windows (reported as skipped, with the reason, by
`yantra_skip_test()`): `test_pei` and `conformance_pei` (the simulated
terminal `yantra-pei-sim` needs a POSIX pseudo-terminal; the plugin's
Windows serial port is the next step), `plugin_docs` (needs that
simulator), `clean_env_conformance` (a bubblewrap sandbox; the CI's
Windows runner is the clean machine), `install_destdir` (the Linux FHS
layout with udev rules), `completeness_surprises` (static TLS and GNU
unique symbols are ELF properties).

Golden files: `tests/gen_sim/golden/windows` and
`tests/yantra_gen/golden/windows` hold the Windows digests. The UCRT's
math functions round some results differently in the last bit from
glibc's; same seeds and scenarios, rewritten the same way
(`ninja -C build-win update-golden-gen-sim update-golden-yantra-gen`).
They can also differ between Windows machines (the UCRT picks
CPU-dependent code paths, e.g. FMA3): the `cells_cs16_impaired` digest of
`test_yantra_gen` written on a PC through WSL differs from the one the
test VM computes (that VM gives the Linux value); open, see CHANGELOG.

Portability lives in a few small interfaces: plugin loading
(`tools/yantra-radio-host/dynlib.cpp`, `LoadLibraryExW` with the plugin's
own folder first), the test helpers `tests/test_platform.hpp`
(environment variables, starting a program without a shell, shell
commands for the `--*-cmd` options, a loopback UDP receiver) and the
GSMTAP socket (Winsock started on first use).

Pitfalls:

- Upstream libraries mark their API `dllexport` on Windows whatever is
  defined (SoapySDR's `Config.h`, `hackrf.h`, `airspy.h`, SoapyRemote).
  SoapySDR is compiled against a copy of its `Config.h` without
  `SOAPY_SDR_DLL` (upstream's static case), and the export directives
  (`.drectve`) are removed from every bundled archive after it is built
  (`llvm-objcopy --remove-section=.drectve`, `tools/deps/sdr.cmake`);
  otherwise a plugin exports hundreds of symbols next to its `.def` entry.
- SoapyRemote 0.5.3 writes its Windows code for MSVC only (`_MSC_VER`);
  four of its files are compiled from copies in the build tree with
  `_MSC_VER` read as `_WIN32` (`tools/deps/sdr.cmake`).
- A plugin is linked with `-static` only; a `-Wl,-Bdynamic` after it
  makes the driver import `libstdc++-6.dll` again.
- `std::system` runs `cmd.exe`, which refuses a network-share current
  folder (`\\wsl.localhost`): tests start programs with
  `yantra::test::run_program()` and give `cmd.exe` absolute native paths.
  `cmd.exe /c` strips the first and last quote of a line, so
  `yantra-radio-host conformance --unplug/--replug` encloses the command
  once more on Windows; the conformance tests use
  `"<cmake>" -E touch/rm -f "<file>"`, the same on Linux and Windows.
- UCRT math functions may round differently from one Windows machine to
  another (CPU-dependent code paths); see the golden files above.
- libstdc++'s `lexically_normal()` shortens a UNC path's leading `\\`.
- Paths written into JSON text use `generic_string()` (backslashes would
  be escapes).
- Never two heavy jobs at once: a Windows ctest run through WSL interop
  uses all cores of the PC as well.

The bundled libraries as a prefix for Windows:

    CMAKE_TOOLCHAIN_FILE=$PWD/tools/sdk/toolchain-x86_64-w64-mingw32-clang.cmake \
        tools/deps/build_deps.sh build-win-deps
    cmake --preset windows -DYANTRA_DEPS_PREFIX="$PWD/build-win-deps"

Natively on Windows, in an MSYS2 UCRT64 shell (gcc; `windows-check.yml`,
not run here):

    pacman -S git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,python,rust,binutils}
    tools/deps/build_deps.sh build/deps
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DYANTRA_DEPS_PREFIX="$PWD/build/deps"
    cmake --build build
    ctest --test-dir build -LE slow

Open Windows items: the terminal plugin's serial ports (COM ports) and
its fake terminal, and the WinUSB/Zadig driver notes.

## CI (RAD-134)

`.github/workflows/`: `ci.yml` (every push: lint incl. shell, YAML,
Python, FFI widths, reuse, cargo-deny, `verify_sources.py`, workflow YAML
with PyYAML; Linux build and fast tests with the SDK's clang (the SDK
cached per lock-file version) and with the system gcc; the Windows tree
cross-compiled with the SDK (`windows-cross`: build with warnings as
errors, completeness checks, release package) and its tests natively on
a Windows runner (`windows-x86-64-tests`: the tree without objects as an
artefact, `tools/ci/run_windows_tests.py` runs every `.exe` test of the
cross job's `ctest --show-only=json-v1` list, then the conformance kit on
every `.dll`); completeness checks incl. the clean environment),
`ci-full.yml` (nightly
and before a release: opt-in and slow suites; clang trees x86-64, scalar,
ASan+UBSan, TSan; gcc trees x86-64 and arm64), `sens-quick.yml` (sensitivity
sweep), `windows-check.yml` and `release.yml`. The workflows are complete
while the repository has no remote; `tools/ci/run_linux.sh` runs the same
Linux steps locally.

## Docs

`ninja -C build docs-radio-api`: the radio API reference (Doxygen) in
`build/docs/radio-api/html` (ctest `radio_api_doxygen` fails on any
Doxygen warning). Docs checks: `doc_snippets` (code in the tutorial is the
compiled example plugin), `doc_links` (relative links and anchors),
`plugin_docs` (the plugin pages' settings, presets, sensors and self-test
items, and the record schema tables, against the built plugins;
`ninja -C build update-plugin-docs` rewrites the generated parts).

## Release

The version is `project(YantraRadios VERSION ...)` in CMakeLists.txt, the
tag `v<version>` (annotated), the notes the CHANGELOG.md section of the
version (RAD-98). Before tagging: the full set of CONTRIBUTING.md (default
and scalar trees, opt-in suites, both sanitizer trees, reuse lint,
`verify_sources.py`), then

    ninja -C build release

which runs `tools/ci/make_release.sh` into `build/release/v<version>/`:

| File | Contents |
|---|---|
| `yantra-radios-v<version>-linux-x86_64.tar.gz` | the install tree: plugins (`lib/yantra-radios/plugins/*.so`), tools, scenarios, udev rules, docs and the licence bundle `share/licenses/yantra-radios` (LICENSE, LICENSES/, THIRD_PARTY.md, each bundled library's licence texts and copyright notices, BUNDLE.md) |
| `yantra-radios-v<version>-windows-x86_64.zip` | the Windows tree's install tree (`tools/ci/package_release.sh build-win <out> <tag> windows-x86_64`): `lib/yantra-radios/plugins/*.dll`, `bin/*.exe`, scenarios, docs and the same licence bundle |
| `yantra-radios-v<version>-source.tar.gz` | the complete corresponding source (RAD-137): the repository at the tag with the pinned submodules (without osmo-tetra's `crypto/`) and `upstream/` with every release archive of `tools/deps/manifest.tsv` and its SHA256SUMS |
| `RELEASE_NOTES.md` | the CHANGELOG.md section |
| `SHA256SUMS` | SHA-256 of the files above |

The packaged plugins pass the completeness check before the folder is
written. `release.yml` does the same on a pushed tag for Linux and
Windows x64 (cross-compiled) and publishes the files. Yantra pins the tagged commit
as its submodule and builds the plugins with these scripts, or takes the
release `.so` files of the tag (RAD-138).
