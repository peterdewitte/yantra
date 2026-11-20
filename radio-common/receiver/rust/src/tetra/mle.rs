// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! MLE layer (EN 300 392-2 clause 18): the protocol discriminator of a
//! TL-SDU, the MLE PDU types of the downlink and D-NWRK-BROADCAST
//! (18.4.1.4.1) with its cell re-select parameters, network time and
//! neighbour cell information (18.5.17). Other entities' PDUs (MM, CMCE,
//! SNDCP) are only named: they are addressed or carry call content
//! (spec 16.5).

use crate::bits::{BitReader, Error, Result};
use crate::tetra::mac::{ms_txpwr_text, parse_service_details};
use crate::tetra::values::{self as v, BsServiceDetails};

/// MLE protocol discriminator (3 bits).
pub fn pdisc_text(p: u8) -> &'static str {
    match p & 7 {
        1 => "MM",
        2 => "CMCE",
        4 => "SNDCP",
        5 => "MLE",
        6 => "TETRA management entity",
        7 => "testing",
        _ => "reserved",
    }
}

pub const PDISC_MM: u8 = 1;
pub const PDISC_MLE: u8 = 5;

/// MLE PDU types of the downlink (3 bits).
pub fn mle_type_text(t: u8) -> &'static str {
    match t & 7 {
        0 => "D-NEW-CELL",
        1 => "D-PREPARE-FAIL",
        2 => "D-NWRK-BROADCAST",
        3 => "D-NWRK-BROADCAST-EXTENSION",
        4 => "D-RESTORE-ACK",
        5 => "D-RESTORE-FAIL",
        6 => "D-CHANNEL-RESPONSE",
        _ => "extended PDU",
    }
}

pub const MLE_D_NWRK_BROADCAST: u8 = 2;

/// MM PDU types of the downlink (4 bits); named only.
pub fn mm_type_text(t: u8) -> &'static str {
    match t & 15 {
        0 => "D-OTAR",
        1 => "D-AUTHENTICATION",
        2 => "D-CK CHANGE DEMAND",
        3 => "D-DISABLE",
        4 => "D-ENABLE",
        5 => "D-LOCATION UPDATE ACCEPT",
        6 => "D-LOCATION UPDATE COMMAND",
        7 => "D-LOCATION UPDATE REJECT",
        9 => "D-LOCATION UPDATE PROCEEDING",
        10 => "D-ATTACH/DETACH GROUP IDENTITY",
        11 => "D-ATTACH/DETACH GROUP IDENTITY ACK",
        12 => "D-MM STATUS",
        15 => "MM PDU/FUNCTION NOT SUPPORTED",
        _ => "reserved",
    }
}

pub const PDISC_CMCE: u8 = 2;
pub const MM_D_LOCATION_UPDATE_ACCEPT: u8 = 5;
pub const MM_D_LOCATION_UPDATE_REJECT: u8 = 7;
pub const MM_D_MM_STATUS: u8 = 12;

/// CMCE PDU types of the downlink (5 bits); only named and counted
/// (call control and SDS are out of scope, spec 16.5).
pub fn cmce_type_text(t: u8) -> &'static str {
    match t & 31 {
        0 => "D-ALERT",
        1 => "D-CALL PROCEEDING",
        2 => "D-CONNECT",
        3 => "D-CONNECT ACKNOWLEDGE",
        4 => "D-DISCONNECT",
        5 => "D-INFO",
        6 => "D-RELEASE",
        7 => "D-SETUP",
        8 => "D-STATUS",
        9 => "D-TX CEASED",
        10 => "D-TX CONTINUE",
        11 => "D-TX GRANTED",
        12 => "D-TX WAIT",
        13 => "D-TX INTERRUPT",
        14 => "D-CALL RESTORE",
        15 => "D-SDS-DATA",
        16 => "D-FACILITY",
        31 => "CMCE FUNCTION NOT SUPPORTED",
        _ => "reserved",
    }
}

/// Location update type (16.10.35).
pub fn location_update_type_text(t: u8) -> &'static str {
    match t & 7 {
        0 => "roaming location updating",
        1 => "migrating location updating",
        2 => "periodic location updating",
        3 => "ITSI attach",
        4 => "call restoration roaming location updating",
        5 => "call restoration migrating location updating",
        6 => "demand location updating",
        _ => "disabled MS updating",
    }
}

/// The non-address fields of an MM PDU (clause 16.9), as far as Yantra
/// decodes them; anything identifying a subscriber or group is skipped
/// unseen (spec 16.5).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct MmPdu {
    pub pdu_type: u8,
    pub location_update_type: Option<u8>,
    pub reject_cause: Option<u8>,
    pub subscriber_class: Option<u16>,
    pub energy_saving: Option<u16>,
    pub scch_information: Option<u8>,
    pub status: Option<u8>,
    /// Identifiers of the type 3/4 elements that followed (contents not
    /// decoded: they may hold group identities).
    pub elements: Vec<u8>,
}

/// Reads the type 3/4 elements after an M-bit: identifier, length and
/// contents, which are skipped unseen.
fn skip_type34(r: &mut BitReader, out: &mut Vec<u8>) -> Result<()> {
    // The M-bit before each element; the PDU may also just end.
    while r.remaining() > 0 && r.flag("more bit")? {
        let id = r.read_u8("type 3/4 element identifier", 4)?;
        let len = usize::from(r.read_u16("type 3/4 element length", 11)?);
        r.skip_hidden("type 3/4 element contents", len, "not decoded (may hold group identities, spec 16.5)")?;
        out.push(id);
    }
    Ok(())
}

/// Optional element after its P-bit.
fn opt(r: &mut BitReader, name: &'static str, width: usize) -> Result<Option<u64>> {
    if r.remaining() == 0 || !r.flag("presence bit")? {
        return Ok(None);
    }
    Ok(Some(r.read(name, width)?))
}

/// Optional address element after its P-bit: skipped unseen.
fn opt_hidden(r: &mut BitReader, name: &'static str, width: usize) -> Result<()> {
    if r.remaining() > 0 && r.flag("presence bit")? {
        r.skip_hidden(name, width, "not shown (spec 16.5)")?;
    }
    Ok(())
}

/// Parses an MM PDU after the protocol discriminator.
pub fn parse_mm(r: &mut BitReader) -> Result<MmPdu> {
    let t = r.read_u8("MM PDU type", 4)?;
    r.mean(|| mm_type_text(t).to_string());
    let mut m = MmPdu { pdu_type: t, ..Default::default() };
    match t {
        MM_D_LOCATION_UPDATE_ACCEPT => {
            let lu = r.read_u8("location update accept type", 3)?;
            r.mean(|| location_update_type_text(lu).into());
            m.location_update_type = Some(lu);
            if r.remaining() > 0 && r.flag("optional bit")? {
                opt_hidden(r, "SSI", 24)?;
                opt_hidden(r, "address extension", 24)?;
                m.subscriber_class = opt(r, "subscriber class", 16)?.map(|v| v as u16);
                if let Some(c) = m.subscriber_class {
                    r.mean(|| format!("classes {}", v::subscriber_classes_text(c)));
                }
                m.energy_saving = opt(r, "energy saving information", 14)?.map(|v| v as u16);
                m.scch_information = opt(r, "SCCH information and distribution on 18th frame", 6)?.map(|v| v as u8);
                skip_type34(r, &mut m.elements)?;
            }
        }
        MM_D_LOCATION_UPDATE_REJECT => {
            let lu = r.read_u8("location update type", 3)?;
            r.mean(|| location_update_type_text(lu).into());
            m.location_update_type = Some(lu);
            m.reject_cause = Some(r.read_u8("reject cause", 5)?);
            if r.flag("cipher control")? {
                r.read("ciphering parameters", 10)?;
            }
            if r.remaining() > 0 && r.flag("optional bit")? {
                opt_hidden(r, "address extension", 24)?;
                skip_type34(r, &mut m.elements)?;
            }
        }
        MM_D_MM_STATUS => {
            m.status = Some(r.read_u8("status downlink", 6)?);
            r.note("contents", || "further fields not decoded".into());
        }
        _ => r.note("contents", || "not decoded (addressed, spec 16.5)".into()),
    }
    Ok(m)
}

/// Cell re-select parameters (18.5.4), 4 x 4 bits, each 2 dB steps.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct CellReselectParameters {
    /// SRT
    pub slow_threshold: u8,
    /// SRH
    pub slow_hysteresis: u8,
    /// FRT
    pub fast_threshold: u8,
    /// FRH
    pub fast_hysteresis: u8,
}

/// TETRA network time (18.5.24), 48 bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct NetworkTime {
    /// 2-second units since 00:00 UTC on 1 January.
    pub utc_time: u32,
    pub local_offset_negative: bool,
    /// 15-minute units.
    pub local_offset: u8,
    /// Years since 2000.
    pub year: u8,
    pub reserved: u16,
}

impl NetworkTime {
    /// "YYYY-MM-DD hh:mm:ss UTC+hh:mm".
    pub fn text(&self) -> String {
        let secs = 2 * self.utc_time;
        let (y, m, d) = v::date_of(2000 + u32::from(self.year), secs / 86400);
        let s = secs % 86400;
        let off = u32::from(self.local_offset) * 15;
        format!(
            "{y:04}-{m:02}-{d:02} {:02}:{:02}:{:02} UTC{}{:02}:{:02}",
            s / 3600,
            s / 60 % 60,
            s % 60,
            if self.local_offset_negative { '-' } else { '+' },
            off / 60,
            off % 60
        )
    }
}

/// Main carrier number extension (18.5.12), 10 bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct CarrierExtension {
    pub frequency_band: u8,
    pub offset: u8,
    pub duplex_spacing: u8,
    pub reverse_operation: bool,
}

/// Neighbour cell information for CA (18.5.17); optional elements are
/// `None` when not sent.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct NeighbourCell {
    pub cell_id: u8,
    pub reselection_types: u8,
    pub synchronised: bool,
    pub cell_load: u8,
    pub main_carrier: u16,
    pub carrier_extension: Option<CarrierExtension>,
    pub mcc: Option<u16>,
    pub mnc: Option<u16>,
    pub location_area: Option<u16>,
    pub max_ms_txpwr: Option<u8>,
    pub min_rx_access_level: Option<u8>,
    pub subscriber_class: Option<u16>,
    pub bs_service_details: Option<BsServiceDetails>,
    pub timeshare_security: Option<u8>,
    pub tdma_frame_offset: Option<u8>,
}

impl NeighbourCell {
    /// Downlink frequency; without a carrier extension the serving cell's
    /// band and offset apply.
    pub fn downlink_hz(&self, serving_band: u8, serving_offset: u8) -> u64 {
        match self.carrier_extension {
            Some(e) => v::downlink_hz(e.frequency_band, self.main_carrier, e.offset),
            None => v::downlink_hz(serving_band, self.main_carrier, serving_offset),
        }
    }
}

/// D-NWRK-BROADCAST (18.4.1.4.1).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct NwrkBroadcast {
    pub reselect: CellReselectParameters,
    pub cell_load: u8,
    pub network_time: Option<NetworkTime>,
    /// `None` when the number of neighbour cells was not sent.
    pub neighbours: Option<Vec<NeighbourCell>>,
}

/// Most neighbour cells in one D-NWRK-BROADCAST (3-bit count).
pub const MAX_NEIGHBOURS_PER_PDU: usize = 7;

fn parse_reselect(r: &mut BitReader) -> Result<CellReselectParameters> {
    let mut f = |name: &'static str| -> Result<u8> {
        let x = r.read_u8(name, 4)?;
        r.mean(|| format!("{} dB", v::reselect_db(x)));
        Ok(x)
    };
    Ok(CellReselectParameters {
        slow_threshold: f("slow re-select threshold (SRT)")?,
        slow_hysteresis: f("slow re-select hysteresis (SRH)")?,
        fast_threshold: f("fast re-select threshold (FRT)")?,
        fast_hysteresis: f("fast re-select hysteresis (FRH)")?,
    })
}

fn parse_network_time(r: &mut BitReader) -> Result<NetworkTime> {
    let utc_time = r.read_u32("UTC time", 24)?;
    r.mean(|| {
        let s = 2 * utc_time;
        format!("day {} {:02}:{:02}:{:02}", s / 86400 + 1, s / 3600 % 24, s / 60 % 60, s % 60)
    });
    let local_offset_negative = r.flag("local time offset sign")?;
    let local_offset = r.read_u8("local time offset", 6)?;
    r.mean(|| format!("{} min", u32::from(local_offset) * 15));
    let year = r.read_u8("year", 6)?;
    r.mean(|| format!("{}", 2000 + u32::from(year)));
    let reserved = r.read_u16("reserved", 11)?;
    let t = NetworkTime { utc_time, local_offset_negative, local_offset, year, reserved };
    r.note("network time", || t.text());
    Ok(t)
}

fn parse_carrier_extension(r: &mut BitReader) -> Result<CarrierExtension> {
    let frequency_band = r.read_u8("frequency band", 4)?;
    r.mean(|| v::band_text(frequency_band));
    let offset = r.read_u8("offset", 2)?;
    r.mean(|| v::offset_text(offset).to_string());
    let duplex_spacing = r.read_u8("duplex spacing", 3)?;
    r.mean(|| v::duplex_text(frequency_band, duplex_spacing));
    let reverse_operation = r.flag("reverse operation")?;
    Ok(CarrierExtension { frequency_band, offset, duplex_spacing, reverse_operation })
}

/// One neighbour cell information element.
pub fn parse_neighbour(r: &mut BitReader) -> Result<NeighbourCell> {
    let mut n = NeighbourCell { cell_id: r.read_u8("cell identifier CA", 5)?, ..Default::default() };
    n.reselection_types = r.read_u8("cell re-selection types supported", 2)?;
    let t = n.reselection_types;
    r.mean(|| v::reselection_types_text(t).to_string());
    n.synchronised = r.flag("neighbour cell synchronised")?;
    n.cell_load = r.read_u8("cell load CA", 2)?;
    let l = n.cell_load;
    r.mean(|| v::cell_load_text(l).to_string());
    n.main_carrier = r.read_u16("main carrier number", 12)?;
    if !r.flag("optional elements (o-bit)")? {
        return Ok(n);
    }
    if r.flag("p-bit main carrier number extension")? {
        n.carrier_extension = Some(parse_carrier_extension(r)?);
    }
    if r.flag("p-bit MCC")? {
        n.mcc = Some(r.read_u16("MCC", 10)?);
    }
    if r.flag("p-bit MNC")? {
        n.mnc = Some(r.read_u16("MNC", 14)?);
    }
    if r.flag("p-bit location area")? {
        let la = r.read_u16("location area", 14)?;
        r.mean(|| format!("LA {la} (0x{la:04X})"));
        n.location_area = Some(la);
    }
    if r.flag("p-bit maximum MS transmit power")? {
        let x = r.read_u8("maximum MS transmit power", 3)?;
        r.mean(|| ms_txpwr_text(x));
        n.max_ms_txpwr = Some(x);
    }
    if r.flag("p-bit minimum RX access level")? {
        let x = r.read_u8("minimum RX access level", 4)?;
        r.mean(|| format!("{} dBm", v::rxlev_access_min_dbm(x)));
        n.min_rx_access_level = Some(x);
    }
    if r.flag("p-bit subscriber class")? {
        let x = r.read_u16("subscriber class", 16)?;
        r.mean(|| format!("classes {}", v::subscriber_classes_text(x)));
        n.subscriber_class = Some(x);
    }
    if r.flag("p-bit BS service details")? {
        n.bs_service_details = Some(parse_service_details(r, "BS service details")?);
    }
    if r.flag("p-bit timeshare cell and AI encryption")? {
        n.timeshare_security = Some(r.read_u8("timeshare cell and AI encryption", 5)?);
    }
    if r.flag("p-bit TDMA frame offset")? {
        n.tdma_frame_offset = Some(r.read_u8("TDMA frame offset", 6)?);
    }
    Ok(n)
}

/// D-NWRK-BROADCAST after its protocol discriminator and PDU type.
pub fn parse_nwrk_broadcast(r: &mut BitReader) -> Result<NwrkBroadcast> {
    let reselect = parse_reselect(r)?;
    let cell_load = r.read_u8("cell load CA", 2)?;
    r.mean(|| v::cell_load_text(cell_load).to_string());
    let mut out = NwrkBroadcast { reselect, cell_load, network_time: None, neighbours: None };
    if !r.flag("optional elements (o-bit)")? {
        return Ok(out);
    }
    if r.flag("p-bit TETRA network time")? {
        out.network_time = Some(parse_network_time(r)?);
    }
    if r.flag("p-bit number of CA neighbour cells")? {
        let count = r.read_u8("number of CA neighbour cells", 3)?;
        let mut list = Vec::with_capacity(usize::from(count));
        for i in 0..count {
            if r.tracing() {
                r.set_scope(&format!("neighbour {}: ", i + 1));
            }
            list.push(parse_neighbour(r)?);
        }
        r.set_scope("");
        out.neighbours = Some(list);
    }
    Ok(out)
}

/// An MLE-level view of a TL-SDU.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MlePdu {
    NwrkBroadcast(NwrkBroadcast),
    /// Another MLE PDU type, named only.
    OtherMle(u8),
    /// An MM PDU: type and non-address fields.
    Mm(MmPdu),
    /// A CMCE PDU (calls, SDS): type only, never its contents.
    Cmce(u8),
    /// A PDU of another entity (`pdisc`, its PDU type when read), named only.
    Other { pdisc: u8, pdu_type: Option<u8> },
}

/// Parses the MLE PDU of a TL-SDU (from the reader's position to its end).
pub fn parse_mle(r: &mut BitReader) -> Result<MlePdu> {
    let pdisc = r.read_u8("protocol discriminator", 3)?;
    r.mean(|| pdisc_text(pdisc).to_string());
    match pdisc {
        PDISC_MLE => {
            let t = r.read_u8("MLE PDU type", 3)?;
            r.mean(|| mle_type_text(t).to_string());
            if t == MLE_D_NWRK_BROADCAST {
                Ok(MlePdu::NwrkBroadcast(parse_nwrk_broadcast(r)?))
            } else {
                r.note("contents", || "not decoded (not broadcast information)".into());
                Ok(MlePdu::OtherMle(t))
            }
        }
        PDISC_MM => Ok(MlePdu::Mm(parse_mm(r)?)),
        PDISC_CMCE => {
            let t = r.read_u8("CMCE PDU type", 5)?;
            r.mean(|| cmce_type_text(t).to_string());
            r.note("contents", || "not decoded (call control and SDS, spec 16.5)".into());
            Ok(MlePdu::Cmce(t))
        }
        0 | 3 => Err(Error::Invalid { field: "protocol discriminator", value: u64::from(pdisc) }),
        _ => {
            r.note("contents", || "not decoded (spec 16.5)".into());
            Ok(MlePdu::Other { pdisc, pdu_type: None })
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::testbits::W;

    pub(crate) fn put_neighbour(w: &mut W, n: &NeighbourCell) {
        w.put(n.cell_id.into(), 5).put(n.reselection_types.into(), 2).put(n.synchronised.into(), 1);
        w.put(n.cell_load.into(), 2).put(n.main_carrier.into(), 12);
        let o = n.carrier_extension.is_some()
            || n.mcc.is_some()
            || n.mnc.is_some()
            || n.location_area.is_some()
            || n.max_ms_txpwr.is_some()
            || n.min_rx_access_level.is_some()
            || n.subscriber_class.is_some()
            || n.bs_service_details.is_some()
            || n.timeshare_security.is_some()
            || n.tdma_frame_offset.is_some();
        w.put(o.into(), 1);
        if !o {
            return;
        }
        w.opt(n.carrier_extension.map(|e| {
            (u64::from(e.frequency_band) << 6) | (u64::from(e.offset) << 4) | (u64::from(e.duplex_spacing) << 1) | u64::from(e.reverse_operation)
        }), 10);
        w.opt(n.mcc.map(u64::from), 10);
        w.opt(n.mnc.map(u64::from), 14);
        w.opt(n.location_area.map(u64::from), 14);
        w.opt(n.max_ms_txpwr.map(u64::from), 3);
        w.opt(n.min_rx_access_level.map(u64::from), 4);
        w.opt(n.subscriber_class.map(u64::from), 16);
        w.opt(n.bs_service_details.map(|s| u64::from(s.raw)), 12);
        w.opt(n.timeshare_security.map(u64::from), 5);
        w.opt(n.tdma_frame_offset.map(u64::from), 6);
    }

    pub(crate) fn put_nwrk(w: &mut W, p: &NwrkBroadcast) {
        w.put(5, 3).put(2, 3);
        let s = p.reselect;
        w.put(s.slow_threshold.into(), 4).put(s.slow_hysteresis.into(), 4);
        w.put(s.fast_threshold.into(), 4).put(s.fast_hysteresis.into(), 4);
        w.put(p.cell_load.into(), 2);
        let o = p.network_time.is_some() || p.neighbours.is_some();
        w.put(o.into(), 1);
        if !o {
            return;
        }
        w.put(p.network_time.is_some().into(), 1);
        if let Some(t) = p.network_time {
            w.put(t.utc_time.into(), 24).put(t.local_offset_negative.into(), 1).put(t.local_offset.into(), 6);
            w.put(t.year.into(), 6).put(t.reserved.into(), 11);
        }
        w.put(p.neighbours.is_some().into(), 1);
        if let Some(list) = &p.neighbours {
            w.put(list.len() as u64, 3);
            for n in list {
                put_neighbour(w, n);
            }
        }
    }

    pub(crate) fn full_neighbour(id: u8) -> NeighbourCell {
        NeighbourCell {
            cell_id: id,
            reselection_types: 2,
            synchronised: true,
            cell_load: 1,
            main_carrier: 3603,
            carrier_extension: Some(CarrierExtension { frequency_band: 4, offset: 3, duplex_spacing: 1, reverse_operation: true }),
            mcc: Some(901),
            mnc: Some(16383),
            location_area: Some(0x1ABC),
            max_ms_txpwr: Some(5),
            min_rx_access_level: Some(3),
            subscriber_class: Some(0x8001),
            bs_service_details: Some(BsServiceDetails::new(0xC42)),
            timeshare_security: Some(0x15),
            tdma_frame_offset: Some(63),
        }
    }

    fn roundtrip(p: &NwrkBroadcast) -> NwrkBroadcast {
        let mut w = W::default();
        put_nwrk(&mut w, p);
        match parse_mle(&mut BitReader::new(&w.bits)).unwrap() {
            MlePdu::NwrkBroadcast(x) => x,
            other => panic!("{other:?}"),
        }
    }

    #[test]
    fn nwrk_broadcast_minimal_and_full() {
        let p = NwrkBroadcast {
            reselect: CellReselectParameters { slow_threshold: 2, slow_hysteresis: 3, fast_threshold: 4, fast_hysteresis: 5 },
            cell_load: 2,
            network_time: None,
            neighbours: None,
        };
        assert_eq!(roundtrip(&p), p);
        let p = NwrkBroadcast {
            network_time: Some(NetworkTime { utc_time: 43200 + 1800, local_offset_negative: true, local_offset: 6, year: 26, reserved: 0x7FF }),
            neighbours: Some(vec![
                NeighbourCell { cell_id: 4, main_carrier: 3600, ..Default::default() },
                full_neighbour(31),
                NeighbourCell { cell_id: 2, location_area: Some(7), ..Default::default() },
            ]),
            ..p
        };
        assert_eq!(roundtrip(&p), p);
        assert_eq!(p.network_time.unwrap().text(), "2026-01-02 01:00:00 UTC-01:30");
    }

    #[test]
    fn neighbour_frequencies() {
        let n = full_neighbour(1);
        assert_eq!(n.downlink_hz(3, 0), 400_000_000 + 3603 * 25_000 + 12_500);
        let n = NeighbourCell { main_carrier: 3600, ..Default::default() };
        assert_eq!(n.downlink_hz(3, 2), 389_993_750);
    }

    #[test]
    fn empty_neighbour_list_and_seven() {
        let mut p = NwrkBroadcast { neighbours: Some(vec![]), ..Default::default() };
        assert_eq!(roundtrip(&p), p);
        p.neighbours = Some((1..=7).map(full_neighbour).collect());
        assert_eq!(roundtrip(&p), p);
    }

    #[test]
    fn truncated_at_every_bit_fails_cleanly() {
        let p = NwrkBroadcast {
            network_time: Some(NetworkTime::default()),
            neighbours: Some(vec![full_neighbour(1), full_neighbour(2)]),
            ..Default::default()
        };
        let mut w = W::default();
        put_nwrk(&mut w, &p);
        for n in 0..w.bits.len() {
            let r = parse_mle(&mut BitReader::new(&w.bits[..n]));
            assert!(matches!(r, Err(Error::Truncated { .. })), "{n}: {r:?}");
        }
    }

    /// D-LOCATION UPDATE ACCEPT with every optional element and one
    /// type 3 element.
    pub(crate) fn put_lu_accept(w: &mut W, ssi: u64) {
        w.put(1, 3).put(5, 4).put(2, 3).put(1, 1);
        w.opt(Some(ssi), 24).opt(Some(0xABCDEF), 24).opt(Some(0xF00F), 16).opt(Some(0x1555), 14).opt(Some(0x2A), 6);
        w.put(1, 1).put(5, 4).put(24, 11).put(0x00C0_FFEE, 24).put(0, 1);
    }

    #[test]
    fn mm_location_update_accept_and_reject_hide_addresses() {
        let mut w = W::default();
        put_lu_accept(&mut w, 0x123456);
        let mut r = BitReader::traced(&w.bits);
        let MlePdu::Mm(m) = parse_mle(&mut r).unwrap() else { panic!() };
        assert_eq!(m.location_update_type, Some(2));
        assert_eq!((m.subscriber_class, m.energy_saving, m.scch_information), (Some(0xF00F), Some(0x1555), Some(0x2A)));
        assert_eq!(m.elements, vec![5]);
        assert_eq!(r.pos(), w.bits.len());
        let t = r.into_trace();
        for hidden in ["SSI", "address extension", "type 3/4 element contents"] {
            let f = t.iter().find(|f| f.name == hidden).unwrap();
            assert_eq!(f.raw, None, "{hidden}");
        }
        let text = format!("{t:?}");
        for value in [0x123456u64, 0xABCDEF, 0x00C0_FFEE] {
            assert!(!text.contains(&value.to_string()), "{value}");
        }

        let mut w = W::default();
        w.put(1, 3).put(7, 4).put(0, 3).put(9, 5).put(1, 1).put(0x3FF, 10).put(1, 1).opt(Some(0x777777), 24).put(0, 1);
        let MlePdu::Mm(m) = parse_mle(&mut BitReader::new(&w.bits)).unwrap() else { panic!() };
        assert_eq!((m.location_update_type, m.reject_cause), (Some(0), Some(9)));

        let mut w = W::default();
        w.put(1, 3).put(12, 4).put(33, 6);
        let MlePdu::Mm(m) = parse_mle(&mut BitReader::new(&w.bits)).unwrap() else { panic!() };
        assert_eq!(m.status, Some(33));
        // Truncated inside an element: an error, never a panic.
        let mut w = W::default();
        put_lu_accept(&mut w, 1);
        for n in 0..w.bits.len() {
            let _ = parse_mle(&mut BitReader::new(&w.bits[..n]));
        }
    }

    #[test]
    fn other_pdus_are_named_only() {
        let mut w = W::default();
        w.put(5, 3).put(3, 3).put(0x3FF, 10);
        assert_eq!(parse_mle(&mut BitReader::new(&w.bits)).unwrap(), MlePdu::OtherMle(3));
        let mut w = W::default();
        w.put(1, 3).put(5, 4).put(0, 30);
        assert_eq!(
            parse_mle(&mut BitReader::new(&w.bits)).unwrap(),
            MlePdu::Mm(MmPdu { pdu_type: 5, location_update_type: Some(0), ..Default::default() })
        );
        let mut w = W::default();
        w.put(2, 3).put(15, 5).put(0x1234_5678, 32);
        assert_eq!(parse_mle(&mut BitReader::new(&w.bits)).unwrap(), MlePdu::Cmce(15));
        assert_eq!(cmce_type_text(15), "D-SDS-DATA");
        let mut w = W::default();
        w.put(4, 3).put(0, 30);
        assert_eq!(parse_mle(&mut BitReader::new(&w.bits)).unwrap(), MlePdu::Other { pdisc: 4, pdu_type: None });
        let mut w = W::default();
        w.put(3, 3);
        assert!(matches!(parse_mle(&mut BitReader::new(&w.bits)), Err(Error::Invalid { .. })));
        assert_eq!(mm_type_text(5), "D-LOCATION UPDATE ACCEPT");
        assert_eq!(pdisc_text(6), "TETRA management entity");
    }

    #[test]
    fn trace_names_neighbour_fields() {
        let p = NwrkBroadcast { neighbours: Some(vec![full_neighbour(9)]), ..Default::default() };
        let mut w = W::default();
        put_nwrk(&mut w, &p);
        let mut r = BitReader::traced(&w.bits);
        parse_mle(&mut r).unwrap();
        let t = r.into_trace();
        let la = t.iter().find(|f| f.name == "neighbour 1: location area").unwrap();
        assert_eq!(la.raw, Some(0x1ABC));
        assert_eq!(la.meaning, "LA 6844 (0x1ABC)");
        let srt = t.iter().find(|f| f.name.starts_with("slow re-select threshold")).unwrap();
        assert_eq!(srt.meaning, "0 dB");
    }
}
