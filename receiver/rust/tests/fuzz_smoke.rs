// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Stable-Rust smoke run of the cargo-fuzz entry points (RAD-96): a
//! seeded set of inputs (random bytes and seeded mutations of valid
//! PDUs), the committed crash regressions in tests/fuzz/regressions/
//! tetra_pdu and, when YANTRA_FUZZ_CORPUS names a folder (ctest sets it to
//! the corpus yantra-fuzz-seeds writes from the generator), every file in
//! it, each through yantra_proto::fuzz::tetra_pdu. A panic fails the test.

use std::path::{Path, PathBuf};

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
    fn below(&mut self, n: usize) -> usize {
        if n == 0 { 0 } else { (self.next() % n as u64) as usize }
    }
}

fn files_in(dir: &Path) -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = match std::fs::read_dir(dir) {
        Ok(rd) => rd.filter_map(|e| e.ok().map(|e| e.path())).filter(|p| p.is_file()).collect(),
        Err(_) => Vec::new(),
    };
    v.sort();
    v
}

fn mutate(d: &mut Vec<u8>, r: &mut Rng) {
    for _ in 0..1 + r.below(4) {
        match r.below(4) {
            0 if !d.is_empty() => {
                let i = r.below(d.len());
                d[i] ^= 1 << r.below(8);
            }
            1 if !d.is_empty() => {
                let i = r.below(d.len());
                d[i] = r.next() as u8;
            }
            2 => {
                let n = r.below(d.len() + 1);
                d.truncate(n);
            }
            _ => {
                let i = r.below(d.len() + 1);
                d.insert(i, r.next() as u8);
            }
        }
    }
}

#[test]
fn random_inputs_never_panic() {
    let mut r = Rng(0x5EED_F022_2026_0001);
    for _ in 0..20_000 {
        let n = r.below(80);
        let d: Vec<u8> = (0..n).map(|_| r.next() as u8).collect();
        yantra_proto::fuzz::tetra_pdu(&d);
    }
}

#[test]
fn corpus_and_regressions_never_panic() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/fuzz/regressions/tetra_pdu");
    let mut files = files_in(&root);
    if let Ok(dir) = std::env::var("YANTRA_FUZZ_CORPUS") {
        let corpus = files_in(Path::new(&dir));
        assert!(!corpus.is_empty(), "YANTRA_FUZZ_CORPUS={dir} holds no inputs");
        files.extend(corpus);
    }
    let mut r = Rng(0x5EED_F022_2026_0002);
    for f in &files {
        let d = std::fs::read(f).expect("corpus file");
        yantra_proto::fuzz::tetra_pdu(&d);
        for _ in 0..64 {
            let mut x = d.clone();
            mutate(&mut x, &mut r);
            yantra_proto::fuzz::tetra_pdu(&x);
        }
    }
}
