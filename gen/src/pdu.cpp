// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Broadcast PDU encoders of the TETRA test-signal generator (RAD-70).
// Clause numbers refer to EN 300 392-2.

#include "yantra/gen/pdu.hpp"

#include "yantra/gen/coding.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace yantra::gen {

namespace {

void check_range(unsigned value, unsigned lo, unsigned hi, const char* field)
{
    if (value < lo || value > hi) {
        std::string msg = std::string(field) + " must be " + std::to_string(lo) + ".." +
                          std::to_string(hi) + ", got " + std::to_string(value);
        throw std::out_of_range(msg);
    }
}

void put_carrier_extension(BitWriter& w, const CarrierExtension& c)
{
    w.put(c.frequency_band, 4, "frequency band");
    w.put(c.offset, 2, "offset");
    w.put(c.duplex_spacing, 3, "duplex spacing");
    w.bit(c.reverse_operation);
}

void put_network_time(BitWriter& w, const NetworkTime& t)
{
    w.put(t.utc_time, 24, "UTC time");
    w.bit(t.local_offset_negative);
    w.put(t.local_offset, 6, "local time offset");
    w.put(t.year, 6, "year");
    w.put(t.reserved, 11, "network time reserved");
}

bool has_optional(const NeighbourCell& n)
{
    return n.carrier_extension || n.mcc || n.mnc || n.location_area || n.max_ms_txpwr ||
           n.min_rx_access_level || n.subscriber_class || n.bs_service_details ||
           n.timeshare_security || n.tdma_frame_offset;
}

// Writes the p-bit and, when present, the element.
template <typename T, typename Put>
void put_optional(BitWriter& w, const std::optional<T>& v, Put put)
{
    w.bit(v.has_value());
    if (v)
        put(*v);
}

void put_neighbour(BitWriter& w, const NeighbourCell& n)
{
    w.put(n.cell_id, 5, "cell identifier CA");
    w.put(n.reselection_types, 2, "cell reselection types supported");
    w.bit(n.synchronised);
    w.put(n.cell_load, 2, "cell load CA");
    w.put(n.main_carrier, 12, "main carrier number");
    const bool o = has_optional(n);
    w.bit(o);
    if (!o)
        return;
    put_optional(w, n.carrier_extension, [&](const CarrierExtension& c) { put_carrier_extension(w, c); });
    put_optional(w, n.mcc, [&](uint16_t v) { w.put(v, 10, "neighbour MCC"); });
    put_optional(w, n.mnc, [&](uint16_t v) { w.put(v, 14, "neighbour MNC"); });
    put_optional(w, n.location_area, [&](uint16_t v) { w.put(v, 14, "neighbour location area"); });
    put_optional(w, n.max_ms_txpwr, [&](uint8_t v) { w.put(v, 3, "maximum MS transmit power"); });
    put_optional(w, n.min_rx_access_level, [&](uint8_t v) { w.put(v, 4, "minimum RX access level"); });
    put_optional(w, n.subscriber_class, [&](uint16_t v) { w.put(v, 16, "neighbour subscriber class"); });
    put_optional(w, n.bs_service_details, [&](const BsServiceDetails& v) { w.put(v.pack(), 12); });
    put_optional(w, n.timeshare_security, [&](uint8_t v) { w.put(v, 5, "timeshare cell and AI encryption"); });
    put_optional(w, n.tdma_frame_offset, [&](uint8_t v) { w.put(v, 6, "TDMA frame offset"); });
}

}  // namespace

// 21.4.4.2 MAC-SYNC + 18.4.2.1 D-MLE-SYNC.
Bits encode_sync(const MacSync& p)
{
    check_range(p.timeslot, 1, 4, "timeslot number");
    check_range(p.frame, 1, 18, "frame number");
    check_range(p.multiframe, 1, 60, "multiframe number");
    BitWriter w;
    w.put(p.system_code, 4, "system code");
    w.put(p.colour_code, 6, "colour code");
    w.put(p.timeslot - 1u, 2);
    w.put(p.frame, 5);
    w.put(p.multiframe, 6);
    w.put(p.sharing_mode, 2, "sharing mode");
    w.put(p.ts_reserved_frames, 3, "TS reserved frames");
    w.bit(p.u_plane_dtx);
    w.bit(p.frame18_extension);
    w.bit(false);  // reserved
    w.put(p.mle.mcc, 10, "MCC");
    w.put(p.mle.mnc, 14, "MNC");
    w.put(p.mle.neighbour_cell_broadcast, 2, "neighbour cell broadcast");
    w.put(p.mle.cell_service_level, 2, "cell service level");
    w.bit(p.mle.late_entry_supported);
    return w.take();
}

uint16_t BsServiceDetails::pack() const
{
    const bool flags[12] = {registration,      deregistration,    priority_cell,  no_minimum_mode,
                            migration,         system_wide_services, voice_service, circuit_mode_data,
                            reserved,          sndcp_service,     air_interface_encryption, advanced_link};
    uint16_t v = 0;
    for (bool f : flags)
        v = static_cast<uint16_t>((v << 1) | (f ? 1u : 0u));
    return v;
}

uint32_t ExtendedServices::pack() const
{
    BitWriter w;
    w.put(security_information, 8, "security information");
    w.put(sds_tl_addressing_method, 2, "SDS-TL addressing method");
    w.bit(gck_supported);
    w.put(section, 2, "extended services section");
    w.put(section_information, 7, "section information");
    return static_cast<uint32_t>(get_bits(w.bits(), 0, 20));
}

// 21.4.4.1 MAC-SYSINFO + 18.4.2.2 D-MLE-SYSINFO.
Bits encode_sysinfo(const MacSysinfo& p)
{
    BitWriter w;
    w.put(0b10, 2);  // MAC PDU type: broadcast
    w.put(0b00, 2);  // broadcast type: SYSINFO
    w.put(p.main_carrier, 12, "main carrier");
    w.put(p.frequency_band, 4, "frequency band");
    w.put(p.offset, 2, "offset");
    w.put(p.duplex_spacing, 3, "duplex spacing");
    w.bit(p.reverse_operation);
    w.put(p.num_common_secondary_cch, 2, "number of common secondary control channels");
    w.put(p.ms_txpwr_max_cell, 3, "MS_TXPWR_MAX_CELL");
    w.put(p.rxlev_access_min, 4, "RXLEV_ACCESS_MIN");
    w.put(p.access_parameter, 4, "ACCESS_PARAMETER");
    w.put(p.radio_downlink_timeout, 4, "RADIO_DOWNLINK_TIMEOUT");
    w.bit(p.cck_flag);
    w.put(p.hyperframe_or_cck, 16);
    w.put(static_cast<uint8_t>(p.option), 2, "optional field flag");
    w.put(p.option_value, 20, "option value");
    w.put(p.mle.location_area, 14, "location area");
    w.put(p.mle.subscriber_class, 16);
    w.put(p.mle.bs_service_details.pack(), 12);
    return w.take();
}

// 21.4.7.2 ACCESS-ASSIGN, downlink, header 00.
uint16_t encode_access_assign(const AccessAssign& p)
{
    BitWriter w;
    w.put(p.header, 2, "ACCESS-ASSIGN header");
    const auto access = [&](const AccessField& f) {
        w.put(f.access_code, 2, "access code");
        w.put(f.base_frame_length, 4, "base frame length");
    };
    if (p.header == 0)
        access(p.field1);
    else
        w.put(p.dl_usage, 6, "DL usage marker");
    if (p.header == 3)
        w.put(p.ul_usage, 6, "UL usage marker");
    else
        access(p.field2);
    return static_cast<uint16_t>(get_bits(w.bits(), 0, 14));
}

// 21.4.3.1 MAC-RESOURCE with address type 000: the Null PDU.
Bits encode_null_pdu()
{
    BitWriter w;
    w.put(0b00, 2);      // MAC PDU type: MAC-RESOURCE
    w.bit(false);        // fill bit indication
    w.bit(false);        // position of grant
    w.put(0b00, 2);      // encryption mode: not encrypted
    w.bit(false);        // random access flag
    w.put(2, 6);         // length indication: 2 octets
    w.put(0b000, 3);     // address type: Null PDU
    return w.take();
}

Bits null_block(int block_bits)
{
    if (block_bits < kNullPduBits)
        throw std::invalid_argument("null_block: block too small");
    Bits b = encode_null_pdu();
    b.resize(static_cast<size_t>(block_bits), 0);
    return b;
}

int max_tm_sdu_bits(int block_bits)
{
    return std::min(block_bits / 8, 34) * 8 - kMacResourceHeaderBits;
}

Bits mac_resource_block(const Bits& tm_sdu, int block_bits)
{
    return mac_resource_block(tm_sdu, block_bits, ResourceOptions{});
}

Bits encode_channel_allocation(const ChannelAllocation& c)
{
    if (c.up_downlink == 0)
        throw std::invalid_argument("channel allocation: up/downlink 0 (augmented) not supported");
    BitWriter w;
    w.put(c.allocation_type, 2, "allocation type");
    w.put(c.timeslots, 4, "timeslot assigned");
    w.put(c.up_downlink, 2, "up/downlink assigned");
    w.bit(false);  // CLCH permission
    w.bit(c.cell_change);
    w.put(c.carrier, 12, "carrier number");
    w.bit(false);  // extended carrier numbering flag
    w.put(c.monitoring_pattern, 2, "monitoring pattern");
    if (c.monitoring_pattern == 0)
        w.put(0, 2);  // frame-18 monitoring pattern
    return w.take();
}

std::vector<Bits> fragment_blocks(const Bits& tm_sdu, int block_bits, const ResourceOptions& o,
                                  const std::optional<ChannelAllocation>& end_allocation)
{
    const int sdu = static_cast<int>(tm_sdu.size());
    if (sdu <= max_tm_sdu_bits(block_bits))
        throw std::invalid_argument("fragment_blocks: the TM-SDU fits one block");
    std::vector<Bits> out;
    // MAC-RESOURCE, length indication 63: start of fragmentation; the
    // TM-SDU fills the rest of the block.
    BitWriter w;
    w.put(0b00, 2);
    w.bit(false);  // fill bit indication
    w.bit(false);  // position of grant
    w.put(o.encryption_mode, 2, "encryption mode");
    w.bit(false);  // random access flag
    w.put(63, 6);
    w.put(0b001, 3);
    w.put(kBroadcastSsi, 24);
    w.bit(false);
    w.bit(false);
    w.bit(false);
    int at = block_bits - kMacResourceHeaderBits;
    w.append(Bits(tm_sdu.begin(), tm_sdu.begin() + at));
    out.push_back(w.take());

    const Bits ca = end_allocation ? encode_channel_allocation(*end_allocation) : Bits{};
    const int end_header = 13 + static_cast<int>(ca.size());
    const int end_max = std::min(block_bits, 34 * 8) - end_header - 1;  // room for a fill bit
    while (sdu - at > end_max) {
        // MAC-FRAG: fills its block; leaves at least 8 bits for the end.
        const int n = std::min(block_bits - 4, sdu - at - 8);
        BitWriter f;
        f.put(0b01, 2);
        f.bit(false);  // subtype: MAC-FRAG
        f.bit(n < block_bits - 4);
        f.append(Bits(tm_sdu.begin() + at, tm_sdu.begin() + at + n));
        if (n < block_bits - 4) {
            f.bit(true);
            f.put(0, block_bits - 4 - n - 1);
        }
        out.push_back(f.take());
        at += n;
    }
    const int rest = sdu - at;
    const int octets = (end_header + rest + 7) / 8;
    const int fill = octets * 8 - end_header - rest;
    BitWriter e;
    e.put(0b01, 2);
    e.bit(true);  // subtype: MAC-END
    e.bit(fill > 0);
    e.bit(false);  // position of grant
    e.put(static_cast<unsigned>(octets), 6);
    e.bit(false);  // slot granting flag
    e.bit(end_allocation.has_value());
    e.append(ca);
    e.append(Bits(tm_sdu.begin() + at, tm_sdu.end()));
    if (fill > 0) {
        e.bit(true);
        e.put(0, fill - 1);
    }
    if (block_bits - octets * 8 >= kNullPduBits)
        e.append(encode_null_pdu());
    Bits b = e.take();
    b.resize(static_cast<size_t>(block_bits), 0);
    out.push_back(std::move(b));
    return out;
}

Bits mac_resource_block(const Bits& tm_sdu, int block_bits, const ResourceOptions& o)
{
    const int sdu = static_cast<int>(tm_sdu.size());
    if (sdu > max_tm_sdu_bits(block_bits))
        throw std::length_error("mac_resource_block: TM-SDU of " + std::to_string(sdu) +
                                " bits does not fit (max " +
                                std::to_string(max_tm_sdu_bits(block_bits)) + ")");
    const int pdu = kMacResourceHeaderBits + sdu;
    const int octets = (pdu + 7) / 8;
    const int fill = octets * 8 - pdu;

    BitWriter w;
    w.put(0b00, 2);          // MAC PDU type: MAC-RESOURCE
    w.bit(fill > 0);         // fill bit indication
    w.bit(false);            // position of grant
    w.put(o.encryption_mode, 2, "encryption mode");
    w.bit(false);            // random access flag
    w.put(static_cast<unsigned>(octets), 6);  // length indication (octets)
    w.put(0b001, 3);         // address type: SSI
    w.put(kBroadcastSsi, 24);
    w.bit(false);            // power control flag
    w.bit(false);            // slot granting flag
    w.bit(false);            // channel allocation flag
    w.append(tm_sdu);
    if (fill > 0) {
        w.bit(true);
        w.put(0, fill - 1);
    }
    if (block_bits - octets * 8 >= kNullPduBits)
        w.append(encode_null_pdu());
    Bits b = w.take();
    b.resize(static_cast<size_t>(block_bits), 0);
    return b;
}

// 21.2.1: LLC PDU type 0010 = BL-UDATA without FCS.
Bits llc_bl_udata(const Bits& mle_pdu)
{
    BitWriter w;
    w.put(0b0010, 4);
    w.append(mle_pdu);
    return w.take();
}

uint32_t llc_fcs(const Bits& tl_sdu)
{
    // CRC-32 (polynomial 0x04C11DB7), register preset to all ones, sent
    // inverted, most significant bit first.
    uint32_t crc = 0xFFFFFFFFu;
    for (const uint8_t b : tl_sdu) {
        const bool top = ((crc >> 31) ^ (b & 1u)) != 0;
        crc <<= 1;
        if (top)
            crc ^= 0x04C11DB7u;
    }
    return ~crc;
}

Bits llc_bl_udata_fcs(const Bits& mle_pdu, bool corrupt_fcs)
{
    BitWriter w;
    w.put(0b0110, 4);
    w.append(mle_pdu);
    w.put(llc_fcs(mle_pdu) ^ (corrupt_fcs ? 1u : 0u), 32);
    return w.take();
}

int neighbour_cell_bits(const NeighbourCell& n)
{
    BitWriter w;
    put_neighbour(w, n);
    return static_cast<int>(w.size());
}

// 18.4.1.4.1 D-NWRK-BROADCAST.
Bits encode_nwrk_broadcast(const NwrkBroadcast& p)
{
    BitWriter w;
    w.put(0b101, 3);  // protocol discriminator: MLE
    w.put(0b010, 3);  // PDU type: D-NWRK-BROADCAST
    w.put(p.reselect.slow_threshold, 4, "slow reselect threshold");
    w.put(p.reselect.slow_hysteresis, 4, "slow reselect hysteresis");
    w.put(p.reselect.fast_threshold, 4, "fast reselect threshold");
    w.put(p.reselect.fast_hysteresis, 4, "fast reselect hysteresis");
    w.put(p.cell_load, 2, "cell load CA");
    const bool o = p.network_time || p.neighbours;
    w.bit(o);
    if (o) {
        put_optional(w, p.network_time, [&](const NetworkTime& t) { put_network_time(w, t); });
        put_optional(w, p.neighbours, [&](const std::vector<NeighbourCell>& list) {
            w.put(list.size(), 3, "number of neighbour cells");
            for (const NeighbourCell& n : list)
                put_neighbour(w, n);
        });
    }
    return w.take();
}

Bits nwrk_broadcast_block(const NwrkBroadcast& p)
{
    return mac_resource_block(llc_bl_udata(encode_nwrk_broadcast(p)), kSchF.type1);
}

std::vector<NwrkBroadcast> split_nwrk_broadcast(const NwrkBroadcast& all)
{
    if (!all.neighbours || all.neighbours->empty()) {
        nwrk_broadcast_block(all);  // throws when it does not fit
        return {all};
    }
    const auto pdu_with = [&all](const std::vector<NeighbourCell>& list) {
        NwrkBroadcast p;
        p.reselect = all.reselect;
        p.cell_load = all.cell_load;
        p.network_time = all.network_time;
        p.neighbours = list;
        return p;
    };
    const auto fits = [&pdu_with](const std::vector<NeighbourCell>& list) {
        return static_cast<int>(llc_bl_udata(encode_nwrk_broadcast(pdu_with(list))).size()) <=
               max_tm_sdu_bits(kSchF.type1);
    };
    std::vector<NwrkBroadcast> out;
    std::vector<NeighbourCell> cur;
    for (const NeighbourCell& n : *all.neighbours) {
        cur.push_back(n);
        if (cur.size() <= 7 && fits(cur))
            continue;
        cur.pop_back();
        if (cur.empty())
            throw std::length_error("split_nwrk_broadcast: one neighbour does not fit a block");
        out.push_back(pdu_with(cur));
        cur.assign(1, n);
        if (!fits(cur))
            throw std::length_error("split_nwrk_broadcast: one neighbour does not fit a block");
    }
    out.push_back(pdu_with(cur));
    return out;
}

}  // namespace yantra::gen
