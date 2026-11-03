// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Downlink MAC PDUs of the broadcast and control channels (EN 300 392-2
//! clause 21.4) with the MLE parts they carry: MAC-SYNC + D-MLE-SYNC
//! (BSCH), MAC-SYSINFO + D-MLE-SYSINFO (BNCH) and the MAC-RESOURCE header
//! up to its TM-SDU. Addresses are skipped, never returned (spec 16.5).

use crate::bits::{BitReader, Error, Result};
use crate::tetra::values::{self as v, BsServiceDetails};

/// D-MLE-SYNC (18.4.2.1), 29 bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MleSync {
    pub mcc: u16,
    pub mnc: u16,
    /// 2 bits: D-NWRK-BROADCAST broadcast supported, enquiry supported.
    pub neighbour_cell_broadcast: u8,
    pub cell_service_level: u8,
    pub late_entry: bool,
}

/// MAC-SYNC (21.4.4.2) + D-MLE-SYNC: the 60 bits of the BSCH.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MacSync {
    pub system_code: u8,
    pub colour_code: u8,
    /// 1..4
    pub timeslot: u8,
    /// 1..18
    pub frame: u8,
    /// 1..60
    pub multiframe: u8,
    pub sharing_mode: u8,
    pub ts_reserved_frames: u8,
    pub u_plane_dtx: bool,
    pub frame18_extension: bool,
    pub mle: MleSync,
}

/// Bits of the BSCH (type-1).
pub const SYNC_BITS: usize = 60;
/// Bits of the BNCH (type-1).
pub const SYSINFO_BITS: usize = 124;

pub fn parse_mle_sync(r: &mut BitReader) -> Result<MleSync> {
    let mcc = r.read_u16("MCC", 10)?;
    let mnc = r.read_u16("MNC", 14)?;
    let neighbour_cell_broadcast = r.read_u8("neighbour cell broadcast", 2)?;
    r.mean(|| {
        format!(
            "D-NWRK-BROADCAST {}, enquiry {}",
            if neighbour_cell_broadcast & 2 != 0 { "broadcast" } else { "not broadcast" },
            if neighbour_cell_broadcast & 1 != 0 { "supported" } else { "not supported" }
        )
    });
    let cell_service_level = r.read_u8("cell service level", 2)?;
    r.mean(|| {
        match cell_service_level {
            0 => "unknown",
            1 => "low",
            2 => "medium",
            _ => "high",
        }
        .to_string()
    });
    let late_entry = r.flag("late entry supported")?;
    Ok(MleSync { mcc, mnc, neighbour_cell_broadcast, cell_service_level, late_entry })
}

/// Parses MAC-SYNC + D-MLE-SYNC from the start of the reader.
pub fn parse_sync(r: &mut BitReader) -> Result<MacSync> {
    let system_code = r.read_u8("system code", 4)?;
    r.mean(|| match system_code {
        0 => "ETS 300 392-2 ed. 1".to_string(),
        1 => "EN 300 392-2 ed. 2".to_string(),
        2 => "EN 300 392-2 ed. 3".to_string(),
        n => format!("code {n}"),
    });
    let colour_code = r.read_u8("colour code", 6)?;
    let timeslot = r.read_u8("timeslot number", 2)? + 1;
    r.mean(|| format!("TN {timeslot}"));
    let frame = r.read_u8("frame number", 5)?;
    if !(1..=18).contains(&frame) {
        return Err(Error::Invalid { field: "frame number", value: u64::from(frame) });
    }
    let multiframe = r.read_u8("multiframe number", 6)?;
    if !(1..=60).contains(&multiframe) {
        return Err(Error::Invalid { field: "multiframe number", value: u64::from(multiframe) });
    }
    let sharing_mode = r.read_u8("sharing mode", 2)?;
    r.mean(|| {
        ["continuous transmission", "carrier sharing", "MCCH sharing", "traffic carrier sharing"]
            [usize::from(sharing_mode)]
            .to_string()
    });
    let ts_reserved_frames = r.read_u8("TS reserved frames", 3)?;
    let u_plane_dtx = r.flag("U-plane DTX")?;
    let frame18_extension = r.flag("frame 18 extension")?;
    r.read("reserved", 1)?;
    let mle = parse_mle_sync(r)?;
    Ok(MacSync {
        system_code,
        colour_code,
        timeslot,
        frame,
        multiframe,
        sharing_mode,
        ts_reserved_frames,
        u_plane_dtx,
        frame18_extension,
        mle,
    })
}

/// D-MLE-SYSINFO (18.4.2.2), 42 bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct MleSysinfo {
    pub location_area: u16,
    /// Population classes 1..16 allowed, class 1 in the most significant bit.
    pub subscriber_class: u16,
    pub bs_service_details: BsServiceDetails,
}

pub fn parse_mle_sysinfo(r: &mut BitReader) -> Result<MleSysinfo> {
    let location_area = r.read_u16("location area", 14)?;
    r.mean(|| format!("LA {location_area} (0x{location_area:04X})"));
    let subscriber_class = r.read_u16("subscriber class", 16)?;
    r.mean(|| format!("classes {}", v::subscriber_classes_text(subscriber_class)));
    let bs_service_details = parse_service_details(r, "BS service details")?;
    Ok(MleSysinfo { location_area, subscriber_class, bs_service_details })
}

pub fn parse_service_details(r: &mut BitReader, field: &'static str) -> Result<BsServiceDetails> {
    let s = BsServiceDetails::new(r.read_u16(field, 12)?);
    r.mean(|| s.text());
    Ok(s)
}

/// The optional 20-bit field of MAC-SYSINFO (2-bit flag).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SysinfoOption {
    /// TS_COMMON_FRAMES for even multiframes.
    EvenMultiframe(u32),
    /// TS_COMMON_FRAMES for odd multiframes.
    OddMultiframe(u32),
    /// Default definition for access code A.
    AccessCodeA(u32),
    /// Extended services broadcast.
    ExtendedServices(ExtendedServices),
}

impl SysinfoOption {
    pub fn flag(&self) -> u8 {
        match self {
            SysinfoOption::EvenMultiframe(_) => 0,
            SysinfoOption::OddMultiframe(_) => 1,
            SysinfoOption::AccessCodeA(_) => 2,
            SysinfoOption::ExtendedServices(_) => 3,
        }
    }

    pub fn value(&self) -> u32 {
        match *self {
            SysinfoOption::EvenMultiframe(x) | SysinfoOption::OddMultiframe(x) | SysinfoOption::AccessCodeA(x) => x,
            SysinfoOption::ExtendedServices(e) => e.raw,
        }
    }
}

/// Extended services broadcast (21.4.4.1), 20 bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ExtendedServices {
    pub raw: u32,
    /// Authentication required, security class 1, class 2 or 3, then
    /// class-dependent bits: a cell capability flag, not key material.
    pub security_information: u8,
    pub sds_tl_addressing_method: u8,
    pub gck_supported: bool,
    pub section: u8,
    pub section_information: u8,
}

impl ExtendedServices {
    /// Security class 1..3 of a cell with this security information and
    /// the air encryption service flag (DEC-10): 1 without encryption,
    /// else 3 when the "class 2 or 3" bit says 3, else 2.
    pub fn security_class(info: Option<u8>, air_encryption: bool) -> u8 {
        if !air_encryption {
            1
        } else if info.is_some_and(|i| i & 0x20 != 0) {
            3
        } else {
            2
        }
    }
}

/// MAC-SYSINFO (21.4.4.1) + D-MLE-SYSINFO: the 124 bits of the BNCH.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MacSysinfo {
    pub main_carrier: u16,
    pub frequency_band: u8,
    pub offset: u8,
    pub duplex_spacing: u8,
    pub reverse_operation: bool,
    pub common_scch: u8,
    pub ms_txpwr_max_cell: u8,
    pub rxlev_access_min: u8,
    pub access_parameter: u8,
    pub radio_downlink_timeout: u8,
    /// The hyperframe number, when sent (cipher key flag 0).
    pub hyperframe: Option<u16>,
    /// The common cipher key identifier, when sent instead.
    pub cck_id: Option<u16>,
    pub option: SysinfoOption,
    pub mle: MleSysinfo,
}

impl MacSysinfo {
    pub fn downlink_hz(&self) -> u64 {
        v::downlink_hz(self.frequency_band, self.main_carrier, self.offset)
    }
    pub fn uplink_hz(&self) -> Option<u64> {
        v::uplink_hz(self.frequency_band, self.main_carrier, self.offset, self.duplex_spacing, self.reverse_operation)
    }
    pub fn security_information(&self) -> Option<u8> {
        match self.option {
            SysinfoOption::ExtendedServices(e) => Some(e.security_information),
            _ => None,
        }
    }
    pub fn security_class(&self) -> u8 {
        ExtendedServices::security_class(self.security_information(), self.mle.bs_service_details.air_encryption())
    }
}

/// MAC PDU type (2 bits) of a downlink block.
pub fn mac_pdu_type_text(t: u8) -> &'static str {
    match t & 3 {
        0 => "MAC-RESOURCE",
        1 => "MAC-FRAG or MAC-END",
        2 => "broadcast",
        _ => "supplementary (MAC-D-BLCK)",
    }
}

/// Parses MAC-SYSINFO + D-MLE-SYSINFO; `Invalid` when the block is not a
/// broadcast SYSINFO PDU.
pub fn parse_sysinfo(r: &mut BitReader) -> Result<MacSysinfo> {
    let pdu_type = r.read_u8("MAC PDU type", 2)?;
    r.mean(|| mac_pdu_type_text(pdu_type).to_string());
    if pdu_type != 2 {
        return Err(Error::Invalid { field: "MAC PDU type", value: u64::from(pdu_type) });
    }
    let broadcast_type = r.read_u8("broadcast type", 2)?;
    r.mean(|| ["SYSINFO", "ACCESS-DEFINE", "SYSINFO-DA", "reserved"][usize::from(broadcast_type)].to_string());
    if broadcast_type != 0 {
        return Err(Error::Invalid { field: "broadcast type", value: u64::from(broadcast_type) });
    }
    let main_carrier = r.read_u16("main carrier", 12)?;
    let frequency_band = r.read_u8("frequency band", 4)?;
    r.mean(|| v::band_text(frequency_band));
    let offset = r.read_u8("offset", 2)?;
    r.mean(|| v::offset_text(offset).to_string());
    let duplex_spacing = r.read_u8("duplex spacing", 3)?;
    r.mean(|| v::duplex_text(frequency_band, duplex_spacing));
    let reverse_operation = r.flag("reverse operation")?;
    r.mean(|| if reverse_operation { "uplink above downlink" } else { "normal (uplink below downlink)" }.into());
    let common_scch = r.read_u8("number of common secondary control channels", 2)?;
    let ms_txpwr_max_cell = r.read_u8("MS_TXPWR_MAX_CELL", 3)?;
    r.mean(|| ms_txpwr_text(ms_txpwr_max_cell));
    let rxlev_access_min = r.read_u8("RXLEV_ACCESS_MIN", 4)?;
    r.mean(|| format!("{} dBm", v::rxlev_access_min_dbm(rxlev_access_min)));
    let access_parameter = r.read_u8("ACCESS_PARAMETER", 4)?;
    r.mean(|| format!("{} dBm", v::access_parameter_dbm(access_parameter)));
    let radio_downlink_timeout = r.read_u8("RADIO_DOWNLINK_TIMEOUT", 4)?;
    r.mean(|| v::downlink_timeout_text(radio_downlink_timeout));
    let cck_flag = r.flag("cipher key flag")?;
    r.mean(|| if cck_flag { "common cipher key identifier follows" } else { "hyperframe number follows" }.into());
    let (hyperframe, cck_id) = if cck_flag {
        (None, Some(r.read_u16("CCK identifier", 16)?))
    } else {
        (Some(r.read_u16("hyperframe number", 16)?), None)
    };
    let flag = r.read_u8("optional field flag", 2)?;
    r.mean(|| {
        ["TS_COMMON_FRAMES (even multiframes)", "TS_COMMON_FRAMES (odd multiframes)", "default access code A", "extended services"]
            [usize::from(flag)]
            .to_string()
    });
    let option = match flag {
        0 | 1 => {
            let x = r.read_u32("TS_COMMON_FRAMES", 20)?;
            r.mean(|| common_frames_text(x));
            if flag == 0 {
                SysinfoOption::EvenMultiframe(x)
            } else {
                SysinfoOption::OddMultiframe(x)
            }
        }
        2 => {
            let start = r.pos();
            let x = r.peek("default access code A", 20)? as u32;
            r.read("IMM", 4)?;
            r.read("WT", 4)?;
            r.read("NU", 4)?;
            r.read("frame-length factor", 1)?;
            r.read("timeslot pointer", 4)?;
            r.read("minimum PDU priority", 3)?;
            debug_assert_eq!(r.pos(), start + 20);
            SysinfoOption::AccessCodeA(x)
        }
        _ => {
            let raw = r.peek("extended services", 20)? as u32;
            let security_information = r.read_u8("security information", 8)?;
            r.mean(|| security_text(security_information));
            let sds_tl_addressing_method = r.read_u8("SDS-TL addressing method", 2)?;
            let gck_supported = r.flag("GCK supported")?;
            let section = r.read_u8("section", 2)?;
            let section_information = r.read_u8("section information", 7)?;
            SysinfoOption::ExtendedServices(ExtendedServices {
                raw,
                security_information,
                sds_tl_addressing_method,
                gck_supported,
                section,
                section_information,
            })
        }
    };
    let mle = parse_mle_sysinfo(r)?;
    Ok(MacSysinfo {
        main_carrier,
        frequency_band,
        offset,
        duplex_spacing,
        reverse_operation,
        common_scch,
        ms_txpwr_max_cell,
        rxlev_access_min,
        access_parameter,
        radio_downlink_timeout,
        hyperframe,
        cck_id,
        option,
        mle,
    })
}

pub fn ms_txpwr_text(code: u8) -> String {
    match v::ms_txpwr_dbm(code) {
        Some(d) => format!("{d} dBm"),
        None => "reserved".to_string(),
    }
}

fn common_frames_text(x: u32) -> String {
    let frames: Vec<String> = (0..18).filter(|i| x & (1 << (19 - i)) != 0).map(|i| (i + 1).to_string()).collect();
    if frames.is_empty() {
        "no common frames".into()
    } else {
        format!("frames {}", frames.join(","))
    }
}

fn security_text(s: u8) -> String {
    let mut parts = Vec::new();
    if s & 0x80 != 0 {
        parts.push("authentication required");
    }
    if s & 0x40 != 0 {
        parts.push("security class 1 supported");
    }
    parts.push(if s & 0x20 != 0 { "security class 3" } else { "security class 2" });
    parts.join(", ")
}

/// Address of a MAC-RESOURCE PDU: only its type and whether it is the
/// broadcast address are kept (spec 16.5).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AddressInfo {
    /// 0 = Null PDU, 1 SSI, 2 event label, 3 USSI, 4 SMI, 5 SSI + event
    /// label, 6 SSI + usage marker, 7 SMI + event label.
    pub address_type: u8,
    /// SSI-based address equal to all ones.
    pub broadcast: bool,
}

pub fn address_type_text(t: u8) -> &'static str {
    match t & 7 {
        0 => "Null PDU",
        1 => "SSI",
        2 => "event label",
        3 => "USSI",
        4 => "SMI",
        5 => "SSI and event label",
        6 => "SSI and usage marker",
        _ => "SMI and event label",
    }
}

/// Length indication of MAC-RESOURCE (6 bits).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Length {
    /// 2..=34 octets.
    Octets(u8),
    /// The second half slot is stolen: the PDU fills the block.
    SecondHalfStolen,
    /// Start of fragmentation: the PDU fills the block; MAC-FRAG and
    /// MAC-END continue it in later blocks.
    StartOfFragmentation,
}

/// Header of a MAC-RESOURCE PDU and where its TM-SDU is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MacResource {
    pub fill_bits: bool,
    pub position_of_grant: bool,
    /// Encryption mode (2 bits); not 0: the TM-SDU is encrypted (DEC-10)
    /// and never decoded.
    pub encryption_mode: u8,
    pub random_access_flag: bool,
    pub length: Length,
    pub address: AddressInfo,
    pub power_control: Option<u8>,
    pub slot_granting: Option<u8>,
    /// Channel allocation element, when sent (DEC-06).
    pub channel_allocation: Option<ChannelAllocation>,
    /// Bit range of the TM-SDU (after fill bits are removed).
    pub sdu_start: usize,
    pub sdu_end: usize,
    /// End of this MAC PDU in the block (start of the next one).
    pub pdu_end: usize,
}

impl MacResource {
    pub fn null_pdu(&self) -> bool {
        self.address.address_type == 0
    }
    pub fn encrypted(&self) -> bool {
        self.encryption_mode != 0
    }
}

/// Parses the MAC-RESOURCE PDU starting at the reader's position, in a
/// block that ends at the reader's end. The reader is left at the start
/// of the TM-SDU (nothing of it read).
pub fn parse_mac_resource(r: &mut BitReader) -> Result<MacResource> {
    let start = r.pos();
    let pdu_type = r.read_u8("MAC PDU type", 2)?;
    r.mean(|| mac_pdu_type_text(pdu_type).to_string());
    if pdu_type != 0 {
        return Err(Error::Invalid { field: "MAC PDU type", value: u64::from(pdu_type) });
    }
    let fill_bits = r.flag("fill bit indication")?;
    let position_of_grant = r.flag("position of grant")?;
    let encryption_mode = r.read_u8("encryption mode", 2)?;
    r.mean(|| if encryption_mode == 0 { "not encrypted".into() } else { format!("encrypted (mode {encryption_mode}), not decoded") });
    let random_access_flag = r.flag("random access flag")?;
    let li = r.read_u8("length indication", 6)?;
    let length = match li {
        2..=34 => Length::Octets(li),
        0b111110 => Length::SecondHalfStolen,
        0b111111 => Length::StartOfFragmentation,
        _ => return Err(Error::Invalid { field: "length indication", value: u64::from(li) }),
    };
    r.mean(|| match length {
        Length::Octets(n) => format!("{n} octets"),
        Length::SecondHalfStolen => "second half slot stolen".into(),
        Length::StartOfFragmentation => "start of fragmentation".into(),
    });
    let block_end = r.end();
    let pdu_end = match length {
        Length::Octets(n) => start + 8 * usize::from(n),
        _ => block_end,
    };
    if pdu_end > block_end {
        return Err(Error::Truncated { field: "MAC-RESOURCE", offset: start, need: pdu_end - start, have: block_end - start });
    }
    let address_type = r.read_u8("address type", 3)?;
    r.mean(|| address_type_text(address_type).to_string());
    let address_bits = [0usize, 24, 10, 24, 24, 34, 30, 34][usize::from(address_type)];
    let mut broadcast = false;
    let (mut power_control, mut slot_granting, mut channel_allocation) = (None, None, None);
    if address_type != 0 {
        // Only an SSI decides; it is compared, never kept or shown.
        let ssi_based = matches!(address_type, 1 | 3 | 5 | 6);
        broadcast = ssi_based && r.peek("address", 24)? == 0xFF_FFFF;
        r.skip_hidden("address", address_bits, if broadcast { "broadcast (all ones)" } else { "not shown (spec 16.5)" })?;
        if r.flag("power control flag")? {
            power_control = Some(r.read_u8("power control element", 4)?);
        }
        if r.flag("slot granting flag")? {
            slot_granting = Some(r.read_u8("slot granting element", 8)?);
        }
        if r.flag("channel allocation flag")? {
            channel_allocation = Some(parse_channel_allocation(r)?);
        }
    }
    let sdu_start = r.pos();
    if sdu_start > pdu_end {
        return Err(Error::Truncated { field: "MAC-RESOURCE header", offset: start, need: sdu_start - start, have: pdu_end - start });
    }
    let mut sdu_end = pdu_end;
    if fill_bits && address_type != 0 {
        // Fill bits: a 1 followed by 0s up to the end of the PDU.
        sdu_end = r.last_one(sdu_start, pdu_end).ok_or(Error::Invalid { field: "fill bits", value: 0 })?;
    }
    Ok(MacResource {
        fill_bits,
        position_of_grant,
        encryption_mode,
        random_access_flag,
        length,
        address: AddressInfo { address_type, broadcast },
        power_control,
        slot_granting,
        channel_allocation,
        sdu_start,
        sdu_end: if address_type == 0 { sdu_start } else { sdu_end },
        pdu_end: if address_type == 0 { block_end } else { pdu_end },
    })
}

/// Carrier number extension of a channel allocation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AllocationExtension {
    pub frequency_band: u8,
    pub offset: u8,
    pub duplex_spacing: u8,
    pub reverse_operation: bool,
}

/// Channel allocation element (21.5.2) without the augmented part: where
/// a call or a control channel moves to (DEC-06). It says nothing about
/// who the addressed party is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ChannelAllocation {
    /// 0 replace, 1 additional, 2 quit and go, 3 replace + CSS channel.
    pub allocation_type: u8,
    /// Timeslots assigned, timeslot 1 in the most significant of 4 bits
    /// (0: the timeslots of the MCCH sharing).
    pub timeslots: u8,
    /// 1 downlink only, 2 uplink only, 3 both (0, augmented: unsupported).
    pub up_downlink: u8,
    pub clch_permission: bool,
    pub cell_change: bool,
    pub carrier: u16,
    pub extension: Option<AllocationExtension>,
    /// 0 none (then the frame 18 pattern), 1..3 one in 1, 2, 4 multiframes.
    pub monitoring_pattern: u8,
    pub frame18_monitoring: Option<u8>,
}

pub fn allocation_type_text(t: u8) -> &'static str {
    match t & 3 {
        0 => "replace the current channel",
        1 => "additional channel",
        2 => "quit the current channel and go",
        _ => "replace, with a common secondary control channel",
    }
}

pub fn up_downlink_text(t: u8) -> &'static str {
    match t & 3 {
        0 => "augmented channel allocation",
        1 => "downlink only",
        2 => "uplink only",
        _ => "uplink and downlink",
    }
}

/// Timeslots of a 4-bit timeslot assigned field: "1,3", or "MCCH timeslots".
pub fn timeslots_text(bits: u8) -> String {
    if bits & 15 == 0 {
        return "the timeslots of the MCCH sharing".into();
    }
    (1..=4).filter(|t| bits & (8 >> (t - 1)) != 0).map(|t| t.to_string()).collect::<Vec<_>>().join(",")
}

/// Parses a channel allocation element (21.5.2).
pub fn parse_channel_allocation(r: &mut BitReader) -> Result<ChannelAllocation> {
    let allocation_type = r.read_u8("allocation type", 2)?;
    r.mean(|| allocation_type_text(allocation_type).into());
    let timeslots = r.read_u8("timeslot assigned", 4)?;
    r.mean(|| format!("timeslot(s) {}", timeslots_text(timeslots)));
    let up_downlink = r.read_u8("up/downlink assigned", 2)?;
    r.mean(|| up_downlink_text(up_downlink).into());
    let clch_permission = r.flag("CLCH permission")?;
    let cell_change = r.flag("cell change flag")?;
    let carrier = r.read_u16("carrier number", 12)?;
    let mut extension = None;
    if r.flag("extended carrier numbering flag")? {
        let frequency_band = r.read_u8("frequency band", 4)?;
        let offset = r.read_u8("offset", 2)?;
        r.mean(|| crate::tetra::values::offset_text(offset).into());
        let duplex_spacing = r.read_u8("duplex spacing", 3)?;
        let reverse_operation = r.flag("reverse operation")?;
        extension = Some(AllocationExtension { frequency_band, offset, duplex_spacing, reverse_operation });
    }
    let monitoring_pattern = r.read_u8("monitoring pattern", 2)?;
    let mut frame18_monitoring = None;
    if monitoring_pattern == 0 {
        frame18_monitoring = Some(r.read_u8("frame 18 monitoring pattern", 2)?);
    }
    if up_downlink == 0 {
        return Err(Error::Unsupported { what: "augmented channel allocation" });
    }
    Ok(ChannelAllocation {
        allocation_type,
        timeslots,
        up_downlink,
        clch_permission,
        cell_change,
        carrier,
        extension,
        monitoring_pattern,
        frame18_monitoring,
    })
}

/// MAC-FRAG (21.4.3.2, downlink): a continuation of a fragmented TM-SDU;
/// it fills the rest of its block.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MacFrag {
    pub fill_bits: bool,
    pub sdu_start: usize,
    pub sdu_end: usize,
}

/// MAC-END (21.4.3.3, downlink): the last fragment of a TM-SDU.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MacEnd {
    pub fill_bits: bool,
    pub position_of_grant: bool,
    /// Length of the MAC-END PDU in octets (2..=34).
    pub length_octets: u8,
    pub slot_granting: Option<u8>,
    pub channel_allocation: Option<ChannelAllocation>,
    pub sdu_start: usize,
    pub sdu_end: usize,
    pub pdu_end: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FragEnd {
    Frag(MacFrag),
    End(MacEnd),
}

/// Parses a MAC-FRAG or MAC-END PDU (MAC PDU type 1) at the reader's
/// position, in a block that ends at the reader's end. The reader is left
/// at the start of the TM-SDU fragment.
pub fn parse_mac_frag_end(r: &mut BitReader) -> Result<FragEnd> {
    let start = r.pos();
    let pdu_type = r.read_u8("MAC PDU type", 2)?;
    r.mean(|| mac_pdu_type_text(pdu_type).to_string());
    if pdu_type != 1 {
        return Err(Error::Invalid { field: "MAC PDU type", value: u64::from(pdu_type) });
    }
    let end = r.read_u8("MAC PDU subtype", 1)? == 1;
    r.mean(|| if end { "MAC-END".into() } else { "MAC-FRAG".into() });
    let fill_bits = r.flag("fill bit indication")?;
    let block_end = r.end();
    let fill_end = |r: &BitReader, from: usize, to: usize| -> Result<usize> {
        if fill_bits {
            r.last_one(from, to).ok_or(Error::Invalid { field: "fill bits", value: 0 })
        } else {
            Ok(to)
        }
    };
    if !end {
        let sdu_start = r.pos();
        let sdu_end = fill_end(r, sdu_start, block_end)?;
        return Ok(FragEnd::Frag(MacFrag { fill_bits, sdu_start, sdu_end }));
    }
    let position_of_grant = r.flag("position of grant")?;
    let li = r.read_u8("length indication", 6)?;
    if !(2..=34).contains(&li) {
        return Err(Error::Invalid { field: "length indication", value: u64::from(li) });
    }
    r.mean(|| format!("{li} octets"));
    let pdu_end = start + 8 * usize::from(li);
    if pdu_end > block_end {
        return Err(Error::Truncated { field: "MAC-END", offset: start, need: pdu_end - start, have: block_end - start });
    }
    let mut slot_granting = None;
    if r.flag("slot granting flag")? {
        slot_granting = Some(r.read_u8("slot granting element", 8)?);
    }
    let mut channel_allocation = None;
    if r.flag("channel allocation flag")? {
        channel_allocation = Some(parse_channel_allocation(r)?);
    }
    let sdu_start = r.pos();
    if sdu_start > pdu_end {
        return Err(Error::Truncated { field: "MAC-END header", offset: start, need: sdu_start - start, have: pdu_end - start });
    }
    let sdu_end = fill_end(r, sdu_start, pdu_end)?;
    Ok(FragEnd::End(MacEnd {
        fill_bits,
        position_of_grant,
        length_octets: li,
        slot_granting,
        channel_allocation,
        sdu_start,
        sdu_end,
        pdu_end,
    }))
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::testbits::W;

    fn sync_bits() -> Vec<u8> {
        let mut w = W::default();
        w.put(1, 4).put(12, 6).put(2, 2).put(18, 5).put(60, 6).put(1, 2).put(3, 3).put(1, 1).put(0, 1).put(0, 1);
        w.put(901, 10).put(5, 14).put(2, 2).put(3, 2).put(1, 1);
        w.bits
    }

    #[test]
    fn sync_fields() {
        let b = sync_bits();
        assert_eq!(b.len(), SYNC_BITS);
        let s = parse_sync(&mut BitReader::new(&b)).unwrap();
        assert_eq!((s.system_code, s.colour_code, s.timeslot, s.frame, s.multiframe), (1, 12, 3, 18, 60));
        assert_eq!((s.sharing_mode, s.ts_reserved_frames, s.u_plane_dtx, s.frame18_extension), (1, 3, true, false));
        assert_eq!(s.mle, MleSync { mcc: 901, mnc: 5, neighbour_cell_broadcast: 2, cell_service_level: 3, late_entry: true });
    }

    #[test]
    fn sync_rejects_bad_numbers_and_truncation() {
        let mut b = sync_bits();
        b[12..17].copy_from_slice(&[0, 0, 0, 0, 0]);
        assert_eq!(parse_sync(&mut BitReader::new(&b)), Err(Error::Invalid { field: "frame number", value: 0 }));
        let b = sync_bits();
        assert!(matches!(parse_sync(&mut BitReader::new(&b[..59])), Err(Error::Truncated { field: "late entry supported", .. })));
    }

    pub(crate) fn sysinfo_bits(option: u8, option_value: u32, cck: bool) -> Vec<u8> {
        let mut w = W::default();
        w.put(2, 2).put(0, 2).put(3600, 12).put(3, 4).put(1, 2).put(0, 3).put(0, 1).put(1, 2);
        w.put(5, 3).put(2, 4).put(7, 4).put(3, 4).put(u64::from(cck), 1).put(0x1234, 16);
        w.put(u64::from(option), 2).put(u64::from(option_value), 20);
        w.put(0x2A5, 14).put(0xFFFF, 16).put(0b1100_0100_0010, 12);
        w.bits
    }

    #[test]
    fn sysinfo_fields_and_derived_values() {
        let es = (0b0110_0000u32 << 12) | 0x123;
        let b = sysinfo_bits(3, es, false);
        assert_eq!(b.len(), SYSINFO_BITS);
        let s = parse_sysinfo(&mut BitReader::new(&b)).unwrap();
        assert_eq!((s.main_carrier, s.frequency_band, s.offset, s.duplex_spacing), (3600, 3, 1, 0));
        assert_eq!((s.common_scch, s.ms_txpwr_max_cell, s.rxlev_access_min, s.access_parameter), (1, 5, 2, 7));
        assert_eq!(s.radio_downlink_timeout, 3);
        assert_eq!((s.hyperframe, s.cck_id), (Some(0x1234), None));
        assert_eq!(s.option.flag(), 3);
        assert_eq!(s.option.value(), es);
        assert_eq!(s.security_information(), Some(0x60));
        assert_eq!(s.security_class(), 3);
        assert_eq!(s.mle.location_area, 0x2A5);
        assert_eq!(s.mle.subscriber_class, 0xFFFF);
        assert!(s.mle.bs_service_details.air_encryption());
        assert_eq!(s.downlink_hz(), 390_006_250);
        assert_eq!(s.uplink_hz(), Some(380_006_250));
    }

    #[test]
    fn sysinfo_cck_and_other_options() {
        let s = parse_sysinfo(&mut BitReader::new(&sysinfo_bits(2, 0xABCDE, true))).unwrap();
        assert_eq!((s.hyperframe, s.cck_id), (None, Some(0x1234)));
        assert_eq!(s.option, SysinfoOption::AccessCodeA(0xABCDE));
        assert_eq!(s.security_information(), None);
        let s = parse_sysinfo(&mut BitReader::new(&sysinfo_bits(1, 0x80001, false))).unwrap();
        assert_eq!(s.option, SysinfoOption::OddMultiframe(0x80001));
    }

    #[test]
    fn sysinfo_rejects_other_pdus_and_truncation() {
        let mut b = sysinfo_bits(0, 0, false);
        b[3] = 1;
        assert_eq!(parse_sysinfo(&mut BitReader::new(&b)), Err(Error::Invalid { field: "broadcast type", value: 1 }));
        b[0] = 0;
        assert!(matches!(parse_sysinfo(&mut BitReader::new(&b)), Err(Error::Invalid { field: "MAC PDU type", .. })));
        let b = sysinfo_bits(0, 0, false);
        for n in 0..SYSINFO_BITS {
            assert!(matches!(parse_sysinfo(&mut BitReader::new(&b[..n])), Err(Error::Truncated { .. })), "{n}");
        }
    }

    pub(crate) fn resource_header(w: &mut W, fill: bool, enc: u8, li: u8, addr_type: u8, addr: u64) {
        w.put(0, 2).put(u64::from(fill), 1).put(0, 1).put(u64::from(enc), 2).put(0, 1).put(u64::from(li), 6);
        w.put(u64::from(addr_type), 3);
        let n = [0usize, 24, 10, 24, 24, 34, 30, 34][usize::from(addr_type)];
        if n > 0 {
            w.put(addr, n).put(0, 3);
        }
    }

    #[test]
    fn mac_resource_broadcast_with_fill_bits() {
        let mut w = W::default();
        resource_header(&mut w, true, 0, 7, 1, 0xFFFFFF);
        w.put(0b1011, 4); // 4-bit SDU
        w.put(1, 1).put(0, 7); // fill up to 56 bits
        w.put(0, 268 - 56);
        let b = w.bits;
        let m = parse_mac_resource(&mut BitReader::new(&b)).unwrap();
        assert_eq!(m.length, Length::Octets(7));
        assert_eq!(m.address, AddressInfo { address_type: 1, broadcast: true });
        assert_eq!((m.sdu_start, m.sdu_end, m.pdu_end), (43, 47, 56));
        assert!(!m.null_pdu() && !m.encrypted());
    }

    #[test]
    fn mac_resource_addressed_null_and_errors() {
        let mut w = W::default();
        resource_header(&mut w, false, 1, 6, 1, 0x123456);
        w.put(0, 268 - 43);
        let m = parse_mac_resource(&mut BitReader::new(&w.bits)).unwrap();
        assert!(!m.address.broadcast);
        assert!(m.encrypted());

        let mut w = W::default();
        resource_header(&mut w, false, 0, 2, 0, 0);
        w.put(0, 100);
        let m = parse_mac_resource(&mut BitReader::new(&w.bits)).unwrap();
        assert!(m.null_pdu());
        assert_eq!(m.pdu_end, 116);

        let mut w = W::default();
        resource_header(&mut w, false, 0, 0, 1, 0);
        assert!(matches!(parse_mac_resource(&mut BitReader::new(&w.bits)), Err(Error::Invalid { field: "length indication", .. })));

        let mut w = W::default();
        resource_header(&mut w, false, 0, 20, 1, 0xFFFFFF);
        w.put(0, 40);
        assert!(matches!(parse_mac_resource(&mut BitReader::new(&w.bits)), Err(Error::Truncated { .. })));

        let mut w = W::default();
        resource_header(&mut w, false, 0, 63, 3, 0xFFFFFF);
        w.put(0, 50);
        let m = parse_mac_resource(&mut BitReader::new(&w.bits)).unwrap();
        assert_eq!(m.length, Length::StartOfFragmentation);
        assert_eq!(m.pdu_end, w.bits.len());
    }

    #[test]
    fn mac_resource_ssi_with_event_label_is_broadcast_by_its_ssi() {
        let mut w = W::default();
        resource_header(&mut w, false, 0, 10, 5, (0xFFFFFFu64 << 10) | 0x155);
        w.put(0, 80 - 53);
        let m = parse_mac_resource(&mut BitReader::new(&w.bits)).unwrap();
        assert!(m.address.broadcast);
        let mut w = W::default();
        resource_header(&mut w, false, 0, 10, 2, 0x3FF);
        w.put(0, 80 - 29);
        assert!(!parse_mac_resource(&mut BitReader::new(&w.bits)).unwrap().address.broadcast);
    }

    #[test]
    fn traced_resource_never_shows_the_address() {
        let mut w = W::default();
        resource_header(&mut w, false, 0, 6, 1, 0x123456);
        w.put(0, 5);
        let mut r = BitReader::traced(&w.bits);
        parse_mac_resource(&mut r).unwrap();
        let t = r.into_trace();
        let a = t.iter().find(|f| f.name == "address").unwrap();
        assert_eq!(a.raw, None);
        assert!(!format!("{t:?}").contains(&0x123456u64.to_string()));
    }
}
