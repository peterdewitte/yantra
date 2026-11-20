// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! A whole TM-SDU (LLC with FCS, then MLE/MM), the input's bits packed
//! MSB first.
#![no_main]

libfuzzer_sys::fuzz_target!(|data: &[u8]| {
    yantra_proto::fuzz::tm_sdu(data);
});
