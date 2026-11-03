#!/bin/bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bounded libFuzzer run of every fuzz target (RAD-96): configures and
# builds the "fuzz" preset (clang, ASan + UBSan, libFuzzer; build-fuzz/),
# writes the seed corpus from the generator, adds the committed crash
# regressions, then runs each target for a fixed time, several at once.
# New corpus inputs stay in build-fuzz/fuzz-corpus/<target>/, findings go
# to build-fuzz/fuzz-artifacts/<target>-crash-* (logs <target>.log). The
# Rust crate's cargo-fuzz targets (receiver/rust/fuzz) run as well when
# cargo-fuzz is installed: with a nightly rustc, or with a stable one
# through RUSTC_BOOTSTRAP=1 (sanitizer "none": the crate is safe Rust;
# debug assertions and overflow checks stay on). Their first build fetches
# the libfuzzer-sys crate (pinned in receiver/rust/fuzz/Cargo.lock).
# Usage: tools/ci/fuzz.sh [seconds per target, default 60] [target...]
# Copied from yantra 68ea20a: ci/fuzz.sh, adapted (parallel, Rust part).
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
secs=${1:-60}
shift || true
build="$root/build-fuzz"
jobs=$(nproc)

cmake --preset fuzz -S "$root" > /dev/null
cmake --build --preset fuzz --target fuzz-all
corpus="$build/fuzz-corpus"
art="$build/fuzz-artifacts"
mkdir -p "$art"
"$build/bin/yantra-fuzz-seeds" "$corpus" "$root" > /dev/null
targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
  for f in "$root"/tests/fuzz/target_*.cpp; do
    t=${f##*/target_}
    targets+=("${t%.cpp}")
  done
fi

run_one() {
  local t=$1 log="$art/$1.log"
  if [ -d "$root/tests/fuzz/regressions/$t" ]; then
    cp "$root/tests/fuzz/regressions/$t"/* "$corpus/$t/"
  fi
  if "$build/fuzz/fuzz_$t" -max_total_time="$secs" -timeout=20 -rss_limit_mb=2048 -malloc_limit_mb=1024 \
       -print_final_stats=1 -artifact_prefix="$art/$t-" "$corpus/$t" > "$log" 2>&1; then
    runs=$(sed -n 's/^stat::number_of_executed_units: *//p' "$log")
    echo "$t: no finding in ${secs} s (${runs:-?} runs)"
  else
    echo "$t: FINDING, see $log"
    return 1
  fi
}

status=0
pids=()
for t in "${targets[@]}"; do
  run_one "$t" &
  pids+=($!)
  while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do sleep 1; done
done
for p in "${pids[@]}"; do
  wait "$p" || status=1
done

fuzzdir="$root/receiver/rust/fuzz"
cargo_fuzz=$(command -v cargo-fuzz || echo "$HOME/.cargo/bin/cargo-fuzz")
if [ -x "$cargo_fuzz" ]; then
  export RUSTC_BOOTSTRAP=1 CARGO_BUILD_JOBS="$jobs"
  for t in $(cargo fuzz list --fuzz-dir "$fuzzdir" 2>/dev/null); do
    mkdir -p "$corpus/rust_$t"
    src=$t
    [ -d "$corpus/$src" ] || src=tetra_pdu
    cp "$corpus/$src"/* "$corpus/rust_$t/" 2>/dev/null || true
    if cargo fuzz run --fuzz-dir "$fuzzdir" -s none --target-dir "$build/cargo-fuzz" "$t" "$corpus/rust_$t" \
         -- -max_total_time="$secs" -rss_limit_mb=2048 -artifact_prefix="$art/rust_$t-" \
         > "$art/rust_$t.log" 2>&1; then
      echo "rust $t: no finding in ${secs} s"
    else
      echo "rust $t: FINDING, see $art/rust_$t.log"
      status=1
    fi
  done
else
  echo "rust cargo-fuzz targets: skipped (cargo-fuzz not installed)"
fi
exit $status
