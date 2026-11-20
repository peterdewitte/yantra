// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Prints the Rust layout report of the protocol crate's C ABI for
//! tools/ci/lint.sh's FFI check (RAD-90).

fn main() {
    print!("{}", yantra_proto::ffi::layout_report());
}
