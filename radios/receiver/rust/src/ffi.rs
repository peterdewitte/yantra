// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! C ABI of the protocol crate, declared by hand in
//! `receiver/rust/include/yantra/proto.h` (no cbindgen). `tools/ci/lint.sh`
//! compares the struct layouts the C compiler sees (`tools/ci/proto_layout.c`)
//! with [`layout_report`], so the two cannot drift apart (RAD-90).
//!
//! Bits are passed unpacked (one bit per byte, any non-zero byte is 1).
//! No function keeps a pointer, allocates on behalf of the caller or
//! touches shared state: every call is independent and thread-safe. A
//! panic never crosses the boundary (`YANTRA_PROTO_ERR_PANIC`).

#![allow(non_camel_case_types)]

use std::ffi::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::bits::{BitReader, Error};
use crate::tetra::block::{describe_text, parse_dl_pdu, parse_tm_sdu_bits, DescribeKind, DlPdu, TmSdu};
use crate::tetra::mac::{self, ChannelAllocation, Length};
use crate::tetra::mle::{MlePdu, NeighbourCell, NwrkBroadcast, MAX_NEIGHBOURS_PER_PDU};
use crate::tetra::values;

pub const YANTRA_PROTO_OK: i32 = 0;
pub const YANTRA_PROTO_ERR_TRUNCATED: i32 = -1;
pub const YANTRA_PROTO_ERR_INVALID: i32 = -2;
pub const YANTRA_PROTO_ERR_UNSUPPORTED: i32 = -3;
pub const YANTRA_PROTO_ERR_ARGUMENT: i32 = -4;
pub const YANTRA_PROTO_ERR_BUFFER_TOO_SMALL: i32 = -5;
pub const YANTRA_PROTO_ERR_PANIC: i32 = -6;

pub const YANTRA_PROTO_DESCRIBE_SYNC: u32 = 1;
pub const YANTRA_PROTO_DESCRIBE_SYSINFO: u32 = 2;
pub const YANTRA_PROTO_DESCRIBE_DL_PDU: u32 = 3;
pub const YANTRA_PROTO_DESCRIBE_TM_SDU: u32 = 4;

pub const YANTRA_PROTO_NB_EXTENSION: u32 = 1 << 0;
pub const YANTRA_PROTO_NB_MCC: u32 = 1 << 1;
pub const YANTRA_PROTO_NB_MNC: u32 = 1 << 2;
pub const YANTRA_PROTO_NB_LOCATION_AREA: u32 = 1 << 3;
pub const YANTRA_PROTO_NB_MAX_MS_TXPWR: u32 = 1 << 4;
pub const YANTRA_PROTO_NB_MIN_RX_ACCESS: u32 = 1 << 5;
pub const YANTRA_PROTO_NB_SUBSCRIBER_CLASS: u32 = 1 << 6;
pub const YANTRA_PROTO_NB_SERVICE_DETAILS: u32 = 1 << 7;
pub const YANTRA_PROTO_NB_TIMESHARE: u32 = 1 << 8;
pub const YANTRA_PROTO_NB_FRAME_OFFSET: u32 = 1 << 9;

pub const YANTRA_PROTO_NONE: u8 = 0xFF;
pub const YANTRA_PROTO_LENGTH_OCTETS: u8 = 0;
pub const YANTRA_PROTO_LENGTH_SECOND_HALF_STOLEN: u8 = 1;
pub const YANTRA_PROTO_LENGTH_START_OF_FRAGMENTATION: u8 = 2;
pub const YANTRA_PROTO_CONTENT_NONE: u8 = 0;
pub const YANTRA_PROTO_CONTENT_NWRK_BROADCAST: u8 = 1;
pub const YANTRA_PROTO_FRAGMENT_NONE: u8 = 0;
pub const YANTRA_PROTO_FRAGMENT_START: u8 = 1;
pub const YANTRA_PROTO_FRAGMENT_FRAG: u8 = 2;
pub const YANTRA_PROTO_FRAGMENT_END: u8 = 3;
pub const YANTRA_PROTO_FCS_NONE: u8 = 0;
pub const YANTRA_PROTO_FCS_OK: u8 = 1;
pub const YANTRA_PROTO_FCS_ERROR: u8 = 2;

/// MAC-SYSINFO + D-MLE-SYSINFO with derived values.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct yantra_proto_tetra_sysinfo {
    pub struct_size: u32,
    pub main_carrier: u16,
    pub frequency_band: u8,
    pub offset: u8,
    pub duplex_spacing: u8,
    pub reverse_operation: u8,
    pub common_scch: u8,
    pub ms_txpwr_max_cell: u8,
    pub rxlev_access_min: u8,
    pub access_parameter: u8,
    pub radio_downlink_timeout: u8,
    pub hyperframe_present: u8,
    pub hyperframe_or_cck: u16,
    pub option: u8,
    pub security_class: u8,
    pub option_value: u32,
    pub location_area: u16,
    pub subscriber_class: u16,
    pub bs_service_details: u16,
    pub security_information_present: u8,
    pub security_information: u8,
    pub offset_hz: i32,
    pub downlink_hz: u64,
    /// 0 when the duplex spacing is reserved for the band.
    pub uplink_hz: u64,
    /// -1 when reserved for the band.
    pub duplex_spacing_hz: i64,
}

/// One neighbour cell information element.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct yantra_proto_tetra_neighbour {
    /// YANTRA_PROTO_NB_* bits of the optional elements sent.
    pub present: u32,
    pub cell_id: u8,
    pub reselection_types: u8,
    pub synchronised: u8,
    pub cell_load: u8,
    pub main_carrier: u16,
    pub mcc: u16,
    pub mnc: u16,
    pub location_area: u16,
    pub subscriber_class: u16,
    pub bs_service_details: u16,
    pub frequency_band: u8,
    pub offset: u8,
    pub duplex_spacing: u8,
    pub reverse_operation: u8,
    pub max_ms_txpwr: u8,
    pub min_rx_access_level: u8,
    pub timeshare_security: u8,
    pub tdma_frame_offset: u8,
}

/// D-NWRK-BROADCAST.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct yantra_proto_tetra_nwrk_broadcast {
    pub struct_size: u32,
    pub slow_threshold: u8,
    pub slow_hysteresis: u8,
    pub fast_threshold: u8,
    pub fast_hysteresis: u8,
    pub cell_load: u8,
    pub network_time_present: u8,
    pub neighbours_present: u8,
    pub neighbour_count: u8,
    pub utc_time: u32,
    pub local_offset_negative: u8,
    pub local_offset: u8,
    pub year: u8,
    pub reserved0: u8,
    pub time_reserved: u16,
    pub reserved1: u16,
    pub neighbours: [yantra_proto_tetra_neighbour; 7],
}

/// Channel allocation element of MAC-RESOURCE / MAC-END (DEC-06).
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct yantra_proto_tetra_chan_alloc {
    pub present: u8,
    pub allocation_type: u8,
    pub timeslots: u8,
    pub up_downlink: u8,
    pub clch_permission: u8,
    pub cell_change: u8,
    pub extension_present: u8,
    pub frequency_band: u8,
    pub offset: u8,
    pub duplex_spacing: u8,
    pub reverse_operation: u8,
    pub monitoring_pattern: u8,
    pub carrier: u16,
    pub reserved0: u16,
}

/// One downlink MAC PDU of a signalling block and its contents.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct yantra_proto_tetra_dl_pdu {
    pub struct_size: u32,
    /// Bit offset of the next PDU in the block; 0 when this was the last.
    pub next_offset: u32,
    pub sdu_start: u32,
    pub sdu_end: u32,
    pub mac_pdu_type: u8,
    pub null_pdu: u8,
    pub encryption_mode: u8,
    pub length_kind: u8,
    pub length_octets: u8,
    pub address_type: u8,
    pub broadcast: u8,
    pub llc_type: u8,
    pub mle_pdisc: u8,
    pub mle_pdu_type: u8,
    pub content: u8,
    /// YANTRA_PROTO_FRAGMENT_*: part of a fragmented TM-SDU.
    pub fragment: u8,
    /// YANTRA_PROTO_FCS_*: the LLC frame check sequence.
    pub fcs: u8,
    pub slot_granting_present: u8,
    pub reserved0: u16,
    /// Status of the TM-SDU decode (LLC/MLE); the MAC fields are valid
    /// whatever it says.
    pub sdu_status: i32,
    pub chan_alloc: yantra_proto_tetra_chan_alloc,
    pub nwrk: yantra_proto_tetra_nwrk_broadcast,
}

fn status_of(e: &Error) -> i32 {
    match e {
        Error::Truncated { .. } => YANTRA_PROTO_ERR_TRUNCATED,
        Error::Invalid { .. } => YANTRA_PROTO_ERR_INVALID,
        Error::Unsupported { .. } => YANTRA_PROTO_ERR_UNSUPPORTED,
    }
}

fn guard<F: FnOnce() -> i32>(f: F) -> i32 {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(YANTRA_PROTO_ERR_PANIC)
}

/// # Safety
/// `bits` must point to `nbits` readable bytes (or be null with nbits 0).
unsafe fn bits_slice<'a>(bits: *const u8, nbits: u32) -> Option<&'a [u8]> {
    if bits.is_null() {
        return if nbits == 0 { Some(&[]) } else { None };
    }
    Some(std::slice::from_raw_parts(bits, nbits as usize))
}

/// Writes `value` to `out` when its declared size is large enough.
///
/// # Safety
/// `out` must be null or point to a writable struct whose first field is
/// `struct_size`.
unsafe fn store<T: Copy>(out: *mut T, value: T) -> bool {
    if out.is_null() {
        return false;
    }
    let declared = *(out as *const u32) as usize;
    if declared < std::mem::size_of::<T>() {
        return false;
    }
    std::ptr::write(out, value);
    true
}

pub fn sysinfo_to_c(s: &mac::MacSysinfo) -> yantra_proto_tetra_sysinfo {
    let sec = s.security_information();
    yantra_proto_tetra_sysinfo {
        struct_size: std::mem::size_of::<yantra_proto_tetra_sysinfo>() as u32,
        main_carrier: s.main_carrier,
        frequency_band: s.frequency_band,
        offset: s.offset,
        duplex_spacing: s.duplex_spacing,
        reverse_operation: s.reverse_operation.into(),
        common_scch: s.common_scch,
        ms_txpwr_max_cell: s.ms_txpwr_max_cell,
        rxlev_access_min: s.rxlev_access_min,
        access_parameter: s.access_parameter,
        radio_downlink_timeout: s.radio_downlink_timeout,
        hyperframe_present: s.hyperframe.is_some().into(),
        hyperframe_or_cck: s.hyperframe.or(s.cck_id).unwrap_or(0),
        option: s.option.flag(),
        security_class: s.security_class(),
        option_value: s.option.value(),
        location_area: s.mle.location_area,
        subscriber_class: s.mle.subscriber_class,
        bs_service_details: s.mle.bs_service_details.raw,
        security_information_present: sec.is_some().into(),
        security_information: sec.unwrap_or(0),
        offset_hz: values::offset_hz(s.offset),
        downlink_hz: s.downlink_hz(),
        uplink_hz: s.uplink_hz().unwrap_or(0),
        duplex_spacing_hz: values::duplex_spacing_hz(s.frequency_band, s.duplex_spacing).map_or(-1, |x| x as i64),
    }
}

pub fn neighbour_to_c(n: &NeighbourCell) -> yantra_proto_tetra_neighbour {
    let mut c = yantra_proto_tetra_neighbour {
        cell_id: n.cell_id,
        reselection_types: n.reselection_types,
        synchronised: n.synchronised.into(),
        cell_load: n.cell_load,
        main_carrier: n.main_carrier,
        ..Default::default()
    };
    let mut p = 0;
    if let Some(e) = n.carrier_extension {
        p |= YANTRA_PROTO_NB_EXTENSION;
        c.frequency_band = e.frequency_band;
        c.offset = e.offset;
        c.duplex_spacing = e.duplex_spacing;
        c.reverse_operation = e.reverse_operation.into();
    }
    macro_rules! opt {
        ($field:ident, $flag:ident) => {
            if let Some(x) = n.$field {
                p |= $flag;
                c.$field = x;
            }
        };
    }
    opt!(mcc, YANTRA_PROTO_NB_MCC);
    opt!(mnc, YANTRA_PROTO_NB_MNC);
    opt!(location_area, YANTRA_PROTO_NB_LOCATION_AREA);
    opt!(max_ms_txpwr, YANTRA_PROTO_NB_MAX_MS_TXPWR);
    opt!(min_rx_access_level, YANTRA_PROTO_NB_MIN_RX_ACCESS);
    opt!(subscriber_class, YANTRA_PROTO_NB_SUBSCRIBER_CLASS);
    opt!(timeshare_security, YANTRA_PROTO_NB_TIMESHARE);
    opt!(tdma_frame_offset, YANTRA_PROTO_NB_FRAME_OFFSET);
    if let Some(s) = n.bs_service_details {
        p |= YANTRA_PROTO_NB_SERVICE_DETAILS;
        c.bs_service_details = s.raw;
    }
    c.present = p;
    c
}

pub fn nwrk_to_c(b: &NwrkBroadcast) -> yantra_proto_tetra_nwrk_broadcast {
    let mut c = yantra_proto_tetra_nwrk_broadcast {
        struct_size: std::mem::size_of::<yantra_proto_tetra_nwrk_broadcast>() as u32,
        slow_threshold: b.reselect.slow_threshold,
        slow_hysteresis: b.reselect.slow_hysteresis,
        fast_threshold: b.reselect.fast_threshold,
        fast_hysteresis: b.reselect.fast_hysteresis,
        cell_load: b.cell_load,
        ..Default::default()
    };
    if let Some(t) = b.network_time {
        c.network_time_present = 1;
        c.utc_time = t.utc_time;
        c.local_offset_negative = t.local_offset_negative.into();
        c.local_offset = t.local_offset;
        c.year = t.year;
        c.time_reserved = t.reserved;
    }
    if let Some(list) = &b.neighbours {
        c.neighbours_present = 1;
        c.neighbour_count = list.len().min(MAX_NEIGHBOURS_PER_PDU) as u8;
        for (slot, n) in c.neighbours.iter_mut().zip(list) {
            *slot = neighbour_to_c(n);
        }
    }
    c
}

fn chan_alloc_to_c(a: &Option<ChannelAllocation>) -> yantra_proto_tetra_chan_alloc {
    let Some(a) = a else { return yantra_proto_tetra_chan_alloc::default() };
    let mut c = yantra_proto_tetra_chan_alloc {
        present: 1,
        allocation_type: a.allocation_type,
        timeslots: a.timeslots,
        up_downlink: a.up_downlink,
        clch_permission: a.clch_permission.into(),
        cell_change: a.cell_change.into(),
        monitoring_pattern: a.monitoring_pattern,
        carrier: a.carrier,
        ..Default::default()
    };
    if let Some(x) = a.extension {
        c.extension_present = 1;
        c.frequency_band = x.frequency_band;
        c.offset = x.offset;
        c.duplex_spacing = x.duplex_spacing;
        c.reverse_operation = x.reverse_operation.into();
    }
    c
}

fn empty_dl_pdu() -> yantra_proto_tetra_dl_pdu {
    yantra_proto_tetra_dl_pdu {
        struct_size: std::mem::size_of::<yantra_proto_tetra_dl_pdu>() as u32,
        llc_type: YANTRA_PROTO_NONE,
        mle_pdisc: YANTRA_PROTO_NONE,
        mle_pdu_type: YANTRA_PROTO_NONE,
        address_type: YANTRA_PROTO_NONE,
        ..Default::default()
    }
}

/// Fills the TM-SDU part (LLC, MLE-level PDU, status) of the C view.
fn sdu_to_c(c: &mut yantra_proto_tetra_dl_pdu, llc: &Option<crate::tetra::llc::Llc>, mle: &Option<MlePdu>, error: &Option<Error>) {
    c.llc_type = llc.map_or(YANTRA_PROTO_NONE, |l| l.pdu_type);
    c.fcs = match llc.and_then(|l| l.fcs_ok) {
        None => YANTRA_PROTO_FCS_NONE,
        Some(true) => YANTRA_PROTO_FCS_OK,
        Some(false) => YANTRA_PROTO_FCS_ERROR,
    };
    c.sdu_status = error.as_ref().map_or(YANTRA_PROTO_OK, status_of);
    match mle {
        Some(MlePdu::NwrkBroadcast(b)) => {
            c.mle_pdisc = crate::tetra::mle::PDISC_MLE;
            c.mle_pdu_type = crate::tetra::mle::MLE_D_NWRK_BROADCAST;
            c.content = YANTRA_PROTO_CONTENT_NWRK_BROADCAST;
            c.nwrk = nwrk_to_c(b);
        }
        Some(MlePdu::OtherMle(t)) => {
            c.mle_pdisc = crate::tetra::mle::PDISC_MLE;
            c.mle_pdu_type = *t;
        }
        Some(MlePdu::Mm(m)) => {
            c.mle_pdisc = crate::tetra::mle::PDISC_MM;
            c.mle_pdu_type = m.pdu_type;
        }
        Some(MlePdu::Cmce(t)) => {
            c.mle_pdisc = crate::tetra::mle::PDISC_CMCE;
            c.mle_pdu_type = *t;
        }
        Some(MlePdu::Other { pdisc, pdu_type }) => {
            c.mle_pdisc = *pdisc;
            c.mle_pdu_type = pdu_type.unwrap_or(YANTRA_PROTO_NONE);
        }
        None => {}
    }
}

/// The C view of one parsed downlink PDU.
pub fn dl_pdu_to_c(p: &crate::tetra::block::Parsed) -> yantra_proto_tetra_dl_pdu {
    let mut c = empty_dl_pdu();
    c.next_offset = p.next.unwrap_or(0) as u32;
    match &p.pdu {
        DlPdu::Other { pdu_type } => c.mac_pdu_type = *pdu_type,
        DlPdu::Frag(f) => {
            c.mac_pdu_type = 1;
            c.fragment = YANTRA_PROTO_FRAGMENT_FRAG;
            c.sdu_start = f.sdu_start as u32;
            c.sdu_end = f.sdu_end as u32;
        }
        DlPdu::End(e) => {
            c.mac_pdu_type = 1;
            c.fragment = YANTRA_PROTO_FRAGMENT_END;
            c.sdu_start = e.sdu_start as u32;
            c.sdu_end = e.sdu_end as u32;
            c.length_octets = e.length_octets;
            c.slot_granting_present = e.slot_granting.is_some().into();
            c.chan_alloc = chan_alloc_to_c(&e.channel_allocation);
        }
        DlPdu::Resource(r) => {
            let m = &r.mac;
            c.sdu_start = m.sdu_start as u32;
            c.sdu_end = m.sdu_end as u32;
            c.null_pdu = m.null_pdu().into();
            c.encryption_mode = m.encryption_mode;
            (c.length_kind, c.length_octets) = match m.length {
                Length::Octets(n) => (YANTRA_PROTO_LENGTH_OCTETS, n),
                Length::SecondHalfStolen => (YANTRA_PROTO_LENGTH_SECOND_HALF_STOLEN, 0),
                Length::StartOfFragmentation => (YANTRA_PROTO_LENGTH_START_OF_FRAGMENTATION, 0),
            };
            if m.length == Length::StartOfFragmentation && !m.null_pdu() {
                c.fragment = YANTRA_PROTO_FRAGMENT_START;
            }
            c.address_type = m.address.address_type;
            c.broadcast = m.address.broadcast.into();
            c.slot_granting_present = m.slot_granting.is_some().into();
            c.chan_alloc = chan_alloc_to_c(&m.channel_allocation);
            sdu_to_c(&mut c, &r.llc, &r.mle, &r.sdu_error);
        }
    }
    c
}

/// The C view of a whole (reassembled) TM-SDU.
pub fn tm_sdu_to_c(t: &TmSdu, nbits: usize) -> yantra_proto_tetra_dl_pdu {
    let mut c = empty_dl_pdu();
    c.sdu_end = nbits as u32;
    sdu_to_c(&mut c, &t.llc, &t.mle, &t.error);
    c
}

/// Version of this C ABI; bumped whenever an exported struct or function
/// changes. Declared as YANTRA_PROTO_ABI_VERSION in the header.
pub const ABI_VERSION: u32 = 3;

#[no_mangle]
pub extern "C" fn yantra_proto_abi_version() -> u32 {
    ABI_VERSION
}

/// Parses the 124 type-1 bits of a BNCH (MAC-SYSINFO + D-MLE-SYSINFO).
///
/// # Safety
/// `bits` points to `nbits` bytes; `out` to a struct with struct_size set.
#[no_mangle]
pub unsafe extern "C" fn yantra_proto_tetra_parse_sysinfo(
    bits: *const u8,
    nbits: u32,
    out: *mut yantra_proto_tetra_sysinfo,
) -> i32 {
    guard(|| {
        let Some(b) = bits_slice(bits, nbits) else { return YANTRA_PROTO_ERR_ARGUMENT };
        if out.is_null() || (*out).struct_size < std::mem::size_of::<yantra_proto_tetra_sysinfo>() as u32 {
            return YANTRA_PROTO_ERR_ARGUMENT;
        }
        match mac::parse_sysinfo(&mut BitReader::new(b)) {
            Ok(s) => {
                store(out, sysinfo_to_c(&s));
                YANTRA_PROTO_OK
            }
            Err(e) => status_of(&e),
        }
    })
}

/// Parses the downlink MAC PDU at bit `offset` of a block of `nbits`.
///
/// # Safety
/// `bits` points to `nbits` bytes; `out` to a struct with struct_size set.
#[no_mangle]
pub unsafe extern "C" fn yantra_proto_tetra_parse_dl_pdu(
    bits: *const u8,
    nbits: u32,
    offset: u32,
    out: *mut yantra_proto_tetra_dl_pdu,
) -> i32 {
    guard(|| {
        let Some(b) = bits_slice(bits, nbits) else { return YANTRA_PROTO_ERR_ARGUMENT };
        if out.is_null() || (*out).struct_size < std::mem::size_of::<yantra_proto_tetra_dl_pdu>() as u32 || offset > nbits {
            return YANTRA_PROTO_ERR_ARGUMENT;
        }
        let mut r = BitReader::new(b);
        r.seek(offset as usize);
        match parse_dl_pdu(&mut r) {
            Ok(p) => {
                store(out, dl_pdu_to_c(&p));
                YANTRA_PROTO_OK
            }
            Err(e) => status_of(&e),
        }
    })
}

/// Decodes a whole TM-SDU of `nbits` (a reassembled one): LLC with its
/// FCS, then the MLE-level PDU. The MAC fields of `out` are "none"; the
/// status is that of the decode (also in out->sdu_status).
///
/// # Safety
/// `bits` points to `nbits` bytes; `out` to a struct with struct_size set.
#[no_mangle]
pub unsafe extern "C" fn yantra_proto_tetra_parse_tm_sdu(
    bits: *const u8,
    nbits: u32,
    out: *mut yantra_proto_tetra_dl_pdu,
) -> i32 {
    guard(|| {
        let Some(b) = bits_slice(bits, nbits) else { return YANTRA_PROTO_ERR_ARGUMENT };
        if out.is_null() || (*out).struct_size < std::mem::size_of::<yantra_proto_tetra_dl_pdu>() as u32 {
            return YANTRA_PROTO_ERR_ARGUMENT;
        }
        let c = tm_sdu_to_c(&parse_tm_sdu_bits(b), b.len());
        let status = c.sdu_status;
        store(out, c);
        status
    })
}

/// Field-by-field decode as text (see `describe_text`), NUL-terminated in
/// `buf`. `*needed` (if not null) receives the size including the NUL;
/// YANTRA_PROTO_ERR_BUFFER_TOO_SMALL when it does not fit (nothing
/// written). A decode error is part of the text, not the status.
///
/// # Safety
/// `bits` points to `nbits` bytes, `buf` to `size` writable bytes (or is
/// null with size 0).
#[no_mangle]
pub unsafe extern "C" fn yantra_proto_tetra_describe(
    kind: u32,
    bits: *const u8,
    nbits: u32,
    offset: u32,
    buf: *mut c_char,
    size: u32,
    needed: *mut u32,
) -> i32 {
    guard(|| {
        let Some(b) = bits_slice(bits, nbits) else { return YANTRA_PROTO_ERR_ARGUMENT };
        let kind = match kind {
            YANTRA_PROTO_DESCRIBE_SYNC => DescribeKind::Sync,
            YANTRA_PROTO_DESCRIBE_SYSINFO => DescribeKind::Sysinfo,
            YANTRA_PROTO_DESCRIBE_DL_PDU => DescribeKind::DlPdu,
            YANTRA_PROTO_DESCRIBE_TM_SDU => DescribeKind::TmSdu,
            _ => return YANTRA_PROTO_ERR_ARGUMENT,
        };
        if offset > nbits || (buf.is_null() && size != 0) {
            return YANTRA_PROTO_ERR_ARGUMENT;
        }
        let text = describe_text(kind, b, offset as usize);
        let n = text.len() + 1;
        if !needed.is_null() {
            *needed = n as u32;
        }
        if n > size as usize {
            return YANTRA_PROTO_ERR_BUFFER_TOO_SMALL;
        }
        std::ptr::copy_nonoverlapping(text.as_ptr(), buf as *mut u8, text.len());
        *buf.add(text.len()) = 0;
        YANTRA_PROTO_OK
    })
}

/// Downlink frequency of a carrier (band, carrier number, offset code).
#[no_mangle]
pub extern "C" fn yantra_proto_tetra_downlink_hz(band: u8, carrier: u16, offset: u8) -> u64 {
    values::downlink_hz(band, carrier, offset)
}

/// Duplex spacing in Hz, -1 when reserved for the band.
#[no_mangle]
pub extern "C" fn yantra_proto_tetra_duplex_spacing_hz(band: u8, duplex: u8) -> i64 {
    values::duplex_spacing_hz(band, duplex).map_or(-1, |x| x as i64)
}

macro_rules! layout {
    ($out:ident, $ty:ident { $($field:ident),* $(,)? }) => {{
        $out.push_str(&format!(
            "struct {} size {} align {}\n",
            stringify!($ty),
            std::mem::size_of::<$ty>(),
            std::mem::align_of::<$ty>()
        ));
        $(
            $out.push_str(&format!(
                "field {}.{} {}\n",
                stringify!($ty),
                stringify!($field),
                std::mem::offset_of!($ty, $field)
            ));
        )*
    }};
}

/// Size, alignment and field offsets of every exported struct and the
/// values of the constants, in the format and order of `tools/ci/proto_layout.c`.
pub fn layout_report() -> String {
    let mut o = String::new();
    layout!(o, yantra_proto_tetra_sysinfo {
        struct_size, main_carrier, frequency_band, offset, duplex_spacing, reverse_operation,
        common_scch, ms_txpwr_max_cell, rxlev_access_min, access_parameter, radio_downlink_timeout,
        hyperframe_present, hyperframe_or_cck, option, security_class, option_value, location_area,
        subscriber_class, bs_service_details, security_information_present, security_information,
        offset_hz, downlink_hz, uplink_hz, duplex_spacing_hz
    });
    layout!(o, yantra_proto_tetra_neighbour {
        present, cell_id, reselection_types, synchronised, cell_load, main_carrier, mcc, mnc,
        location_area, subscriber_class, bs_service_details, frequency_band, offset, duplex_spacing,
        reverse_operation, max_ms_txpwr, min_rx_access_level, timeshare_security, tdma_frame_offset
    });
    layout!(o, yantra_proto_tetra_nwrk_broadcast {
        struct_size, slow_threshold, slow_hysteresis, fast_threshold, fast_hysteresis, cell_load,
        network_time_present, neighbours_present, neighbour_count, utc_time, local_offset_negative,
        local_offset, year, reserved0, time_reserved, reserved1, neighbours
    });
    layout!(o, yantra_proto_tetra_chan_alloc {
        present, allocation_type, timeslots, up_downlink, clch_permission, cell_change,
        extension_present, frequency_band, offset, duplex_spacing, reverse_operation,
        monitoring_pattern, carrier, reserved0
    });
    layout!(o, yantra_proto_tetra_dl_pdu {
        struct_size, next_offset, sdu_start, sdu_end, mac_pdu_type, null_pdu, encryption_mode,
        length_kind, length_octets, address_type, broadcast, llc_type, mle_pdisc, mle_pdu_type,
        content, fragment, fcs, slot_granting_present, reserved0, sdu_status, chan_alloc, nwrk
    });
    let consts: [(&str, i64); 35] = [
        ("YANTRA_PROTO_ABI_VERSION", i64::from(ABI_VERSION)),
        ("YANTRA_PROTO_OK", i64::from(YANTRA_PROTO_OK)),
        ("YANTRA_PROTO_ERR_TRUNCATED", i64::from(YANTRA_PROTO_ERR_TRUNCATED)),
        ("YANTRA_PROTO_ERR_INVALID", i64::from(YANTRA_PROTO_ERR_INVALID)),
        ("YANTRA_PROTO_ERR_UNSUPPORTED", i64::from(YANTRA_PROTO_ERR_UNSUPPORTED)),
        ("YANTRA_PROTO_ERR_ARGUMENT", i64::from(YANTRA_PROTO_ERR_ARGUMENT)),
        ("YANTRA_PROTO_ERR_BUFFER_TOO_SMALL", i64::from(YANTRA_PROTO_ERR_BUFFER_TOO_SMALL)),
        ("YANTRA_PROTO_ERR_PANIC", i64::from(YANTRA_PROTO_ERR_PANIC)),
        ("YANTRA_PROTO_DESCRIBE_SYNC", i64::from(YANTRA_PROTO_DESCRIBE_SYNC)),
        ("YANTRA_PROTO_DESCRIBE_SYSINFO", i64::from(YANTRA_PROTO_DESCRIBE_SYSINFO)),
        ("YANTRA_PROTO_DESCRIBE_DL_PDU", i64::from(YANTRA_PROTO_DESCRIBE_DL_PDU)),
        ("YANTRA_PROTO_DESCRIBE_TM_SDU", i64::from(YANTRA_PROTO_DESCRIBE_TM_SDU)),
        ("YANTRA_PROTO_NB_EXTENSION", i64::from(YANTRA_PROTO_NB_EXTENSION)),
        ("YANTRA_PROTO_NB_MCC", i64::from(YANTRA_PROTO_NB_MCC)),
        ("YANTRA_PROTO_NB_MNC", i64::from(YANTRA_PROTO_NB_MNC)),
        ("YANTRA_PROTO_NB_LOCATION_AREA", i64::from(YANTRA_PROTO_NB_LOCATION_AREA)),
        ("YANTRA_PROTO_NB_MAX_MS_TXPWR", i64::from(YANTRA_PROTO_NB_MAX_MS_TXPWR)),
        ("YANTRA_PROTO_NB_MIN_RX_ACCESS", i64::from(YANTRA_PROTO_NB_MIN_RX_ACCESS)),
        ("YANTRA_PROTO_NB_SUBSCRIBER_CLASS", i64::from(YANTRA_PROTO_NB_SUBSCRIBER_CLASS)),
        ("YANTRA_PROTO_NB_SERVICE_DETAILS", i64::from(YANTRA_PROTO_NB_SERVICE_DETAILS)),
        ("YANTRA_PROTO_NB_TIMESHARE", i64::from(YANTRA_PROTO_NB_TIMESHARE)),
        ("YANTRA_PROTO_NB_FRAME_OFFSET", i64::from(YANTRA_PROTO_NB_FRAME_OFFSET)),
        ("YANTRA_PROTO_NONE", i64::from(YANTRA_PROTO_NONE)),
        ("YANTRA_PROTO_LENGTH_OCTETS", i64::from(YANTRA_PROTO_LENGTH_OCTETS)),
        ("YANTRA_PROTO_LENGTH_SECOND_HALF_STOLEN", i64::from(YANTRA_PROTO_LENGTH_SECOND_HALF_STOLEN)),
        ("YANTRA_PROTO_LENGTH_START_OF_FRAGMENTATION", i64::from(YANTRA_PROTO_LENGTH_START_OF_FRAGMENTATION)),
        ("YANTRA_PROTO_CONTENT_NONE", i64::from(YANTRA_PROTO_CONTENT_NONE)),
        ("YANTRA_PROTO_CONTENT_NWRK_BROADCAST", i64::from(YANTRA_PROTO_CONTENT_NWRK_BROADCAST)),
        ("YANTRA_PROTO_FRAGMENT_NONE", i64::from(YANTRA_PROTO_FRAGMENT_NONE)),
        ("YANTRA_PROTO_FRAGMENT_START", i64::from(YANTRA_PROTO_FRAGMENT_START)),
        ("YANTRA_PROTO_FRAGMENT_FRAG", i64::from(YANTRA_PROTO_FRAGMENT_FRAG)),
        ("YANTRA_PROTO_FRAGMENT_END", i64::from(YANTRA_PROTO_FRAGMENT_END)),
        ("YANTRA_PROTO_FCS_NONE", i64::from(YANTRA_PROTO_FCS_NONE)),
        ("YANTRA_PROTO_FCS_OK", i64::from(YANTRA_PROTO_FCS_OK)),
        ("YANTRA_PROTO_FCS_ERROR", i64::from(YANTRA_PROTO_FCS_ERROR)),
    ];
    for (name, value) in consts {
        o.push_str(&format!("const {name} {value}\n"));
    }
    o
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tetra::mle::tests::full_neighbour;
    use crate::tetra::mle::NwrkBroadcast;

    #[test]
    fn abi_version() {
        assert_eq!(yantra_proto_abi_version(), ABI_VERSION);
    }

    #[test]
    fn sysinfo_through_the_c_abi() {
        let b = crate::tetra::mac::tests::sysinfo_bits(3, 0x60000, false);
        let mut out = yantra_proto_tetra_sysinfo {
            struct_size: std::mem::size_of::<yantra_proto_tetra_sysinfo>() as u32,
            ..Default::default()
        };
        assert_eq!(unsafe { yantra_proto_tetra_parse_sysinfo(b.as_ptr(), b.len() as u32, &mut out) }, YANTRA_PROTO_OK);
        assert_eq!(out.main_carrier, 3600);
        assert_eq!(out.location_area, 0x2A5);
        assert_eq!(out.security_class, 3);
        assert_eq!(out.offset_hz, 6250);
        assert_eq!(out.duplex_spacing_hz, 10_000_000);
        assert_eq!(out.uplink_hz, 380_006_250);
        assert_eq!(
            unsafe { yantra_proto_tetra_parse_sysinfo(b.as_ptr(), 100, &mut out) },
            YANTRA_PROTO_ERR_TRUNCATED
        );
        let mut small = yantra_proto_tetra_sysinfo { struct_size: 8, ..Default::default() };
        assert_eq!(unsafe { yantra_proto_tetra_parse_sysinfo(b.as_ptr(), 124, &mut small) }, YANTRA_PROTO_ERR_ARGUMENT);
        assert_eq!(unsafe { yantra_proto_tetra_parse_sysinfo(std::ptr::null(), 124, &mut out) }, YANTRA_PROTO_ERR_ARGUMENT);
        assert_eq!(
            unsafe { yantra_proto_tetra_parse_sysinfo(b.as_ptr(), 124, std::ptr::null_mut()) },
            YANTRA_PROTO_ERR_ARGUMENT
        );
    }

    #[test]
    fn dl_pdu_and_describe_through_the_c_abi() {
        let p = NwrkBroadcast {
            neighbours: Some(vec![NeighbourCell { cell_id: 5, ..Default::default() }, full_neighbour(6)]),
            ..Default::default()
        };
        let b = crate::tetra::block::tests::nwrk_block(&p);
        let mut out = yantra_proto_tetra_dl_pdu {
            struct_size: std::mem::size_of::<yantra_proto_tetra_dl_pdu>() as u32,
            ..Default::default()
        };
        assert_eq!(unsafe { yantra_proto_tetra_parse_dl_pdu(b.as_ptr(), 268, 0, &mut out) }, YANTRA_PROTO_OK);
        assert_eq!(out.content, YANTRA_PROTO_CONTENT_NWRK_BROADCAST);
        assert_eq!(out.broadcast, 1);
        assert_eq!(out.llc_type, 2);
        assert_eq!(out.nwrk.neighbour_count, 2);
        assert_eq!(out.nwrk.neighbours[1].cell_id, 6);
        assert_eq!(out.nwrk.neighbours[1].present, 0x3FF);
        assert_eq!(out.nwrk.neighbours[1].location_area, 0x1ABC);
        assert_eq!(out.sdu_status, YANTRA_PROTO_OK);
        let next = out.next_offset;
        assert!(next > 0);
        assert_eq!(unsafe { yantra_proto_tetra_parse_dl_pdu(b.as_ptr(), 268, next, &mut out) }, YANTRA_PROTO_OK);
        assert_eq!((out.null_pdu, out.next_offset, out.content), (1, 0, YANTRA_PROTO_CONTENT_NONE));
        assert_eq!(unsafe { yantra_proto_tetra_parse_dl_pdu(b.as_ptr(), 268, 269, &mut out) }, YANTRA_PROTO_ERR_ARGUMENT);

        let mut needed = 0u32;
        assert_eq!(
            unsafe { yantra_proto_tetra_describe(3, b.as_ptr(), 268, 0, std::ptr::null_mut(), 0, &mut needed) },
            YANTRA_PROTO_ERR_BUFFER_TOO_SMALL
        );
        let mut buf = vec![0 as c_char; needed as usize];
        assert_eq!(
            unsafe { yantra_proto_tetra_describe(3, b.as_ptr(), 268, 0, buf.as_mut_ptr(), needed, &mut needed) },
            YANTRA_PROTO_OK
        );
        let text = unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap();
        assert!(text.contains("D-NWRK-BROADCAST"));
        assert_eq!(text.len() + 1, needed as usize);
        assert_eq!(
            unsafe { yantra_proto_tetra_describe(9, b.as_ptr(), 268, 0, buf.as_mut_ptr(), needed, &mut needed) },
            YANTRA_PROTO_ERR_ARGUMENT
        );
    }

    #[test]
    fn helpers() {
        assert_eq!(yantra_proto_tetra_downlink_hz(3, 3600, 0), 390_000_000);
        assert_eq!(yantra_proto_tetra_duplex_spacing_hz(3, 0), 10_000_000);
        assert_eq!(yantra_proto_tetra_duplex_spacing_hz(0, 0), -1);
    }

    #[test]
    fn layout_report_lists_every_struct() {
        let r = layout_report();
        assert_eq!(r.lines().filter(|l| l.starts_with("struct ")).count(), 5);
        assert!(r.contains("struct yantra_proto_tetra_neighbour size 28 align 4\n"));
    }
}
