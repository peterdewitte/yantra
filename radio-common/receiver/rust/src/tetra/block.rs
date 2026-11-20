// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! One downlink MAC PDU of a signalling block (SCH/F, SCH/HD, STCH, BNCH)
//! down to the MLE: MAC-RESOURCE -> LLC -> MLE. Fragmented TM-SDUs
//! (MAC-RESOURCE with start of fragmentation, MAC-FRAG, MAC-END) are
//! parsed per PDU here and reassembled by the caller, which keeps the
//! per-timeslot state and hands the whole TM-SDU to [`parse_tm_sdu`].
//! Encrypted TM-SDUs are flagged, never decoded (DEC-10).

use crate::bits::{BitReader, Error, Field, Result};
use crate::tetra::llc::{parse_llc, Llc};
use crate::tetra::mac::{self, FragEnd, Length, MacEnd, MacFrag, MacResource};
use crate::tetra::mle::{parse_mle, MlePdu};

/// What a TM-SDU held: its LLC PDU and the MLE-level PDU in it.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct TmSdu {
    pub llc: Option<Llc>,
    pub mle: Option<MlePdu>,
    /// Why the TM-SDU could not be decoded further (an FCS error is
    /// `Invalid { field: "FCS" }`; the TL-SDU is then not decoded).
    pub error: Option<Error>,
}

/// A MAC-RESOURCE PDU and what its TM-SDU held.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ResourcePdu {
    pub mac: MacResource,
    pub llc: Option<Llc>,
    pub mle: Option<MlePdu>,
    /// Why the TM-SDU could not be decoded further (the MAC header is fine).
    pub sdu_error: Option<Error>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DlPdu {
    Resource(ResourcePdu),
    /// A middle fragment (its TM-SDU bits are for the caller's reassembly).
    Frag(MacFrag),
    /// The last fragment.
    End(MacEnd),
    /// MAC broadcast (2) or MAC-D-BLCK (3): only typed.
    Other { pdu_type: u8 },
}

/// A parsed PDU and where the next one starts in the block, if any.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Parsed {
    pub start: usize,
    pub pdu: DlPdu,
    pub next: Option<usize>,
}

/// Bits of the shortest MAC PDU (the Null PDU).
const MIN_PDU_BITS: usize = 16;

/// Decodes a complete TM-SDU from the reader's position to its end: LLC
/// (with its FCS checked) and the MLE-level PDU.
pub fn parse_tm_sdu(r: &mut BitReader) -> TmSdu {
    let mut out = TmSdu::default();
    match parse_llc(r) {
        Ok(llc) => {
            out.llc = Some(llc);
            if llc.fcs_ok == Some(false) {
                out.error = Some(Error::Invalid { field: "FCS", value: u64::from(llc.fcs.unwrap_or(0)) });
            } else {
                match parse_mle(r) {
                    Ok(x) => out.mle = Some(x),
                    Err(e) => out.error = Some(e),
                }
            }
        }
        Err(e) => out.error = Some(e),
    }
    if let Some(e) = &out.error {
        let text = e.to_string();
        r.note("decoding stopped", || text);
    }
    out
}

/// [`parse_tm_sdu`] over a whole bit buffer (a reassembled TM-SDU).
pub fn parse_tm_sdu_bits(bits: &[u8]) -> TmSdu {
    parse_tm_sdu(&mut BitReader::new(bits))
}

/// Parses the PDU at the reader's position; the block ends at the reader's
/// end.
pub fn parse_dl_pdu(r: &mut BitReader) -> Result<Parsed> {
    let start = r.pos();
    let pdu_type = r.peek("MAC PDU type", 2)? as u8;
    if pdu_type == 1 {
        return Ok(match mac::parse_mac_frag_end(r)? {
            FragEnd::Frag(f) => {
                r.note("TM-SDU", || "fragment, reassembled with the others".into());
                Parsed { start, pdu: DlPdu::Frag(f), next: None }
            }
            FragEnd::End(e) => {
                r.note("TM-SDU", || "last fragment, reassembled with the others".into());
                let next = next_after(r, e.pdu_end);
                Parsed { start, pdu: DlPdu::End(e), next }
            }
        });
    }
    if pdu_type != 0 {
        r.read("MAC PDU type", 2)?;
        r.mean(|| mac::mac_pdu_type_text(pdu_type).to_string());
        r.note("contents", || "not decoded here".into());
        return Ok(Parsed { start, pdu: DlPdu::Other { pdu_type }, next: None });
    }
    let m = mac::parse_mac_resource(r)?;
    let mut pdu = ResourcePdu { mac: m, llc: None, mle: None, sdu_error: None };
    if m.encrypted() {
        r.note("TM-SDU", || "encrypted, not decoded".into());
    } else if m.length == Length::StartOfFragmentation {
        r.note("TM-SDU", || "first fragment, reassembled with the others".into());
    } else if !m.null_pdu() && m.sdu_end > m.sdu_start {
        let block_end = r.end();
        r.set_end(m.sdu_end);
        let sdu = parse_tm_sdu(r);
        (pdu.llc, pdu.mle, pdu.sdu_error) = (sdu.llc, sdu.mle, sdu.error);
        r.seek(m.sdu_end);
        r.set_end(block_end);
    }
    let next = if m.null_pdu() || !matches!(m.length, Length::Octets(_)) { None } else { next_after(r, m.pdu_end) };
    Ok(Parsed { start, pdu: DlPdu::Resource(pdu), next })
}

/// Start of a next PDU at `at`, unless the block ends or only padding follows.
fn next_after(r: &BitReader, at: usize) -> Option<usize> {
    if r.end() < at + MIN_PDU_BITS {
        return None;
    }
    // All zeros: padding after the last PDU (length indication 0 is reserved).
    if r.last_one(at, at + MIN_PDU_BITS).is_none() {
        return None;
    }
    Some(at)
}

/// Every PDU of a block, in order; stops at the first one that fails.
pub fn parse_block(bits: &[u8]) -> (Vec<Parsed>, Option<Error>) {
    let mut out = Vec::new();
    let mut at = 0;
    loop {
        let mut r = BitReader::new(bits);
        r.seek(at);
        match parse_dl_pdu(&mut r) {
            Ok(p) => {
                let next = p.next;
                out.push(p);
                match next {
                    Some(n) => at = n,
                    None => return (out, None),
                }
            }
            Err(e) => return (out, Some(e)),
        }
    }
}

/// What `describe` decodes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DescribeKind {
    /// MAC-SYNC + D-MLE-SYNC (BSCH)
    Sync,
    /// MAC-SYSINFO + D-MLE-SYSINFO (BNCH)
    Sysinfo,
    /// The downlink MAC PDU at a bit offset
    DlPdu,
    /// A whole (reassembled) TM-SDU: LLC and the MLE-level PDU
    TmSdu,
}

/// Field-by-field decode (DEC-04 view): every field read until the end or
/// the error, plus the error if there was one.
pub fn describe(kind: DescribeKind, bits: &[u8], offset: usize) -> (Vec<Field>, Option<Error>) {
    let mut r = BitReader::traced(bits);
    r.seek(offset);
    let err = match kind {
        DescribeKind::Sync => mac::parse_sync(&mut r).err(),
        DescribeKind::Sysinfo => mac::parse_sysinfo(&mut r).err(),
        DescribeKind::DlPdu => parse_dl_pdu(&mut r).err(),
        DescribeKind::TmSdu => {
            r.note("TM-SDU", || "reassembled from its fragments".into());
            parse_tm_sdu(&mut r).error
        }
    };
    (r.into_trace(), err)
}

/// The fields as text lines "offset<TAB>width<TAB>name<TAB>raw<TAB>meaning"
/// (raw "-" when not shown), then "error<TAB>..." if decoding failed.
pub fn describe_text(kind: DescribeKind, bits: &[u8], offset: usize) -> String {
    let (fields, err) = describe(kind, bits, offset);
    let mut s = String::new();
    for f in &fields {
        let raw = f.raw.map_or_else(|| "-".to_string(), |v| v.to_string());
        s.push_str(&format!("{}\t{}\t{}\t{}\t{}\n", f.offset, f.width, f.name, raw, f.meaning));
    }
    if let Some(e) = err {
        s.push_str(&format!("error\t{e}\n"));
    }
    s
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::tetra::mac::tests::resource_header;
    use crate::tetra::mle::tests::{full_neighbour, put_nwrk};
    use crate::tetra::mle::NwrkBroadcast;
    use crate::testbits::W;

    /// D-NWRK-BROADCAST in BL-UDATA in a broadcast MAC-RESOURCE with fill
    /// bits, then a Null PDU, in a 268-bit SCH/F block (like the generator).
    pub(crate) fn nwrk_block(p: &NwrkBroadcast) -> Vec<u8> {
        let mut sdu = W::default();
        sdu.put(2, 4);
        put_nwrk(&mut sdu, p);
        let pdu_bits = 43 + sdu.bits.len();
        let octets = pdu_bits.div_ceil(8);
        let mut w = W::default();
        resource_header(&mut w, octets * 8 > pdu_bits, 0, octets as u8, 1, 0xFFFFFF);
        w.bits.extend_from_slice(&sdu.bits);
        if octets * 8 > pdu_bits {
            w.put(1, 1).put(0, octets * 8 - pdu_bits - 1);
        }
        if 268 - octets * 8 >= 16 {
            resource_header(&mut w, false, 0, 2, 0, 0);
        }
        w.bits.resize(268, 0);
        w.bits
    }

    #[test]
    fn nwrk_broadcast_block_then_null_pdu() {
        let p = NwrkBroadcast { neighbours: Some(vec![full_neighbour(3)]), ..Default::default() };
        let b = nwrk_block(&p);
        let (pdus, err) = parse_block(&b);
        assert_eq!(err, None);
        assert_eq!(pdus.len(), 2);
        let DlPdu::Resource(first) = &pdus[0].pdu else { panic!() };
        assert!(first.mac.address.broadcast);
        assert_eq!(first.llc.unwrap().pdu_type, 2);
        assert_eq!(first.mle, Some(MlePdu::NwrkBroadcast(p)));
        assert_eq!(first.sdu_error, None);
        let DlPdu::Resource(null) = &pdus[1].pdu else { panic!() };
        assert!(null.mac.null_pdu());
        assert_eq!(pdus[1].next, None);
    }

    #[test]
    fn encrypted_and_fragmented_sdus_are_not_decoded() {
        let mut w = W::default();
        resource_header(&mut w, false, 2, 10, 1, 0xFFFFFF);
        w.put(0b0010_101_010, 10);
        w.bits.resize(124, 0);
        let (p, _) = parse_block(&w.bits);
        let DlPdu::Resource(x) = &p[0].pdu else { panic!() };
        assert!(x.mac.encrypted());
        assert_eq!((x.llc, &x.mle), (None, &None));

        let mut w = W::default();
        resource_header(&mut w, false, 0, 63, 1, 0xFFFFFF);
        w.put(0b0010_101_010, 10);
        w.bits.resize(124, 0);
        let (p, err) = parse_block(&w.bits);
        assert_eq!((p.len(), err), (1, None));
        let DlPdu::Resource(x) = &p[0].pdu else { panic!() };
        assert_eq!(x.mac.length, Length::StartOfFragmentation);
        assert_eq!(x.mle, None);
    }

    #[test]
    fn sdu_errors_keep_the_mac_header_and_other_types_end_the_block() {
        // BL-UDATA with a truncated D-NWRK-BROADCAST.
        let mut w = W::default();
        resource_header(&mut w, false, 0, 7, 1, 0xFFFFFF);
        w.put(2, 4).put(5, 3).put(2, 3).put(0, 3);
        w.bits.resize(124, 0);
        let (p, err) = parse_block(&w.bits);
        assert_eq!(err, None);
        let DlPdu::Resource(x) = &p[0].pdu else { panic!() };
        assert!(matches!(x.sdu_error, Some(Error::Truncated { .. })));
        assert_eq!(p.len(), 1, "zeros after the PDU are padding");

        let mut w = W::default();
        w.put(2, 2).put(0, 120);
        let (p, err) = parse_block(&w.bits);
        assert_eq!(err, None);
        assert_eq!(p[0].pdu, DlPdu::Other { pdu_type: 2 });

        let (p, err) = parse_block(&[0, 0, 0]);
        assert!(p.is_empty());
        assert!(matches!(err, Some(Error::Truncated { .. })));
    }

    /// The TL-SDU of a D-NWRK-BROADCAST in BL-DATA with FCS.
    fn nwrk_sdu_with_fcs(p: &NwrkBroadcast) -> Vec<u8> {
        let mut tl = W::default();
        put_nwrk(&mut tl, p);
        let mut w = W::default();
        w.put(5, 4).put(1, 1);
        w.bits.extend_from_slice(&tl.bits);
        w.put(u64::from(crate::tetra::llc::fcs32(&tl.bits)), 32);
        w.bits
    }

    #[test]
    fn fragments_reassemble_to_the_tm_sdu() {
        // As many full neighbours as make the TM-SDU need all three blocks.
        let (p, sdu) = (1..=7)
            .map(|k| {
                let p = NwrkBroadcast { neighbours: Some((1..=k).map(full_neighbour).collect()), ..Default::default() };
                let sdu = nwrk_sdu_with_fcs(&p);
                (p, sdu)
            })
            .find(|(_, sdu)| sdu.len() > 81 + 120 + 40)
            .unwrap();
        assert!(sdu.len() < 81 + 120 + 200);
        // Start: MAC-RESOURCE, start of fragmentation, in a 124-bit block.
        let mut b1 = W::default();
        resource_header(&mut b1, false, 0, 63, 1, 0xFFFFFF);
        let n1 = 124 - b1.bits.len();
        b1.bits.extend_from_slice(&sdu[..n1]);
        // MAC-FRAG: 4-bit header, fills its block.
        let mut b2 = W::default();
        b2.put(1, 2).put(0, 1).put(0, 1);
        let n2 = 124 - 4;
        b2.bits.extend_from_slice(&sdu[n1..n1 + n2]);
        // MAC-END with fill bits and a channel allocation, then a Null PDU.
        let rest = &sdu[n1 + n2..];
        let hdr = 2 + 1 + 1 + 1 + 6 + 1 + 1 + 22 + 1 + 2;
        let octets = (hdr + rest.len() + 1).div_ceil(8);
        let mut b3 = W::default();
        b3.put(1, 2).put(1, 1).put(1, 1).put(0, 1).put(octets as u64, 6).put(0, 1).put(1, 1);
        b3.put(2, 2).put(0b0110, 4).put(3, 2).put(0, 1).put(1, 1).put(3612, 12).put(0, 1).put(1, 2);
        b3.bits.extend_from_slice(rest);
        b3.put(1, 1);
        b3.bits.resize(octets * 8, 0);
        resource_header(&mut b3, false, 0, 2, 0, 0);
        b3.bits.resize(268, 0);

        let (a, err) = parse_block(&b1.bits);
        assert_eq!((a.len(), err), (1, None));
        let DlPdu::Resource(r) = &a[0].pdu else { panic!() };
        assert_eq!(r.mac.length, Length::StartOfFragmentation);
        let mut all = b1.bits[r.mac.sdu_start..r.mac.sdu_end].to_vec();
        let (f, _) = parse_block(&b2.bits);
        let DlPdu::Frag(x) = &f[0].pdu else { panic!() };
        all.extend_from_slice(&b2.bits[x.sdu_start..x.sdu_end]);
        let (e, err) = parse_block(&b3.bits);
        assert_eq!((e.len(), err), (2, None));
        let DlPdu::End(x) = &e[0].pdu else { panic!() };
        let ca = x.channel_allocation.unwrap();
        assert_eq!((ca.allocation_type, ca.timeslots, ca.up_downlink, ca.cell_change, ca.carrier), (2, 6, 3, true, 3612));
        assert_eq!(crate::tetra::mac::timeslots_text(ca.timeslots), "2,3");
        all.extend_from_slice(&b3.bits[x.sdu_start..x.sdu_end]);
        assert_eq!(all, sdu);
        let t = parse_tm_sdu_bits(&all);
        assert_eq!(t.error, None);
        assert_eq!(t.llc.unwrap().fcs_ok, Some(true));
        assert_eq!(t.mle, Some(MlePdu::NwrkBroadcast(p)));
        let text = describe_text(DescribeKind::TmSdu, &all, 0);
        assert!(text.contains("frame check sequence, correct"), "{text}");
        // One bit wrong: an FCS error, the TL-SDU is not decoded.
        all[40] ^= 1;
        let t = parse_tm_sdu_bits(&all);
        assert_eq!(t.llc.unwrap().fcs_ok, Some(false));
        assert!(matches!(t.error, Some(Error::Invalid { field: "FCS", .. })));
        assert_eq!(t.mle, None);
    }

    #[test]
    fn mac_end_errors() {
        let mut w = W::default();
        w.put(1, 2).put(1, 1).put(0, 1).put(0, 1).put(40, 6);
        w.bits.resize(124, 0);
        assert!(matches!(parse_block(&w.bits).1, Some(Error::Invalid { field: "length indication", .. })));
        let mut w = W::default();
        w.put(1, 2).put(1, 1).put(0, 1).put(0, 1).put(20, 6);
        w.bits.resize(124, 0);
        assert!(matches!(parse_block(&w.bits).1, Some(Error::Truncated { .. })));
        let mut w = W::default();
        w.put(1, 2).put(0, 1).put(1, 1);
        w.bits.resize(124, 0);
        assert!(matches!(parse_block(&w.bits).1, Some(Error::Invalid { field: "fill bits", .. })));
    }

    #[test]
    fn describe_lists_fields_and_errors() {
        let p = NwrkBroadcast { neighbours: Some(vec![full_neighbour(3)]), ..Default::default() };
        let b = nwrk_block(&p);
        let t = describe_text(DescribeKind::DlPdu, &b, 0);
        assert!(t.contains("\taddress\t-\tbroadcast (all ones)\n"), "{t}");
        assert!(t.contains("\tMLE PDU type\t2\tD-NWRK-BROADCAST\n"), "{t}");
        assert!(t.contains("neighbour 1: cell identifier CA\t3\t"), "{t}");
        assert!(!t.contains("error"));
        let t = describe_text(DescribeKind::Sysinfo, &b[..20], 0);
        assert!(t.ends_with("error\tinvalid MAC PDU type: 0\n"), "{t}");
        let t = describe_text(DescribeKind::Sync, &[1; 10], 0);
        assert!(t.contains("error\ttruncated"), "{t}");
    }
}
