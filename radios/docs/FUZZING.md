<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Fuzzing (RAD-96)

Every parser that reads untrusted input has a fuzz target: a function
`LLVMFuzzerTestOneInput` in `tests/fuzz/target_<name>.cpp`. A target
fails by crashing: a sanitizer report, an exception other than the
parser's documented error, or a broken property (`fuzz::fail`).

| Target | Input | Also checked |
|---|---|---|
| `tetra_pdu` | byte 0 options, then bits packed MSB first | every bit-level TETRA parser (MAC-SYNC, MAC-SYSINFO by osmo-tetra and by the protocol crate, which must agree, ACCESS-ASSIGN, RM(30,14), MAC PDU classes, block PDUs, TM-SDU, field-by-field text, frequencies) and the crate's C ABI directly; PDU offsets in the block, no Rust panic (`YANTRA_PROTO_ERR_PANIC`) |
| `tetra_bursts` | bursts: 1 type byte + 510 soft bits | lower MAC (osmo-tetra burst split, descrambling, Viterbi, CRC, RM(30,14)), reassembly, GSMTAP scope filter (RAD-115): no datagram holds an addressed, encrypted or out-of-scope PDU |
| `iq` | byte 0 format, byte 1 options, byte 2 block size, then IQ file content | sample conversion as decode-iq reads files (RAD-63) and the whole measurement engine (search, channelizer, demodulator, decoder, records) with zero-length blocks, discontinuities, NaN/Inf; every sample counted |
| `sigmf` | `.sigmf-meta` JSON | accepted metadata has capture segments sorted from sample 0 |
| `scenario` | yantra-radios-scenario JSON (RAD-74) | an accepted scenario gives every cell's broadcast configuration; the scenario engine refuses it with its documented errors or makes samples and ground truth (inputs naming scene files are skipped) |
| `scene` | yantra-radio-scene JSON (RAD-36) | an accepted scene reads back unchanged from `scene_to_json`; the multi-wall model gives a level for every cell |
| `record_file` | yantra-radio-records file (RAD-65) | every accepted record is written again and reads back to the same line |
| `pei` | byte 0: the command (low bits) and the read size (high bits), then the terminal's reply bytes | the terminal plugin's AT parser (RAD-80): reply lines, result codes, AT+CSQ, AT+CREG, identification texts, cell lines by field list and field-list texts; values within their documented ranges, carrier numbers round-trip, every buffer terminated |

PEI responses (RAD-96) get their target with the terminal plugin
(RAD-80). The Rust crate has the same entry points in
`receiver/rust/src/fuzz.rs` (`yantra_proto::fuzz::tetra_pdu`, same input
format as the C++ `tetra_pdu`, and `tm_sdu`), cargo-fuzz targets in
`receiver/rust/fuzz/` and a stable-Rust smoke test
`receiver/rust/tests/fuzz_smoke.rs`.

## In every build: replay under ctest

Normal builds link each target with `tests/fuzz/replay_main.cpp`. The
ctest test `fuzz_seeds` writes the seed corpus with `yantra-fuzz-seeds`
from the generator and yantra-gen into `<build>/fuzz-corpus/<target>/`
(nothing large is committed); `fuzz_<target>` then replays it, plus the
committed crash inputs in `tests/fuzz/regressions/<target>/`, plus 24
seeded mutations of each input; `fuzz_proto_rust` runs the Rust smoke
test on the `tetra_pdu` and `tm_sdu` corpora. All carry the labels
`fast` and `fuzz`:

```sh
ctest --test-dir build -L fuzz
```

The sanitizer presets (`asan-ubsan`, `tsan`) run the same tests. To find
which mutated input crashed a replay, run it with
`YANTRA_FUZZ_SAVE=<file>`: every input is written there before it runs.

## libFuzzer runs (clang)

The `fuzz` preset (`build-fuzz/`, clang of the cross SDK, ASan + UBSan,
`YANTRA_FUZZ=ON`) builds the targets as libFuzzer binaries in
`build-fuzz/fuzz/`; the other trees are not affected. All C and C++ code and the Rust crate
(stable `rustc` codegen options for SanitizerCoverage,
`cmake/YantraFuzz.cmake`) are instrumented, so `tetra_pdu` fuzzes the
Rust parsers with coverage feedback through the C ABI.

```sh
tools/ci/fuzz.sh 60               # every target for 60 s, in parallel
tools/ci/fuzz.sh 300 tetra_bursts # one target for 5 minutes
```

The script configures and builds the preset, writes the seeds, adds the
regression inputs and runs each target with `-max_total_time`,
`-timeout=20`, `-rss_limit_mb=2048` and `-malloc_limit_mb=1024` (no
allocation without bound). The grown corpus stays in
`build-fuzz/fuzz-corpus/`; findings and logs go to
`build-fuzz/fuzz-artifacts/`. By hand:

```sh
cmake --preset fuzz && cmake --build --preset fuzz --target fuzz-all
build-fuzz/bin/yantra-fuzz-seeds build-fuzz/fuzz-corpus .
build-fuzz/fuzz/fuzz_scenario -max_total_time=60 build-fuzz/fuzz-corpus/scenario
build-fuzz/fuzz/fuzz_scenario <crash file>   # reproduce one input
```

## cargo-fuzz

`receiver/rust/fuzz/` is a cargo-fuzz project (its own workspace,
outside the crate's). It needs the `libfuzzer-sys` crate from crates.io
(pinned in its `Cargo.lock`; fetched on the first run, so that run needs
the network) and nightly `rustc` features. Without a nightly toolchain
`tools/ci/fuzz.sh` uses the stable `rustc` with `RUSTC_BOOTSTRAP=1` and
sanitizer `none` (the crate is safe Rust; debug assertions and overflow
checks stay on; the sanitizer runtimes are not part of a stable
toolchain):

```sh
RUSTC_BOOTSTRAP=1 cargo fuzz run --fuzz-dir "$PWD/receiver/rust/fuzz" -s none \
  --target-dir "$PWD/build-fuzz/cargo-fuzz" tetra_pdu -- -max_total_time=60
```

## When a fuzzer finds something

1. Reproduce with the crash file; fix the cause in the parser (not in
   the target).
2. Copy the input to `tests/fuzz/regressions/<target>/<short-name>` (a
   few bytes to a few KB; text inputs may be minimised with
   `-minimize_crash=1`) and re-run cmake so the replay test picks up the
   folder. Every crash keeps its regression input.
3. Add a unit test next to the parser's other tests when the case says
   something about the format.

New parser: add `target_<name>.cpp`, its name and libraries to
`tests/fuzz/CMakeLists.txt` and its seeds to `tests/fuzz/seeds.cpp`.
