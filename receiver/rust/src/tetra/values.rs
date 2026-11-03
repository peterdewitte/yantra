// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Meanings of TETRA broadcast field values (EN 300 392-2 clauses 18.5
//! and 21.5, carrier numbering of TS 100 392-15): frequencies, power
//! levels, thresholds and the BS service details flags.

/// Carrier offset of the 2-bit offset code, Hz.
pub fn offset_hz(code: u8) -> i32 {
    match code & 3 {
        0 => 0,
        1 => 6250,
        2 => -6250,
        _ => 12500,
    }
}

/// Downlink frequency: band * 100 MHz + carrier * 25 kHz + offset.
pub fn downlink_hz(band: u8, carrier: u16, offset: u8) -> u64 {
    let f = i64::from(band & 15) * 100_000_000 + i64::from(carrier & 0xFFF) * 25_000 + i64::from(offset_hz(offset));
    f.max(0) as u64
}

/// Duplex spacing in kHz per duplex spacing code (rows) and frequency band
/// (columns), TS 100 392-15 table 2; `None` = reserved.
const DUPLEX_KHZ: [[Option<u32>; 16]; 8] = {
    const N: Option<u32> = None;
    [
        [N, Some(1600), Some(10000), Some(10000), Some(10000), Some(10000), Some(10000), N, N, N, N, N, N, N, N, N],
        [N, Some(4500), N, Some(36000), Some(7000), N, N, N, Some(45000), Some(45000), N, N, N, N, N, N],
        [Some(0); 16],
        [N, N, N, Some(8000), Some(8000), N, N, N, Some(18000), Some(18000), N, N, N, N, N, N],
        [N, N, N, Some(18000), Some(5000), N, Some(30000), Some(30000), N, Some(39000), N, N, N, N, N, N],
        [N, N, N, N, Some(9500), N, N, N, N, N, N, N, N, N, N, N],
        [N; 16],
        [N; 16],
    ]
};

/// Duplex spacing in Hz, `None` when the code is reserved for the band.
pub fn duplex_spacing_hz(band: u8, duplex: u8) -> Option<u64> {
    DUPLEX_KHZ[usize::from(duplex & 7)][usize::from(band & 15)].map(|k| u64::from(k) * 1000)
}

/// Uplink frequency: downlink minus the duplex spacing, plus it with
/// reverse operation (uplink above downlink).
pub fn uplink_hz(band: u8, carrier: u16, offset: u8, duplex: u8, reverse: bool) -> Option<u64> {
    let dl = downlink_hz(band, carrier, offset);
    let spacing = duplex_spacing_hz(band, duplex)?;
    if reverse {
        Some(dl + spacing)
    } else {
        dl.checked_sub(spacing)
    }
}

/// MS_TXPWR_MAX_CELL / maximum MS transmit power (3 bits): 15 dBm for 1,
/// 5 dB steps up to 45 dBm; 0 is reserved.
pub fn ms_txpwr_dbm(code: u8) -> Option<i32> {
    match code & 7 {
        0 => None,
        n => Some(15 + 5 * (i32::from(n) - 1)),
    }
}

/// RXLEV_ACCESS_MIN / minimum RX access level (4 bits): -125 dBm + 5 dB steps.
pub fn rxlev_access_min_dbm(code: u8) -> i32 {
    -125 + 5 * i32::from(code & 15)
}

/// ACCESS_PARAMETER (4 bits): -53 dBm + 2 dB steps.
pub fn access_parameter_dbm(code: u8) -> i32 {
    -53 + 2 * i32::from(code & 15)
}

/// Cell re-select thresholds and hysteresis (4 bits each): 2 dB steps.
pub fn reselect_db(code: u8) -> u32 {
    2 * u32::from(code & 15)
}

/// RADIO_DOWNLINK_TIMEOUT (4 bits): 0 disables it, else n * 144 timeslots.
pub fn downlink_timeout_text(code: u8) -> String {
    match code & 15 {
        0 => "disabled".to_string(),
        n => format!("{} timeslots", u32::from(n) * 144),
    }
}

pub fn band_text(band: u8) -> String {
    format!("{} MHz band", u32::from(band & 15) * 100)
}

pub fn offset_text(code: u8) -> &'static str {
    match code & 3 {
        0 => "no offset",
        1 => "+6.25 kHz",
        2 => "-6.25 kHz",
        _ => "+12.5 kHz",
    }
}

pub fn duplex_text(band: u8, duplex: u8) -> String {
    match duplex_spacing_hz(band, duplex) {
        Some(hz) => format!("{} kHz", hz / 1000),
        None => "reserved for this band".to_string(),
    }
}

/// Cell load CA (2 bits).
pub fn cell_load_text(code: u8) -> &'static str {
    match code & 3 {
        0 => "unknown",
        1 => "low",
        2 => "medium",
        _ => "high",
    }
}

/// Cell re-selection types supported by a neighbour (2 bits).
pub fn reselection_types_text(code: u8) -> &'static str {
    match code & 3 {
        0 => "cell change service not supported",
        1 => "call restoration not supported",
        2 => "call restoration on the current cell",
        _ => "call restoration on the new cell",
    }
}

/// Population (subscriber) classes allowed by the 16-bit bitmap; the
/// first transmitted bit is class 1. "none", "1-16", "1,3,5-7".
pub fn subscriber_classes_text(bitmap: u16) -> String {
    let set: Vec<u32> = (1..=16).filter(|c| bitmap & (0x8000 >> (c - 1)) != 0).collect();
    if set.is_empty() {
        return "none".to_string();
    }
    let mut parts = Vec::new();
    let mut i = 0;
    while i < set.len() {
        let mut j = i;
        while j + 1 < set.len() && set[j + 1] == set[j] + 1 {
            j += 1;
        }
        parts.push(if j > i { format!("{}-{}", set[i], set[j]) } else { set[i].to_string() });
        i = j + 1;
    }
    parts.join(",")
}

/// BS service details (18.5.2), 12 flags in transmission order.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct BsServiceDetails {
    pub raw: u16,
}

/// Names of the BS service details flags, first transmitted first.
pub const SERVICE_FLAGS: [&str; 12] = [
    "registration",
    "de-registration",
    "priority cell",
    "never minimum mode",
    "migration",
    "system-wide services",
    "voice service",
    "circuit mode data",
    "reserved",
    "SNDCP service",
    "air interface encryption",
    "advanced link",
];

impl BsServiceDetails {
    pub fn new(raw: u16) -> Self {
        BsServiceDetails { raw: raw & 0xFFF }
    }

    /// Flag `i` (0 = first transmitted).
    pub fn flag(&self, i: usize) -> bool {
        i < 12 && self.raw & (0x800 >> i) != 0
    }

    pub fn registration(&self) -> bool {
        self.flag(0)
    }
    pub fn system_wide_services(&self) -> bool {
        self.flag(5)
    }
    pub fn air_encryption(&self) -> bool {
        self.flag(10)
    }

    /// Set flags by name ("none" when no flag is set).
    pub fn text(&self) -> String {
        let set: Vec<&str> = (0..12).filter(|&i| self.flag(i) && i != 8).map(|i| SERVICE_FLAGS[i]).collect();
        if set.is_empty() {
            "none".to_string()
        } else {
            set.join(", ")
        }
    }
}

/// Trunking mode (DEC-03): wide-area when system-wide services are
/// available, otherwise local site trunking.
pub fn trunking_text(details: BsServiceDetails) -> &'static str {
    if details.system_wide_services() {
        "wide-area trunking"
    } else {
        "local site trunking"
    }
}

/// Date of `days` after 1 January of `year` (proleptic Gregorian).
pub fn date_of(year: u32, mut days: u32) -> (u32, u32, u32) {
    let mut y = year;
    loop {
        let len = if leap(y) { 366 } else { 365 };
        if days < len {
            break;
        }
        days -= len;
        y += 1;
    }
    let months = [31, if leap(y) { 29 } else { 28 }, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    let mut m = 0;
    while days >= months[m] {
        days -= months[m];
        m += 1;
    }
    (y, m as u32 + 1, days + 1)
}

fn leap(y: u32) -> bool {
    (y % 4 == 0 && y % 100 != 0) || y % 400 == 0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frequencies() {
        assert_eq!(downlink_hz(3, 3600, 0), 390_000_000);
        assert_eq!(downlink_hz(4, 0, 1), 400_006_250);
        assert_eq!(downlink_hz(3, 3600, 2), 389_993_750);
        assert_eq!(downlink_hz(3, 3600, 3), 390_012_500);
        assert_eq!(duplex_spacing_hz(3, 0), Some(10_000_000));
        assert_eq!(duplex_spacing_hz(0, 0), None);
        assert_eq!(duplex_spacing_hz(9, 2), Some(0));
        assert_eq!(uplink_hz(3, 3600, 0, 0, false), Some(380_000_000));
        assert_eq!(uplink_hz(3, 3600, 0, 0, true), Some(400_000_000));
        assert_eq!(uplink_hz(3, 3600, 0, 6, false), None);
        assert_eq!(uplink_hz(0, 0, 0, 2, false), Some(0));
        assert_eq!(uplink_hz(1, 0, 0, 1, false), Some(95_500_000));
    }

    #[test]
    fn levels() {
        assert_eq!(ms_txpwr_dbm(0), None);
        assert_eq!(ms_txpwr_dbm(1), Some(15));
        assert_eq!(ms_txpwr_dbm(7), Some(45));
        assert_eq!(rxlev_access_min_dbm(0), -125);
        assert_eq!(rxlev_access_min_dbm(15), -50);
        assert_eq!(access_parameter_dbm(0), -53);
        assert_eq!(access_parameter_dbm(15), -23);
        assert_eq!(reselect_db(15), 30);
        assert_eq!(downlink_timeout_text(0), "disabled");
        assert_eq!(downlink_timeout_text(2), "288 timeslots");
    }

    #[test]
    fn classes_and_services() {
        assert_eq!(subscriber_classes_text(0xFFFF), "1-16");
        assert_eq!(subscriber_classes_text(0), "none");
        assert_eq!(subscriber_classes_text(0b1010_1110_0000_0001), "1,3,5-7,16");
        let s = BsServiceDetails::new(0b1100_0100_0010);
        assert!(s.registration());
        assert!(s.system_wide_services());
        assert!(s.air_encryption());
        assert_eq!(s.text(), "registration, de-registration, system-wide services, air interface encryption");
        assert_eq!(trunking_text(s), "wide-area trunking");
        assert_eq!(trunking_text(BsServiceDetails::new(0)), "local site trunking");
        assert_eq!(BsServiceDetails::new(0x008).text(), "none", "the reserved bit is not a service");
    }

    #[test]
    fn dates() {
        assert_eq!(date_of(2026, 0), (2026, 1, 1));
        assert_eq!(date_of(2026, 59), (2026, 3, 1));
        assert_eq!(date_of(2024, 59), (2024, 2, 29));
        assert_eq!(date_of(2026, 365), (2027, 1, 1));
        assert_eq!(date_of(2000, 366), (2001, 1, 1));
    }
}
