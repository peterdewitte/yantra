// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Every parser of yantra-proto on one input (same format as the C++
//! target tetra_pdu: byte 0 options, then bits packed MSB first).
#![no_main]

libfuzzer_sys::fuzz_target!(|data: &[u8]| {
    yantra_proto::fuzz::tetra_pdu(data);
});
