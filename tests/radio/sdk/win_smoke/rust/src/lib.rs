// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// C ABI of the smoke test's Rust library (tests/sdk, RAD-139): uses the
// standard library's allocator, formatting and threads, so the Windows
// rust-std of the SDK is really linked.

/// Sum of `n` values at `values`, computed on a Rust thread; 0 for a null pointer.
///
/// # Safety
/// `values` points to `n` readable `i32` values (or is null).
#[no_mangle]
pub unsafe extern "C" fn yantra_sdk_smoke_sum(values: *const i32, n: usize) -> i64 {
    if values.is_null() {
        return 0;
    }
    let v: Vec<i64> = std::slice::from_raw_parts(values, n).iter().map(|&x| i64::from(x)).collect();
    std::thread::spawn(move || v.iter().sum()).join().unwrap_or(-1)
}

/// Writes "rust <version of the formatting>: <sum>" into `buf` (NUL-terminated,
/// at most `len` bytes) and returns the length without the NUL.
///
/// # Safety
/// `buf` points to `len` writable bytes (or is null with `len` 0).
#[no_mangle]
pub unsafe extern "C" fn yantra_sdk_smoke_text(sum: i64, buf: *mut u8, len: usize) -> usize {
    let s = format!("rust says {sum}");
    if buf.is_null() || len == 0 {
        return s.len();
    }
    let n = s.len().min(len - 1);
    std::ptr::copy_nonoverlapping(s.as_ptr(), buf, n);
    *buf.add(n) = 0;
    n
}
