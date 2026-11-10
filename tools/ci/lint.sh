#!/bin/sh
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint job of ci.yml (RAD-134, Yantra TST-02), also run by ctest (repo_lint):
#   - shell scripts: sh -n / bash -n (by shebang)
#   - Python: py_compile (into a temporary folder, the tree stays clean)
#   - YAML (CI workflows): parsed with Python's yaml module
#   - Rust protocol crate (receiver/rust, RAD-101): cargo-deny licence,
#     ban and source check (RAD-04, NFR-01; offline: the crate has no
#     external crates, so there is no advisory to look up)
#   - FFI widths (RAD-90): every field and parameter of the crate's C ABI
#     has a fixed-width type on both sides (check_ffi_widths.py), and
#     size, alignment and field offsets of every struct and the values of
#     the constants of receiver/rust/include/yantra/proto.h as the C
#     compiler sees them (proto_layout.c) equal those of the Rust side
#     (receiver/rust/examples/print_layout.rs)
# reuse lint runs as its own ctest and CI step.
# Usage: tools/ci/lint.sh [source-dir]   (default: the repository root)
# Environment: YANTRA_LINT_REQUIRE_TOOLS=1 makes a missing tool (Python
# yaml, cargo-deny, cargo, a C compiler) an error (CI);
# YANTRA_LINT_CARGO picks cargo (default: the one on PATH);
# YANTRA_LINT_CARGO_TARGET_DIR reuses a cargo build folder (default: a
# temporary one); CC picks the C compiler.
# Copied from yantra 68ea20a: ci/lint.sh, adapted for RAD-134.
set -eu
root=${1:-$(cd "$(dirname "$0")/../.." && pwd)}
cd "$root"
fail=0
require=${YANTRA_LINT_REQUIRE_TOOLS:-0}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

missing() {
    echo "lint: $1 missing, $2 not checked"
    if [ "$require" = 1 ]; then fail=1; fi
}

list() {
    find . \( -path ./.git -o -path './build*' -o -path ./third_party \
              -o -name target -o -path ./out -o -path ./sdk -o -path ./sdk-cache \) -prune -o \
           -type f \( "$@" \) -print | sort
}

for f in $(list -name '*.sh'); do
    case $(head -n 1 "$f") in
        *bash*) bash -n "$f" || { echo "lint: shell syntax error in $f"; fail=1; } ;;
        *)      sh -n "$f"   || { echo "lint: shell syntax error in $f"; fail=1; } ;;
    esac
done

for f in $(list -name '*.py'); do
    python3 -c 'import py_compile, sys; py_compile.compile(sys.argv[1], cfile=sys.argv[2], doraise=True)' \
        "$f" "$tmp/x.pyc" || { echo "lint: Python syntax error in $f"; fail=1; }
done

yaml_files=$(list -name '*.yml' -o -name '*.yaml')
if [ -n "$yaml_files" ]; then
    if python3 -c 'import yaml' 2>/dev/null; then
        for f in $yaml_files; do
            python3 -c 'import sys, yaml; yaml.safe_load(open(sys.argv[1], encoding="utf-8"))' "$f" \
                || { echo "lint: YAML error in $f"; fail=1; }
        done
    else
        missing "Python yaml module" "YAML files"
    fi
fi

crate=$root/receiver/rust
if [ -f "$crate/Cargo.toml" ]; then
    deny=$(command -v cargo-deny || true)
    [ -z "$deny" ] && [ -x "$HOME/.cargo/bin/cargo-deny" ] && deny=$HOME/.cargo/bin/cargo-deny
    if [ -n "$deny" ]; then
        "$deny" --manifest-path "$crate/Cargo.toml" --offline check licenses bans sources \
            || { echo "lint: cargo-deny failed"; fail=1; }
    else
        missing cargo-deny "Rust crate licences"
    fi

    python3 "$root/tools/ci/check_ffi_widths.py" "$root" || fail=1

    cc_bin=${CC:-cc}
    cargo_bin=${YANTRA_LINT_CARGO:-$(command -v cargo || true)}
    [ -z "$cargo_bin" ] && [ -x "$HOME/.cargo/bin/cargo" ] && cargo_bin=$HOME/.cargo/bin/cargo
    if command -v "$cc_bin" >/dev/null 2>&1 && [ -n "$cargo_bin" ]; then
        target_dir=${YANTRA_LINT_CARGO_TARGET_DIR:-$tmp/cargo}
        if "$cc_bin" -std=c11 -I "$crate/include" "$root/tools/ci/proto_layout.c" -o "$tmp/proto_layout" &&
           "$tmp/proto_layout" > "$tmp/layout_c.txt" &&
           "$cargo_bin" run --quiet --offline --locked --manifest-path "$crate/Cargo.toml" \
               --target-dir "$target_dir" --example print_layout > "$tmp/layout_rust.txt"; then
            if ! diff -u "$tmp/layout_c.txt" "$tmp/layout_rust.txt"; then
                echo "lint: FFI struct layout differs between proto.h (C) and ffi.rs (Rust)"
                fail=1
            fi
        else
            echo "lint: FFI layout check could not build or run"
            fail=1
        fi
    else
        missing "C compiler or cargo" "FFI struct layout"
    fi
fi

[ $fail -eq 0 ] && echo "lint: OK"
exit $fail
