// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! LLC PDUs (EN 300 392-2 clause 21.2) of the basic link around an MLE
//! PDU. The advanced link, supplementary and layer-2 signalling PDUs are
//! named but not opened (no broadcast information travels in them).

use crate::bits::{BitReader, Error, Result};

/// LLC PDU type (4 bits).
pub fn llc_type_text(t: u8) -> &'static str {
    match t & 15 {
        0 => "BL-ADATA",
        1 => "BL-DATA",
        2 => "BL-UDATA",
        3 => "BL-ACK",
        4 => "BL-ADATA with FCS",
        5 => "BL-DATA with FCS",
        6 => "BL-UDATA with FCS",
        7 => "BL-ACK with FCS",
        8 => "AL-SETUP",
        9 => "AL-DATA/AL-FINAL",
        10 => "AL-UDATA/AL-UFINAL",
        11 => "AL-ACK/AL-RNR",
        12 => "AL-RECONNECT",
        13 => "supplementary LLC PDU",
        14 => "layer 2 signalling PDU",
        _ => "AL-DISC",
    }
}

/// A basic-link LLC PDU and where its TL-SDU is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Llc {
    pub pdu_type: u8,
    pub ns: Option<u8>,
    pub nr: Option<u8>,
    /// The frame check sequence when sent.
    pub fcs: Option<u32>,
    /// Whether the FCS matches the TL-SDU (when one was sent).
    pub fcs_ok: Option<bool>,
    pub sdu_start: usize,
    pub sdu_end: usize,
}

/// The LLC frame check sequence (EN 300 392-2 clause 22): CRC-32
/// with the generator x^32+x^26+x^23+x^22+x^16+x^12+x^11+x^10+x^8+x^7+
/// x^5+x^4+x^2+x+1 over the TL-SDU bits, first bit first; the first 32
/// bits (all of them when the TL-SDU is shorter) are inverted, and so is
/// the result.
pub fn fcs32(bits: &[u8]) -> u32 {
    let n = bits.len();
    let mut crc: u32 = if n >= 32 { 0xFFFF_FFFF } else if n == 0 { 0 } else { 0xFFFF_FFFFu32 << (32 - n) };
    for &b in bits {
        let bit = u32::from(b != 0) ^ (crc >> 31);
        crc <<= 1;
        if bit != 0 {
            crc ^= 0x04C1_1DB7;
        }
    }
    !crc
}

/// Parses the LLC header from the reader's position to its end; the
/// reader is left at the TL-SDU, its end set before the FCS.
pub fn parse_llc(r: &mut BitReader) -> Result<Llc> {
    let pdu_type = r.read_u8("LLC PDU type", 4)?;
    r.mean(|| llc_type_text(pdu_type).to_string());
    if pdu_type >= 8 {
        return Err(Error::Unsupported { what: "advanced link, supplementary or layer 2 signalling LLC PDU" });
    }
    let base = pdu_type & 3;
    let mut nr = None;
    let mut ns = None;
    if base == 0 || base == 3 {
        nr = Some(r.read_u8("N(R)", 1)?);
    }
    if base == 0 || base == 1 {
        ns = Some(r.read_u8("N(S)", 1)?);
    }
    let mut fcs = None;
    let mut fcs_ok = None;
    if pdu_type >= 4 {
        if r.remaining() < 32 {
            return Err(Error::Truncated { field: "FCS", offset: r.pos(), need: 32, have: r.remaining() });
        }
        let at = r.end() - 32;
        let sdu = r.pos();
        let computed = fcs32(r.slice(sdu, at));
        r.seek(at);
        let sent = r.read_u32("FCS", 32)?;
        r.mean(|| if sent == computed { "frame check sequence, correct".into() } else { format!("frame check sequence error (computed {computed})") });
        fcs = Some(sent);
        fcs_ok = Some(sent == computed);
        r.seek(sdu);
        r.set_end(at);
    }
    Ok(Llc { pdu_type, ns, nr, fcs, fcs_ok, sdu_start: r.pos(), sdu_end: r.end() })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testbits::W;

    #[test]
    fn basic_link_headers() {
        let mut w = W::default();
        w.put(2, 4).put(0b101, 3);
        let mut r = BitReader::new(&w.bits);
        let l = parse_llc(&mut r).unwrap();
        assert_eq!((l.pdu_type, l.ns, l.nr, l.fcs, l.fcs_ok, l.sdu_start, l.sdu_end), (2, None, None, None, None, 4, 7));

        let mut w = W::default();
        w.put(0, 4).put(1, 1).put(0, 1).put(0b11, 2);
        let l = parse_llc(&mut BitReader::new(&w.bits)).unwrap();
        assert_eq!((l.nr, l.ns, l.sdu_start), (Some(1), Some(0), 6));

        let mut w = W::default();
        w.put(1, 4).put(1, 1);
        assert_eq!(parse_llc(&mut BitReader::new(&w.bits)).unwrap().ns, Some(1));

        let mut w = W::default();
        w.put(3, 4).put(0, 1);
        assert_eq!(parse_llc(&mut BitReader::new(&w.bits)).unwrap().nr, Some(0));
    }

    #[test]
    fn fcs32_matches_known_values() {
        // 32 or more bits: the usual CRC-32/BZIP2 (MSB first, init and
        // result inverted) over the bits; "123456789" gives 0xFC891918.
        let mut w = W::default();
        for c in b"123456789" {
            w.put(u64::from(*c), 8);
        }
        assert_eq!(fcs32(&w.bits), 0xFC89_1918);
        // Shorter: only its bits are inverted at the start.
        assert_eq!(fcs32(&[]), 0xFFFF_FFFF);
        let short = [1u8, 0, 1, 1];
        let mut inverted = W::default();
        inverted.put(0b0100, 4);
        let mut zero_init: u32 = 0;
        for &b in &inverted.bits {
            let bit = u32::from(b) ^ (zero_init >> 31);
            zero_init <<= 1;
            if bit != 0 {
                zero_init ^= 0x04C1_1DB7;
            }
        }
        assert_eq!(fcs32(&short), !zero_init);
    }

    #[test]
    fn fcs_is_checked_and_split_off_the_end() {
        let mut w = W::default();
        w.put(6, 4).put(0b1110, 4).put(u64::from(fcs32(&[1, 1, 1, 0])), 32);
        let l = parse_llc(&mut BitReader::new(&w.bits)).unwrap();
        assert_eq!(l.fcs_ok, Some(true));
        let mut w = W::default();
        w.put(6, 4).put(0b1110, 4).put(0xDEADBEEF, 32);
        let mut r = BitReader::new(&w.bits);
        let l = parse_llc(&mut r).unwrap();
        assert_eq!(l.fcs_ok, Some(false));
        assert_eq!(l.fcs, Some(0xDEADBEEF));
        assert_eq!((l.sdu_start, l.sdu_end), (4, 8));
        assert_eq!(r.read("sdu", 4).unwrap(), 0b1110);
        assert!(r.read("more", 1).is_err());
    }

    #[test]
    fn errors() {
        let mut w = W::default();
        w.put(6, 4).put(0, 31);
        assert!(matches!(parse_llc(&mut BitReader::new(&w.bits)), Err(Error::Truncated { field: "FCS", .. })));
        let mut w = W::default();
        w.put(9, 4);
        assert!(matches!(parse_llc(&mut BitReader::new(&w.bits)), Err(Error::Unsupported { .. })));
        assert!(matches!(parse_llc(&mut BitReader::new(&[0, 0])), Err(Error::Truncated { .. })));
        let mut w = W::default();
        w.put(0, 4).put(1, 1);
        assert!(matches!(parse_llc(&mut BitReader::new(&w.bits)), Err(Error::Truncated { field: "N(S)", .. })));
    }
}
