// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Golden digests of every protocol function (RAD-120): a seeded run over
//! many generated inputs per function - valid PDUs with random field
//! values, random optional elements and random truncation, plus random
//! bits - with every decoded output (or error) hashed into one digest per
//! function. The digests are committed in `tests/golden/digests.txt`; a
//! change to any decoded output fails this test.
//!
//! Updating the digests on purpose: `YANTRA_UPDATE_GOLDEN=1 cargo test
//! --offline --manifest-path <abs>/receiver/rust/Cargo.toml --test
//! golden_digest`, and give the reason in the commit message.

use std::collections::BTreeMap;
use std::fmt::Debug;
use std::path::PathBuf;

use yantra_proto::bits::BitReader;
use yantra_proto::ffi;
use yantra_proto::tetra::{block, llc, mac, mle, values};

const SEED: u64 = 0x5EED_7E7A_2026_0001;
const RUNS: usize = 3000;

/// xorshift64* - a small deterministic generator (no crates).
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    fn below(&mut self, n: u64) -> u64 {
        self.next() % n
    }
    fn bits(&mut self, n: usize) -> Vec<u8> {
        (0..n).map(|_| (self.next() >> 63) as u8).collect()
    }
}

/// FNV-1a, 64 bits (own hash, no crates).
struct Digest(u64);

impl Digest {
    fn new() -> Self {
        Digest(0xcbf2_9ce4_8422_2325)
    }
    fn add(&mut self, bytes: &[u8]) {
        for &b in bytes {
            self.0 ^= u64::from(b);
            self.0 = self.0.wrapping_mul(0x0000_0100_0000_01B3);
        }
        self.0 ^= 0xFF;
        self.0 = self.0.wrapping_mul(0x0000_0100_0000_01B3);
    }
    fn add_debug<T: Debug>(&mut self, v: &T) {
        self.add(format!("{v:?}").as_bytes());
    }
}

fn put(bits: &mut Vec<u8>, v: u64, n: usize) {
    for i in (0..n).rev() {
        bits.push(if i < 64 { ((v >> i) & 1) as u8 } else { 0 });
    }
}

/// Cuts `bits` short in about a quarter of the runs.
fn maybe_truncate(rng: &mut Rng, mut bits: Vec<u8>) -> Vec<u8> {
    if rng.below(4) == 0 && !bits.is_empty() {
        let n = rng.below(bits.len() as u64) as usize;
        bits.truncate(n);
    }
    bits
}

/// A random D-NWRK-BROADCAST TL-SDU: BL-UDATA, MLE pdisc and type, random
/// element values, random optional elements (network time, 0..3
/// neighbours each with random optional elements); sometimes followed by
/// random bits.
fn nwrk_sdu(rng: &mut Rng) -> Vec<u8> {
    let mut b = Vec::new();
    put(&mut b, 2, 4);
    put(&mut b, 5, 3);
    put(&mut b, 2, 3);
    put(&mut b, rng.next(), 18);
    let o = rng.below(5) != 0;
    put(&mut b, o.into(), 1);
    if o {
        let time = rng.below(3) == 0;
        put(&mut b, time.into(), 1);
        if time {
            put(&mut b, rng.next(), 48);
        }
        let count = rng.below(5) != 0;
        put(&mut b, count.into(), 1);
        if count {
            let n = rng.below(4);
            put(&mut b, n, 3);
            for _ in 0..n {
                put(&mut b, rng.next(), 22);
                let o = rng.below(2) == 0;
                put(&mut b, o.into(), 1);
                if o {
                    for width in [10, 10, 14, 14, 3, 4, 16, 12, 5, 6] {
                        let p = rng.below(4) == 0;
                        put(&mut b, p.into(), 1);
                        if p {
                            put(&mut b, rng.next(), width);
                        }
                    }
                }
            }
        }
    }
    if rng.below(4) == 0 {
        let n = rng.below(40) as usize;
        b.extend(rng.bits(n));
    }
    b
}

/// A random downlink signalling block: mostly a MAC-RESOURCE carrying a
/// D-NWRK-BROADCAST or another MLE/MM PDU, with a fitting length and fill
/// bits, sometimes odd lengths, addresses, encryption or random bits.
fn dl_block(rng: &mut Rng) -> Vec<u8> {
    let len = if rng.below(3) != 0 { 268 } else { 124 };
    if rng.below(8) == 0 {
        return rng.bits(len);
    }
    let at = if rng.below(6) == 0 { rng.below(8) } else { 1 };
    let mut hdr_rest = Vec::new();
    put(&mut hdr_rest, at, 3);
    if at != 0 {
        let n = [0usize, 24, 10, 24, 24, 34, 30, 34][at as usize];
        let addr = if rng.below(4) == 0 { rng.next() } else { u64::MAX };
        put(&mut hdr_rest, addr, n);
        put(&mut hdr_rest, if rng.below(10) == 0 { rng.below(8) } else { 0 }, 3);
    }
    let sdu = match rng.below(5) {
        0..=2 => nwrk_sdu(rng),
        3 => {
            let mut x = Vec::new();
            put(&mut x, rng.below(16), 4);
            put(&mut x, rng.below(8), 3);
            put(&mut x, rng.next(), 20);
            x
        }
        _ => {
            let n = rng.below(100) as usize;
            rng.bits(n)
        }
    };
    let pdu_bits = 13 + hdr_rest.len() + sdu.len();
    let fitting = pdu_bits.div_ceil(8) as u64;
    let li = match rng.below(12) {
        0 => 63,
        1 => 62,
        2 => rng.below(64),
        _ => fitting,
    };
    let fill = li * 8 > pdu_bits as u64 && li <= 34;
    let mut b = Vec::new();
    put(&mut b, 0, 2);
    put(&mut b, fill.into(), 1);
    put(&mut b, 0, 1);
    put(&mut b, if rng.below(10) == 0 { 1 + rng.below(3) } else { 0 }, 2);
    put(&mut b, 0, 1);
    put(&mut b, li, 6);
    b.extend(hdr_rest);
    b.extend(sdu);
    if fill {
        put(&mut b, 1, 1);
        let n = (li as usize * 8).saturating_sub(b.len());
        put(&mut b, 0, n);
    }
    if b.len() + 16 <= len && rng.below(3) != 0 {
        // A Null PDU follows.
        put(&mut b, 0b0000_0000_1000, 13);
        put(&mut b, 0, 3);
    }
    let rest = len.saturating_sub(b.len());
    if rng.below(2) == 0 {
        put(&mut b, 0, rest);
    } else {
        b.extend(rng.bits(rest));
    }
    b.truncate(len);
    b
}

fn sync_input(rng: &mut Rng) -> Vec<u8> {
    let mut b = rng.bits(60);
    if rng.below(3) != 0 {
        // Valid frame and multiframe numbers.
        let mut f = Vec::new();
        put(&mut f, 1 + rng.below(18), 5);
        put(&mut f, 1 + rng.below(60), 6);
        b[12..23].copy_from_slice(&f);
    }
    maybe_truncate(rng, b)
}

fn sysinfo_input(rng: &mut Rng) -> Vec<u8> {
    let mut b = rng.bits(124);
    if rng.below(5) != 0 {
        b[0..4].copy_from_slice(&[1, 0, 0, 0]);
    }
    maybe_truncate(rng, b)
}

fn run(name: &str) -> u64 {
    let mut rng = Rng(SEED ^ name.bytes().fold(0u64, |h, c| h.rotate_left(5) ^ u64::from(c)));
    let mut d = Digest::new();
    for _ in 0..RUNS {
        match name {
            "bits.read" => {
                let n = rng.below(80) as usize;
                let b = rng.bits(n);
                let mut r = BitReader::new(&b);
                for _ in 0..6 {
                    let w = rng.below(65) as usize;
                    d.add_debug(&r.read("f", w));
                }
            }
            "values" => {
                let (band, carrier, off, dup) =
                    (rng.below(16) as u8, rng.below(4096) as u16, rng.below(4) as u8, rng.below(8) as u8);
                let rev = rng.below(2) == 1;
                let x = rng.below(16) as u8;
                let class = rng.next() as u16;
                d.add_debug(&(
                    values::downlink_hz(band, carrier, off),
                    values::uplink_hz(band, carrier, off, dup, rev),
                    values::duplex_spacing_hz(band, dup),
                    values::ms_txpwr_dbm(x),
                    values::rxlev_access_min_dbm(x),
                    values::access_parameter_dbm(x),
                    values::reselect_db(x),
                    values::downlink_timeout_text(x),
                ));
                d.add_debug(&(values::subscriber_classes_text(class), values::BsServiceDetails::new(class).text()));
                d.add_debug(&values::date_of(2000 + rng.below(63) as u32, rng.below(400) as u32));
            }
            "mac.parse_sync" => {
                let b = sync_input(&mut rng);
                d.add_debug(&mac::parse_sync(&mut BitReader::new(&b)));
            }
            "mac.parse_sysinfo" => {
                let b = sysinfo_input(&mut rng);
                let r = mac::parse_sysinfo(&mut BitReader::new(&b));
                if let Ok(s) = &r {
                    d.add_debug(&(s.downlink_hz(), s.uplink_hz(), s.security_class()));
                    d.add_debug(&ffi::sysinfo_to_c(s));
                }
                d.add_debug(&r);
            }
            "mac.parse_mac_resource" => {
                let b = dl_block(&mut rng);
                let b = maybe_truncate(&mut rng, b);
                d.add_debug(&mac::parse_mac_resource(&mut BitReader::new(&b)));
            }
            "llc.parse_llc" => {
                let n = rng.below(80) as usize;
                let b = rng.bits(n);
                let mut r = BitReader::new(&b);
                d.add_debug(&llc::parse_llc(&mut r));
                d.add_debug(&(r.pos(), r.end()));
            }
            "mle.parse_mle" => {
                let b = if rng.below(4) == 0 {
                    let n = rng.below(64) as usize;
                    rng.bits(n)
                } else {
                    nwrk_sdu(&mut rng)[4..].to_vec()
                };
                let b = maybe_truncate(&mut rng, b);
                let r = mle::parse_mle(&mut BitReader::new(&b));
                if let Ok(mle::MlePdu::NwrkBroadcast(x)) = &r {
                    d.add_debug(&ffi::nwrk_to_c(x));
                    if let Some(t) = x.network_time {
                        d.add(t.text().as_bytes());
                    }
                }
                d.add_debug(&r);
            }
            "mle.parse_neighbour" => {
                let b = rng.bits(150);
                let b = maybe_truncate(&mut rng, b);
                let r = mle::parse_neighbour(&mut BitReader::new(&b));
                if let Ok(n) = &r {
                    d.add_debug(&(n.downlink_hz(3, 0), ffi::neighbour_to_c(n)));
                }
                d.add_debug(&r);
            }
            "block.parse_block" => {
                let b = dl_block(&mut rng);
                let (pdus, err) = block::parse_block(&b);
                for p in &pdus {
                    d.add_debug(&ffi::dl_pdu_to_c(p));
                }
                d.add_debug(&(pdus, err));
            }
            "block.describe" => {
                let (kind, b) = match rng.below(3) {
                    0 => (block::DescribeKind::Sync, sync_input(&mut rng)),
                    1 => (block::DescribeKind::Sysinfo, sysinfo_input(&mut rng)),
                    _ => (block::DescribeKind::DlPdu, dl_block(&mut rng)),
                };
                d.add(block::describe_text(kind, &b, 0).as_bytes());
            }
            "llc.fcs32" => {
                let n = rng.below(300) as usize;
                d.add_debug(&llc::fcs32(&rng.bits(n)));
            }
            "mac.parse_mac_frag_end" => {
                let mut b = Vec::new();
                put(&mut b, 1, 2);
                put(&mut b, rng.below(2), 1);
                put(&mut b, rng.below(2), 1);
                put(&mut b, rng.below(2), 1);
                put(&mut b, 2 + rng.below(40), 6);
                let len = [124usize, 268][rng.below(2) as usize];
                b.extend(rng.bits(len - b.len()));
                let b = maybe_truncate(&mut rng, b);
                d.add_debug(&mac::parse_mac_frag_end(&mut BitReader::new(&b)));
            }
            "mle.parse_mm" => {
                let mut b = Vec::new();
                put(&mut b, [5u64, 7, 12, rng.below(16)][rng.below(4) as usize], 4);
                let n = rng.below(160) as usize;
                b.extend(rng.bits(n));
                d.add_debug(&mle::parse_mm(&mut BitReader::new(&b)));
            }
            "block.parse_tm_sdu" => {
                let mut b = Vec::new();
                let llc_type = rng.below(8);
                put(&mut b, llc_type, 4);
                if llc_type & 3 == 0 || llc_type & 3 == 3 {
                    put(&mut b, rng.below(2), 1);
                }
                if llc_type & 3 <= 1 {
                    put(&mut b, rng.below(2), 1);
                }
                let sdu = match rng.below(3) {
                    0 => nwrk_sdu(&mut rng),
                    1 => {
                        let mut m = Vec::new();
                        put(&mut m, 1, 3);
                        put(&mut m, [5u64, 7, 12][rng.below(3) as usize], 4);
                        let n = rng.below(120) as usize;
                        m.extend(rng.bits(n));
                        m
                    }
                    _ => {
                        let n = rng.below(200) as usize;
                        rng.bits(n)
                    }
                };
                if llc_type >= 4 && rng.below(4) != 0 {
                    let fcs = llc::fcs32(&sdu);
                    b.extend(&sdu);
                    put(&mut b, u64::from(fcs), 32);
                } else {
                    b.extend(&sdu);
                    if llc_type >= 4 {
                        b.extend(rng.bits(32));
                    }
                }
                let b = maybe_truncate(&mut rng, b);
                d.add_debug(&block::parse_tm_sdu_bits(&b));
                d.add(block::describe_text(block::DescribeKind::TmSdu, &b, 0).as_bytes());
            }
            _ => unreachable!("{name}"),
        }
    }
    d.0
}

const FUNCTIONS: [&str; 14] = [
    "bits.read",
    "values",
    "mac.parse_sync",
    "mac.parse_sysinfo",
    "mac.parse_mac_resource",
    "mac.parse_mac_frag_end",
    "llc.parse_llc",
    "llc.fcs32",
    "mle.parse_mle",
    "mle.parse_mm",
    "mle.parse_neighbour",
    "block.parse_block",
    "block.parse_tm_sdu",
    "block.describe",
];

fn digest_file() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/golden/digests.txt")
}

const HEADER: &str = "# SPDX-FileCopyrightText: 2026 The Yantra contributors\n\
# SPDX-License-Identifier: GPL-3.0-or-later\n\
#\n\
# Golden digests of the protocol functions (RAD-120), written by\n\
# tests/golden_digest.rs with YANTRA_UPDATE_GOLDEN=1. Change only on\n\
# purpose, with the reason in the commit message.\n";

#[test]
fn golden_digests_match() {
    let actual: BTreeMap<&str, String> = FUNCTIONS.iter().map(|f| (*f, format!("{:016x}", run(f)))).collect();
    if std::env::var("YANTRA_UPDATE_GOLDEN").is_ok_and(|v| v == "1") {
        let mut text = HEADER.to_string();
        for f in FUNCTIONS {
            text.push_str(&format!("{f} {}\n", actual[f]));
        }
        std::fs::create_dir_all(digest_file().parent().unwrap()).unwrap();
        std::fs::write(digest_file(), text).unwrap();
        return;
    }
    let text = std::fs::read_to_string(digest_file()).expect("tests/golden/digests.txt (YANTRA_UPDATE_GOLDEN=1 writes it)");
    let expected: BTreeMap<&str, &str> = text
        .lines()
        .filter(|l| !l.starts_with('#') && !l.trim().is_empty())
        .filter_map(|l| l.split_once(' '))
        .collect();
    let mut bad = Vec::new();
    for f in FUNCTIONS {
        match expected.get(f) {
            Some(e) if *e == actual[f] => {}
            Some(e) => bad.push(format!("{f}: expected {e}, got {}", actual[f])),
            None => bad.push(format!("{f}: no committed digest (got {})", actual[f])),
        }
    }
    assert!(bad.is_empty(), "decoded output changed (RAD-120):\n{}", bad.join("\n"));
    assert_eq!(expected.len(), FUNCTIONS.len(), "digest file lists unknown functions");
}

#[test]
fn digests_are_deterministic_and_sensitive() {
    assert_eq!(run("mle.parse_mle"), run("mle.parse_mle"));
    let mut a = Digest::new();
    a.add(b"ab");
    let mut b = Digest::new();
    b.add(b"a");
    b.add(b"b");
    assert_ne!(a.0, b.0, "item boundaries count");
}

#[test]
fn generated_inputs_reach_the_interesting_paths() {
    // The generators must produce decodable D-NWRK-BROADCASTs with
    // neighbours and optional elements, or the digest says little.
    let mut rng = Rng(SEED);
    let (mut nwrk, mut with_neighbours, mut with_la) = (0, 0, 0);
    for _ in 0..RUNS {
        let b = dl_block(&mut rng);
        for p in block::parse_block(&b).0 {
            if let block::DlPdu::Resource(r) = p.pdu {
                if let Some(mle::MlePdu::NwrkBroadcast(x)) = r.mle {
                    nwrk += 1;
                    if let Some(list) = x.neighbours {
                        with_neighbours += usize::from(!list.is_empty());
                        with_la += list.iter().filter(|n| n.location_area.is_some()).count();
                    }
                }
            }
        }
    }
    assert!(nwrk > 500, "{nwrk}");
    assert!(with_neighbours > 200, "{with_neighbours}");
    assert!(with_la > 30, "{with_la}");
}
