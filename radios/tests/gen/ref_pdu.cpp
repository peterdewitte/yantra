// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test-only PDU parsers for the TETRA test-signal generator (RAD-70).

#include "ref_pdu.hpp"

#include <stdexcept>

namespace yantra::gen::ref {

uint64_t BitReader::get(int width)
{
    if (pos_ + static_cast<size_t>(width) > bits_.size())
        throw std::out_of_range("BitReader: read past the end");
    uint64_t v = 0;
    for (int i = 0; i < width; ++i)
        v = (v << 1) | bits_[pos_++];
    return v;
}

namespace {

template <typename T>
T as(uint64_t v)
{
    return static_cast<T>(v);
}

}  // namespace

MacSync parse_sync(const Bits& b)
{
    if (b.size() != 60)
        throw std::invalid_argument("parse_sync: 60 bits expected");
    BitReader r(b);
    MacSync p;
    p.system_code = as<uint8_t>(r.get(4));
    p.colour_code = as<uint8_t>(r.get(6));
    p.timeslot = as<uint8_t>(r.get(2) + 1);
    p.frame = as<uint8_t>(r.get(5));
    p.multiframe = as<uint8_t>(r.get(6));
    p.sharing_mode = as<uint8_t>(r.get(2));
    p.ts_reserved_frames = as<uint8_t>(r.get(3));
    p.u_plane_dtx = r.flag();
    p.frame18_extension = r.flag();
    if (r.get(1) != 0)
        throw std::invalid_argument("parse_sync: reserved bit set");
    p.mle.mcc = as<uint16_t>(r.get(10));
    p.mle.mnc = as<uint16_t>(r.get(14));
    p.mle.neighbour_cell_broadcast = as<uint8_t>(r.get(2));
    p.mle.cell_service_level = as<uint8_t>(r.get(2));
    p.mle.late_entry_supported = r.flag();
    return p;
}

BsServiceDetails parse_bs_service_details(uint16_t v)
{
    auto bit = [v](int n) { return ((v >> (11 - n)) & 1u) != 0; };  // n = 0 is sent first
    BsServiceDetails d;
    d.registration = bit(0);
    d.deregistration = bit(1);
    d.priority_cell = bit(2);
    d.no_minimum_mode = bit(3);
    d.migration = bit(4);
    d.system_wide_services = bit(5);
    d.voice_service = bit(6);
    d.circuit_mode_data = bit(7);
    d.reserved = bit(8);
    d.sndcp_service = bit(9);
    d.air_interface_encryption = bit(10);
    d.advanced_link = bit(11);
    return d;
}

ExtendedServices parse_extended_services(uint32_t v)
{
    ExtendedServices e;
    e.security_information = as<uint8_t>(v >> 12);
    e.sds_tl_addressing_method = as<uint8_t>((v >> 10) & 3u);
    e.gck_supported = ((v >> 9) & 1u) != 0;
    e.section = as<uint8_t>((v >> 7) & 3u);
    e.section_information = as<uint8_t>(v & 0x7Fu);
    return e;
}

MacSysinfo parse_sysinfo(const Bits& b)
{
    if (b.size() != 124)
        throw std::invalid_argument("parse_sysinfo: 124 bits expected");
    BitReader r(b);
    if (r.get(2) != 0b10 || r.get(2) != 0b00)
        throw std::invalid_argument("parse_sysinfo: not a broadcast SYSINFO PDU");
    MacSysinfo p;
    p.main_carrier = as<uint16_t>(r.get(12));
    p.frequency_band = as<uint8_t>(r.get(4));
    p.offset = as<uint8_t>(r.get(2));
    p.duplex_spacing = as<uint8_t>(r.get(3));
    p.reverse_operation = r.flag();
    p.num_common_secondary_cch = as<uint8_t>(r.get(2));
    p.ms_txpwr_max_cell = as<uint8_t>(r.get(3));
    p.rxlev_access_min = as<uint8_t>(r.get(4));
    p.access_parameter = as<uint8_t>(r.get(4));
    p.radio_downlink_timeout = as<uint8_t>(r.get(4));
    p.cck_flag = r.flag();
    p.hyperframe_or_cck = as<uint16_t>(r.get(16));
    p.option = static_cast<SysinfoOption>(r.get(2));
    p.option_value = as<uint32_t>(r.get(20));
    if (r.pos() != 82)
        throw std::logic_error("parse_sysinfo: MAC part is not 82 bits");
    p.mle.location_area = as<uint16_t>(r.get(14));
    p.mle.subscriber_class = as<uint16_t>(r.get(16));
    p.mle.bs_service_details = parse_bs_service_details(as<uint16_t>(r.get(12)));
    return p;
}

AccessAssign parse_access_assign(uint16_t v)
{
    if ((v >> 12) != 0)
        throw std::invalid_argument("parse_access_assign: header is not 00");
    AccessAssign a;
    a.field1.access_code = as<uint8_t>((v >> 10) & 3u);
    a.field1.base_frame_length = as<uint8_t>((v >> 6) & 0xFu);
    a.field2.access_code = as<uint8_t>((v >> 4) & 3u);
    a.field2.base_frame_length = as<uint8_t>(v & 0xFu);
    return a;
}

std::vector<MacResource> parse_mac_block(const Bits& block)
{
    std::vector<MacResource> out;
    size_t start = 0;
    while (block.size() - start >= 16) {
        const Bits rest(block.begin() + static_cast<long>(start), block.end());
        BitReader r(rest);
        if (r.get(2) != 0b00)
            throw std::invalid_argument("parse_mac_block: not a MAC-RESOURCE");
        MacResource m;
        m.fill_bits = r.flag();
        m.grant_position = r.flag();
        m.encryption_mode = as<unsigned>(r.get(2));
        m.random_access = r.flag();
        m.length_octets = as<unsigned>(r.get(6));
        m.address_type = as<unsigned>(r.get(3));
        if (m.length_octets < 2 || m.length_octets * 8 > rest.size())
            throw std::invalid_argument("parse_mac_block: bad length indication");
        if (m.address_type == 0) {
            out.push_back(m);
            break;  // Null PDU: nothing else in this block
        }
        if (m.address_type != 0b001)
            throw std::invalid_argument("parse_mac_block: only SSI addresses expected");
        m.ssi = as<uint32_t>(r.get(24));
        if (r.flag() || r.flag() || r.flag())
            throw std::invalid_argument("parse_mac_block: power/slot/channel flags not expected");
        size_t end = m.length_octets * 8;
        if (m.fill_bits) {
            // Fill bits: a 1 then 0s at the end of the PDU.
            while (end > r.pos() && rest[end - 1] == 0)
                --end;
            if (end == r.pos())
                throw std::invalid_argument("parse_mac_block: fill bits without a 1");
            --end;
        }
        m.tm_sdu.assign(rest.begin() + static_cast<long>(r.pos()), rest.begin() + static_cast<long>(end));
        out.push_back(m);
        start += m.length_octets * 8;
    }
    return out;
}

namespace {

CellReselectParameters parse_reselect(BitReader& r)
{
    CellReselectParameters c;
    c.slow_threshold = as<uint8_t>(r.get(4));
    c.slow_hysteresis = as<uint8_t>(r.get(4));
    c.fast_threshold = as<uint8_t>(r.get(4));
    c.fast_hysteresis = as<uint8_t>(r.get(4));
    return c;
}

NeighbourCell parse_neighbour(BitReader& r)
{
    NeighbourCell n;
    n.cell_id = as<uint8_t>(r.get(5));
    n.reselection_types = as<uint8_t>(r.get(2));
    n.synchronised = r.flag();
    n.cell_load = as<uint8_t>(r.get(2));
    n.main_carrier = as<uint16_t>(r.get(12));
    if (!r.flag())
        return n;
    if (r.flag()) {
        CarrierExtension c;
        c.frequency_band = as<uint8_t>(r.get(4));
        c.offset = as<uint8_t>(r.get(2));
        c.duplex_spacing = as<uint8_t>(r.get(3));
        c.reverse_operation = r.flag();
        n.carrier_extension = c;
    }
    if (r.flag())
        n.mcc = as<uint16_t>(r.get(10));
    if (r.flag())
        n.mnc = as<uint16_t>(r.get(14));
    if (r.flag())
        n.location_area = as<uint16_t>(r.get(14));
    if (r.flag())
        n.max_ms_txpwr = as<uint8_t>(r.get(3));
    if (r.flag())
        n.min_rx_access_level = as<uint8_t>(r.get(4));
    if (r.flag())
        n.subscriber_class = as<uint16_t>(r.get(16));
    if (r.flag())
        n.bs_service_details = parse_bs_service_details(as<uint16_t>(r.get(12)));
    if (r.flag())
        n.timeshare_security = as<uint8_t>(r.get(5));
    if (r.flag())
        n.tdma_frame_offset = as<uint8_t>(r.get(6));
    return n;
}

}  // namespace

NwrkBroadcast parse_nwrk_broadcast_sdu(const Bits& sdu)
{
    BitReader r(sdu);
    if (r.get(4) != 0b0010)
        throw std::invalid_argument("not LLC BL-UDATA");
    if (r.get(3) != 0b101 || r.get(3) != 0b010)
        throw std::invalid_argument("not MLE D-NWRK-BROADCAST");
    NwrkBroadcast p;
    p.reselect = parse_reselect(r);
    p.cell_load = as<uint8_t>(r.get(2));
    if (r.flag()) {
        if (r.flag()) {
            NetworkTime t;
            t.utc_time = as<uint32_t>(r.get(24));
            t.local_offset_negative = r.flag();
            t.local_offset = as<uint8_t>(r.get(6));
            t.year = as<uint8_t>(r.get(6));
            t.reserved = as<uint16_t>(r.get(11));
            p.network_time = t;
        }
        if (r.flag()) {
            const auto count = r.get(3);
            std::vector<NeighbourCell> list;
            for (uint64_t i = 0; i < count; ++i)
                list.push_back(parse_neighbour(r));
            p.neighbours = list;
        }
    }
    if (r.left() != 0)
        throw std::invalid_argument("parse_nwrk_broadcast_sdu: trailing bits");
    return p;
}

}  // namespace yantra::gen::ref
