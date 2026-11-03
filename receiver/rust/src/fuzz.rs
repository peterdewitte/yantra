// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Fuzz entry points (RAD-96), shared by the cargo-fuzz targets in
//! `fuzz/` and the stable-Rust smoke test `tests/fuzz_smoke.rs`, which
//! replays a corpus through the same functions. Input format as the C++
//! target `tetra_pdu` (tests/fuzz): byte 0 = options, then the bits packed
//! MSB first, so one corpus serves both. A parser must never panic; the
//! checks below panic when an output breaks a property.

use crate::bits::BitReader;
use crate::tetra::{block, llc, mac, mle, values};

/// At most this many input bytes are used (8192 bits).
pub const MAX_INPUT_BYTES: usize = 1025;

/// Bits, one per byte, of bytes packed MSB first.
pub fn unpack(bytes: &[u8]) -> Vec<u8> {
    bytes.iter().flat_map(|b| (0..8).rev().map(move |i| (b >> i) & 1)).collect()
}

/// Runs every parser of the crate on one input.
pub fn tetra_pdu(data: &[u8]) {
    let Some((&opt, rest)) = data.split_first() else { return };
    let bits = unpack(&rest[..rest.len().min(MAX_INPUT_BYTES - 1)]);
    let n = bits.len();
    let mut pad = bits.clone();
    pad.resize(n.max(268), 0);

    if let Ok(s) = mac::parse_sync(&mut BitReader::new(&pad[..60])) {
        let _ = format!("{s:?}");
    }
    if let Ok(s) = mac::parse_sysinfo(&mut BitReader::new(&pad[..124])) {
        let _ = (s.downlink_hz(), s.uplink_hz(), s.security_class());
    }
    for parse in [parse_llc_mle as fn(&[u8]), parse_mm_only, parse_nwrk_only] {
        parse(&bits);
    }
    let block_bits = &bits[..n.min(268)];
    let (pdus, _err) = block::parse_block(block_bits);
    let mut prev: Option<usize> = None;
    for p in &pdus {
        assert!(prev.map_or(true, |q| p.start > q), "PDU starts do not increase");
        assert!(p.start < block_bits.len(), "PDU starts outside the block");
        if let Some(next) = p.next {
            assert!(next > p.start && next <= block_bits.len(), "next PDU outside the block");
        }
        prev = Some(p.start);
    }
    let _ = block::parse_tm_sdu_bits(&bits);
    let offset = if n > 0 { usize::from(opt >> 1) % (n + 1) } else { 0 };
    for kind in [block::DescribeKind::Sync, block::DescribeKind::Sysinfo, block::DescribeKind::DlPdu, block::DescribeKind::TmSdu] {
        let _ = block::describe_text(kind, &bits, offset);
    }
    let band = rest.last().copied().unwrap_or(0);
    let _ = values::downlink_hz(band, u16::from(opt) << 4 | u16::from(band), opt & 3);
    let _ = values::uplink_hz(band & 15, u16::from(band), opt & 3, opt >> 5, opt & 4 != 0);
    let _ = values::subscriber_classes_text(u16::from(opt) << 8 | u16::from(band));
}

fn parse_llc_mle(bits: &[u8]) {
    let mut r = BitReader::new(bits);
    if llc::parse_llc(&mut r).is_ok() {
        let _ = mle::parse_mle(&mut r);
    }
}

fn parse_mm_only(bits: &[u8]) {
    let _ = mle::parse_mm(&mut BitReader::new(bits));
}

fn parse_nwrk_only(bits: &[u8]) {
    if let Ok(b) = mle::parse_nwrk_broadcast(&mut BitReader::new(bits)) {
        assert!(b.neighbours.map_or(0, |v| v.len()) <= 7, "more than 7 neighbours in one D-NWRK-BROADCAST");
    }
}
