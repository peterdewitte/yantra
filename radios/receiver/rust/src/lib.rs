// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Protocol crate of yantra-radios (RAD-101, RAD-03): the upper-layer parsing of TETRA
//! broadcast PDUs that osmo-tetra lacks or only partly has (DEC-02,
//! DEC-03): MAC-SYSINFO + D-MLE-SYSINFO with every field, the MAC-RESOURCE
//! -> LLC -> MLE path of the control channels, D-NWRK-BROADCAST with the
//! cell re-select parameters, network time and neighbour cell information,
//! and a field-by-field decode view (names, raw values, meanings) for
//! DEC-04. No external crates; the C++ side calls it through the C ABI in
//! [`ffi`] (header `include/yantra/proto.h`).
//!
//! Scope (RAD-17, Yantra 16.5): broadcast and system information only. Addresses
//! are skipped without being returned or shown, encrypted content is only
//! flagged, and PDUs of other entities (MM, CMCE, SNDCP) are only named.

pub mod bits;
pub mod ffi;
pub mod fuzz;
pub mod tetra;

pub use ffi::ABI_VERSION;

#[cfg(test)]
pub(crate) mod testbits {
    /// MSB-first bit writer for building test PDUs.
    #[derive(Default)]
    pub struct W {
        pub bits: Vec<u8>,
    }

    impl W {
        pub fn put(&mut self, v: u64, n: usize) -> &mut Self {
            for i in (0..n).rev() {
                self.bits.push(if i < 64 { ((v >> i) & 1) as u8 } else { 0 });
            }
            self
        }

        /// A p-bit and, when present, the element.
        pub fn opt(&mut self, v: Option<u64>, n: usize) -> &mut Self {
            self.put(u64::from(v.is_some()), 1);
            if let Some(x) = v {
                self.put(x, n);
            }
            self
        }
    }
}
