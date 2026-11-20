// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The C++ side of the protocol crate's C ABI (mle.hpp). Copied from yantra
// 68ea20a: plugins/tetra/decoder/src/mle.cpp, adapted (namespace, CMCE
// PDUs only typed, RAD-17).

#include "yantra/receiver/mle.hpp"

#include "yantra/proto.h"

#include <chrono>
#include <cstdio>
#include <vector>

namespace yantra::receiver::tetra {
namespace {

NeighbourInfo neighbour_of(const yantra_proto_tetra_neighbour& c)
{
    NeighbourInfo n;
    n.cell_id = c.cell_id;
    n.reselection_types = c.reselection_types;
    n.synchronised = c.synchronised != 0;
    n.cell_load = c.cell_load;
    n.main_carrier = c.main_carrier;
    const auto has = [&](uint32_t bit) { return (c.present & bit) != 0; };
    if (has(YANTRA_PROTO_NB_EXTENSION))
        n.carrier_extension = CarrierExtensionInfo{c.frequency_band, c.offset, c.duplex_spacing, c.reverse_operation != 0};
    if (has(YANTRA_PROTO_NB_MCC))
        n.mcc = c.mcc;
    if (has(YANTRA_PROTO_NB_MNC))
        n.mnc = c.mnc;
    if (has(YANTRA_PROTO_NB_LOCATION_AREA))
        n.location_area = c.location_area;
    if (has(YANTRA_PROTO_NB_MAX_MS_TXPWR))
        n.max_ms_txpwr = c.max_ms_txpwr;
    if (has(YANTRA_PROTO_NB_MIN_RX_ACCESS))
        n.min_rx_access_level = c.min_rx_access_level;
    if (has(YANTRA_PROTO_NB_SUBSCRIBER_CLASS))
        n.subscriber_class = c.subscriber_class;
    if (has(YANTRA_PROTO_NB_SERVICE_DETAILS))
        n.service_details = c.bs_service_details;
    if (has(YANTRA_PROTO_NB_TIMESHARE))
        n.timeshare_security = c.timeshare_security;
    if (has(YANTRA_PROTO_NB_FRAME_OFFSET))
        n.tdma_frame_offset = c.tdma_frame_offset;
    return n;
}

NwrkBroadcastInfo nwrk_of(const yantra_proto_tetra_nwrk_broadcast& c)
{
    NwrkBroadcastInfo b;
    b.reselect = {c.slow_threshold, c.slow_hysteresis, c.fast_threshold, c.fast_hysteresis};
    b.cell_load = c.cell_load;
    if (c.network_time_present)
        b.network_time = NetworkTimeInfo{c.utc_time, c.local_offset_negative != 0, c.local_offset, c.year, c.time_reserved};
    b.neighbours_present = c.neighbours_present != 0;
    for (uint8_t i = 0; i < c.neighbour_count && i < 7; ++i)
        b.neighbours.push_back(neighbour_of(c.neighbours[i]));
    return b;
}

/// The fields every PDU view shares: LLC, MLE-level PDU, status.
void sdu_of(const yantra_proto_tetra_dl_pdu& c, BlockPdu& p)
{
    if (c.llc_type != YANTRA_PROTO_NONE)
        p.llc_type = c.llc_type;
    if (c.mle_pdisc != YANTRA_PROTO_NONE)
        p.pdisc = c.mle_pdisc;
    if (c.mle_pdu_type != YANTRA_PROTO_NONE)
        p.pdu_type = c.mle_pdu_type;
    p.fcs = static_cast<Fcs>(c.fcs);
    p.sdu_status = c.sdu_status;
    if (c.content == YANTRA_PROTO_CONTENT_NWRK_BROADCAST)
        p.nwrk = nwrk_of(c.nwrk);
}

std::optional<ChannelAllocationInfo> chan_alloc_of(const yantra_proto_tetra_chan_alloc& c)
{
    if (!c.present)
        return std::nullopt;
    ChannelAllocationInfo a;
    a.allocation_type = c.allocation_type;
    a.timeslots = c.timeslots;
    a.up_downlink = c.up_downlink;
    a.cell_change = c.cell_change != 0;
    a.carrier = c.carrier;
    if (c.extension_present)
        a.extension = CarrierExtensionInfo{c.frequency_band, c.offset, c.duplex_spacing, c.reverse_operation != 0};
    return a;
}

}  // namespace

std::string ChannelAllocationInfo::text() const
{
    std::string ts;
    for (int t = 1; t <= 4; ++t)
        if (timeslots & (8 >> (t - 1)))
            ts += (ts.empty() ? "" : ",") + std::to_string(t);
    static const char* const kDir[4] = {"augmented", "downlink", "uplink", "uplink and downlink"};
    std::string s = "carrier " + std::to_string(carrier);
    if (extension)
        s += " (band " + std::to_string(extension->frequency_band) + ")";
    s += ts.empty() ? ", MCCH timeslots" : ", timeslot" + std::string(ts.size() > 1 ? "s " : " ") + ts;
    s += std::string(", ") + kDir[up_downlink & 3];
    return s;
}

std::string NetworkTimeInfo::text() const
{
    using namespace std::chrono;
    const uint32_t secs = 2 * utc_time;
    const sys_days day = sys_days{year_month_day{std::chrono::year{2000 + year}, January, 1d}} + days{secs / 86400};
    const year_month_day ymd{day};
    const uint32_t s = secs % 86400;
    const unsigned off = 15u * local_offset;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u %02u:%02u:%02u UTC%c%02u:%02u", static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()), s / 3600, s / 60 % 60, s % 60,
                  local_offset_negative ? '-' : '+', off / 60, off % 60);
    return buf;
}

uint64_t NeighbourInfo::downlink_hz(uint8_t serving_band, uint8_t serving_offset) const
{
    if (carrier_extension)
        return carrier_downlink_hz(carrier_extension->frequency_band, main_carrier, carrier_extension->offset);
    return carrier_downlink_hz(serving_band, main_carrier, serving_offset);
}

std::string llc_type_name(uint8_t t)
{
    static const char* const kNames[16] = {"BL-ADATA",         "BL-DATA",          "BL-UDATA",
                                           "BL-ACK",           "BL-ADATA+FCS",     "BL-DATA+FCS",
                                           "BL-UDATA+FCS",     "BL-ACK+FCS",       "AL-SETUP",
                                           "AL-DATA/FINAL",    "AL-UDATA/UFINAL",  "AL-ACK/RNR",
                                           "AL-RECONNECT",     "LLC supplementary", "L2 signalling",
                                           "AL-DISC"};
    return kNames[t & 15];
}

std::string mle_pdu_name(uint8_t pdisc, std::optional<uint8_t> type)
{
    static const char* const kPdisc[8] = {"reserved", "MM",  "CMCE", "reserved",
                                          "SNDCP",    "MLE", "MGMT", "testing"};
    static const char* const kMle[8] = {"D-NEW-CELL",    "D-PREPARE-FAIL", "D-NWRK-BROADCAST",
                                        "D-NWRK-BROADCAST-EXTENSION", "D-RESTORE-ACK", "D-RESTORE-FAIL",
                                        "D-CHANNEL-RESPONSE", "extended PDU"};
    static const char* const kMm[16] = {"D-OTAR", "D-AUTHENTICATION", "D-CK CHANGE DEMAND", "D-DISABLE", "D-ENABLE",
                                        "D-LOCATION UPDATE ACCEPT", "D-LOCATION UPDATE COMMAND",
                                        "D-LOCATION UPDATE REJECT", "reserved", "D-LOCATION UPDATE PROCEEDING",
                                        "D-ATTACH/DETACH GROUP IDENTITY", "D-ATTACH/DETACH GROUP IDENTITY ACK",
                                        "D-MM STATUS", "reserved", "reserved", "MM PDU/FUNCTION NOT SUPPORTED"};
    std::string s = kPdisc[pdisc & 7];
    if (!type)
        return s;
    if (pdisc == 5)
        s += " " + std::string(kMle[*type & 7]);
    else if (pdisc == 1)
        s += " " + std::string(kMm[*type & 15]);
    else if (pdisc == 2)
        s += " (not decoded)";  // calls and SDS are out of scope (RAD-17)
    else
        s += " type " + std::to_string(*type);
    return s;
}

std::string cell_load_name(uint8_t code)
{
    static const char* const kNames[4] = {"unknown", "low", "medium", "high"};
    return kNames[code & 3];
}

std::string class_list_text(uint16_t bitmap)
{
    std::string s;
    for (int c = 1; c <= 16;) {
        if (!(bitmap & (0x8000u >> (c - 1)))) {
            ++c;
            continue;
        }
        int last = c;
        while (last < 16 && (bitmap & (0x8000u >> last)))
            ++last;
        if (!s.empty())
            s += ',';
        s += std::to_string(c);
        if (last > c)
            s += '-' + std::to_string(last);
        c = last + 1;
    }
    return s.empty() ? "none" : s;
}

std::string BlockPdu::summary() const
{
    std::string s;
    if (fragment == Fragment::Frag)
        return "fragment continued";
    if (fragment == Fragment::End)
        return "last fragment" + (channel_allocation ? ", channel allocation " + channel_allocation->text() : std::string());
    if (mac_pdu_type != 0)
        return mac_pdu_type == 2 ? "MAC broadcast" : "MAC-D-BLCK";
    if (null_pdu)
        return "Null PDU";
    if (reassembled)
        s = "reassembled from " + std::to_string(fragments) + " fragments: ";
    if (encrypted)
        return s + "encrypted, not decoded";
    if (first_fragment)
        return "first fragment" + (channel_allocation ? ", channel allocation " + channel_allocation->text() : std::string());
    if (channel_allocation)
        s += "channel allocation " + channel_allocation->text() + "; ";
    if (llc_type)
        s += llc_type_name(*llc_type);
    if (pdisc)
        s += (s.empty() ? "" : " ") + mle_pdu_name(*pdisc, pdu_type);
    if (nwrk) {
        const ReselectParams& r = nwrk->reselect;
        char buf[160];
        std::snprintf(buf, sizeof buf, ": SRT %d SRH %d FRT %d FRH %d dB, load %s", ReselectParams::db(r.slow_threshold),
                      ReselectParams::db(r.slow_hysteresis), ReselectParams::db(r.fast_threshold),
                      ReselectParams::db(r.fast_hysteresis), cell_load_name(nwrk->cell_load).c_str());
        s += buf;
        if (nwrk->neighbours_present)
            s += ", " + std::to_string(nwrk->neighbours.size()) + (nwrk->neighbours.size() == 1 ? " neighbour" : " neighbours");
        if (nwrk->network_time)
            s += ", time " + nwrk->network_time->text();
    }
    if (fcs == Fcs::Error)
        s += (s.empty() ? "" : ", ") + std::string("FCS error, not decoded");
    else if (sdu_status != YANTRA_PROTO_OK)
        s += (s.empty() ? "" : ", ") + std::string("TM-SDU not decodable");
    return s;
}

std::vector<BlockPdu> parse_block_pdus(const uint8_t* bits, int nbits)
{
    std::vector<BlockPdu> out;
    if (!bits || nbits <= 0)
        return out;
    uint32_t at = 0;
    // A block holds at most 268 / 16 PDUs; the bound guards against a loop.
    for (int guard = 0; guard < 20; ++guard) {
        yantra_proto_tetra_dl_pdu c{};
        c.struct_size = sizeof c;
        if (yantra_proto_tetra_parse_dl_pdu(bits, static_cast<uint32_t>(nbits), at, &c) != YANTRA_PROTO_OK)
            break;
        BlockPdu p;
        p.offset = static_cast<int>(at);
        p.mac_pdu_type = c.mac_pdu_type;
        p.fragment = static_cast<Fragment>(c.fragment);
        p.sdu_start = static_cast<int>(c.sdu_start);
        p.sdu_end = static_cast<int>(c.sdu_end);
        p.channel_allocation = chan_alloc_of(c.chan_alloc);
        if (c.mac_pdu_type == 1 && c.fragment == YANTRA_PROTO_FRAGMENT_END)
            p.length_octets = c.length_octets;
        if (c.mac_pdu_type == 0) {
            p.null_pdu = c.null_pdu != 0;
            p.encrypted = c.encryption_mode != 0;
            p.first_fragment = p.fragment == Fragment::Start;
            if (c.length_kind == YANTRA_PROTO_LENGTH_OCTETS)
                p.length_octets = c.length_octets;
            // The type only (SSI, event label, ...): the address itself
            // never leaves the protocol crate (RAD-17).
            p.address_type = c.address_type;
            p.broadcast = c.broadcast != 0;
            sdu_of(c, p);
        }
        p.last = c.next_offset == 0;
        out.push_back(std::move(p));
        if (c.next_offset == 0 || c.next_offset <= at)
            break;
        at = c.next_offset;
    }
    return out;
}

BlockPdu parse_tm_sdu(const std::vector<uint8_t>& bits)
{
    BlockPdu p;
    p.reassembled = true;
    p.sdu_end = static_cast<int>(bits.size());
    yantra_proto_tetra_dl_pdu c{};
    c.struct_size = sizeof c;
    const int32_t st = yantra_proto_tetra_parse_tm_sdu(bits.data(), static_cast<uint32_t>(bits.size()), &c);
    if (st == YANTRA_PROTO_ERR_ARGUMENT || st == YANTRA_PROTO_ERR_PANIC) {
        p.sdu_status = st;
        return p;
    }
    sdu_of(c, p);
    p.sdu_bits = bits;
    return p;
}

std::string describe(DescribeKind kind, const uint8_t* bits, int nbits, int offset)
{
    if (!bits || nbits < 0 || offset < 0 || offset > nbits)
        return {};
    uint32_t needed = 0;
    const auto k = static_cast<uint32_t>(kind);
    const auto n = static_cast<uint32_t>(nbits);
    const auto at = static_cast<uint32_t>(offset);
    if (yantra_proto_tetra_describe(k, bits, n, at, nullptr, 0, &needed) != YANTRA_PROTO_ERR_BUFFER_TOO_SMALL)
        return {};
    std::vector<char> buf(needed);
    if (yantra_proto_tetra_describe(k, bits, n, at, buf.data(), needed, &needed) != YANTRA_PROTO_OK)
        return {};
    return std::string(buf.data());
}

std::optional<SysinfoInfo> parse_sysinfo_proto(const uint8_t* bits)
{
    yantra_proto_tetra_sysinfo c{};
    c.struct_size = sizeof c;
    if (!bits || yantra_proto_tetra_parse_sysinfo(bits, 124, &c) != YANTRA_PROTO_OK)
        return std::nullopt;
    SysinfoInfo s;
    s.main_carrier = c.main_carrier;
    s.frequency_band = c.frequency_band;
    s.offset = c.offset;
    s.duplex_spacing = c.duplex_spacing;
    s.reverse_operation = c.reverse_operation != 0;
    s.common_scch = c.common_scch;
    s.ms_txpwr_max_cell = c.ms_txpwr_max_cell;
    s.rxlev_access_min = c.rxlev_access_min;
    s.access_parameter = c.access_parameter;
    s.radio_downlink_timeout = c.radio_downlink_timeout;
    s.hyperframe_present = c.hyperframe_present != 0;
    s.hyperframe = s.hyperframe_present ? c.hyperframe_or_cck : 0;  // never keep a key id
    s.option = c.option;
    s.option_value = c.option_value;
    s.location_area = c.location_area;
    s.subscriber_class = c.subscriber_class;
    s.service_details = c.bs_service_details;
    return s;
}

uint64_t carrier_downlink_hz_proto(uint8_t band, uint16_t carrier, uint8_t offset)
{
    return yantra_proto_tetra_downlink_hz(band, carrier, offset);
}

std::optional<uint64_t> duplex_spacing_hz_proto(uint8_t band, uint8_t duplex)
{
    const int64_t v = yantra_proto_tetra_duplex_spacing_hz(band, duplex);
    if (v < 0)
        return std::nullopt;
    return static_cast<uint64_t>(v);
}

bool proto_abi_matches() { return yantra_proto_abi_version() == YANTRA_PROTO_ABI_VERSION; }

}  // namespace yantra::receiver::tetra
