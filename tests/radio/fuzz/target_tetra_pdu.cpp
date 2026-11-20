// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target tetra_pdu (RAD-96): every bit-level parser of the TETRA
// decoder on one input - MAC-SYNC/D-MLE-SYNC, MAC-SYSINFO (protocol crate
// and osmo-tetra), ACCESS-ASSIGN, the RM(30,14) decoder, the MAC PDU
// classifier, the MAC PDUs of a block (MAC-RESOURCE / MAC-FRAG / MAC-END
// -> LLC -> MLE/MM), a whole TM-SDU and the field-by-field text: the
// wrapper and, through its C ABI, the Rust protocol crate.
// Input: byte 0 = options (frame 18, describe offset), then the bits
// packed MSB first. The same format as the Rust crate's fuzz entry
// yantra_proto::fuzz::tetra_pdu (receiver/rust/fuzz).
// Copied from yantra 68ea20a: tests/fuzz/target_tetra_pdu.cpp, adapted
// (receiver namespace and names).

#include "fuzz.hpp"

#include "yantra/proto.h"
#include "yantra/receiver/broadcast.hpp"
#include "yantra/receiver/lower_mac.hpp"
#include "yantra/receiver/mle.hpp"

#include <algorithm>

namespace dec = yantra::receiver::tetra;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return 0;
    const uint8_t opt = data[0];
    // At most a few TM-SDUs worth of bits.
    const std::vector<uint8_t> bits = yantra::fuzz::unpack(data + 1, std::min<size_t>(size - 1, 1024));
    const int n = static_cast<int>(bits.size());
    std::vector<uint8_t> pad = bits;
    pad.resize(std::max<size_t>(bits.size(), 268), 0);  // fixed-size parsers read up to a SCH/F block

    if (auto s = dec::parse_sync(pad.data()))
        if (s->timeslot < 1 || s->timeslot > 4 || s->frame < 1 || s->frame > 18 || s->multiframe < 1 ||
            s->multiframe > 60)
            fail("MAC-SYNC time out of range");
    const auto osmo = dec::parse_sysinfo(pad.data());
    const auto proto = dec::parse_sysinfo_proto(pad.data());
    if (osmo.has_value() != proto.has_value())
        fail("osmo-tetra and the protocol crate disagree on a SYSINFO");
    if (osmo && proto && osmo->main_carrier != proto->main_carrier)
        fail("SYSINFO main carrier differs between the parsers");
    (void)dec::parse_access_assign(pad.data(), (opt & 1) != 0);
    if (size >= 5) {
        const uint32_t w = ((uint32_t(data[1]) << 24) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 8) | data[4]) &
                           0x3FFFFFFFu;
        if (auto r = dec::rm3014_decode(w); r && (r->second < 0 || r->second > 3))
            fail("RM(30,14) corrected more than 3 bits");
    }
    const int block = std::min(n, 268);
    for (dec::LogicalChannel c : {dec::LogicalChannel::Bsch, dec::LogicalChannel::Bnch, dec::LogicalChannel::SchF,
                                  dec::LogicalChannel::SchHd, dec::LogicalChannel::Stch})
        (void)dec::parse_mac_pdu(pad.data(), block, c);

    int prev = -1;
    for (const dec::BlockPdu& p : dec::parse_block_pdus(bits.data(), block)) {
        if (p.offset <= prev || p.offset >= block)
            fail("block PDU offsets not increasing inside the block");
        if (p.sdu_start < 0 || p.sdu_start > p.sdu_end || p.sdu_end > block)
            fail("TM-SDU range outside the block");
        prev = p.offset;
        (void)p.summary();
        if (p.channel_allocation)
            (void)p.channel_allocation->text();
        if (p.nwrk && p.nwrk->neighbours.size() > 7)
            fail("more than 7 neighbours in one D-NWRK-BROADCAST");
        if (p.nwrk && p.nwrk->network_time)
            (void)p.nwrk->network_time->text();
        if (p.nwrk)
            for (const dec::NeighbourInfo& nb : p.nwrk->neighbours)
                (void)nb.downlink_hz(opt & 15, opt >> 4);
    }
    // The crate's C ABI directly: a Rust panic is caught at the boundary
    // and reported as YANTRA_PROTO_ERR_PANIC, which is a bug here.
    const auto nb = static_cast<uint32_t>(n);
    yantra_proto_tetra_sysinfo si{};
    si.struct_size = sizeof si;
    if (yantra_proto_tetra_parse_sysinfo(pad.data(), 124, &si) == YANTRA_PROTO_ERR_PANIC)
        fail("protocol crate panicked (sysinfo)");
    uint32_t at = static_cast<uint32_t>((opt >> 1) % (block + 1));
    for (int guard = 0; guard < 64; ++guard) {
        yantra_proto_tetra_dl_pdu c{};
        c.struct_size = sizeof c;
        const int32_t st = yantra_proto_tetra_parse_dl_pdu(bits.data(), static_cast<uint32_t>(block), at, &c);
        if (st == YANTRA_PROTO_ERR_PANIC)
            fail("protocol crate panicked (dl_pdu)");
        if (st != YANTRA_PROTO_OK || c.next_offset == 0)
            break;
        if (c.next_offset <= at)
            fail("next_offset does not move on");
        at = c.next_offset;
    }
    yantra_proto_tetra_dl_pdu t{};
    t.struct_size = sizeof t;
    if (yantra_proto_tetra_parse_tm_sdu(bits.data(), nb, &t) == YANTRA_PROTO_ERR_PANIC)
        fail("protocol crate panicked (tm_sdu)");
    char text[64];
    uint32_t needed = 0;
    for (uint32_t kind = 0; kind <= 5; ++kind)
        if (yantra_proto_tetra_describe(kind, bits.data(), nb, nb ? (opt >> 1) % (nb + 1) : 0, text, sizeof text,
                                        &needed) == YANTRA_PROTO_ERR_PANIC)
            fail("protocol crate panicked (describe)");
    if (dec::carrier_downlink_hz_proto(data[size - 1] & 15, static_cast<uint16_t>(opt << 4 | data[size - 1]), opt & 3) !=
        dec::carrier_downlink_hz(data[size - 1] & 15, static_cast<uint16_t>(opt << 4 | data[size - 1]), opt & 3))
        fail("downlink frequency differs between the crate and the receiver");
    (void)yantra_proto_tetra_downlink_hz(data[size - 1], static_cast<uint16_t>(opt << 4 | data[size - 1]), opt & 3);
    (void)yantra_proto_tetra_duplex_spacing_hz(data[size - 1] & 15, opt & 7);

    const dec::BlockPdu whole = dec::parse_tm_sdu(bits);
    if (whole.sdu_status == YANTRA_PROTO_ERR_PANIC)
        fail("protocol crate panicked (TM-SDU)");
    (void)whole.summary();
    for (dec::DescribeKind k :
         {dec::DescribeKind::Sync, dec::DescribeKind::Sysinfo, dec::DescribeKind::DlPdu, dec::DescribeKind::TmSdu})
        (void)dec::describe(k, bits.data(), n, n > 0 ? (opt >> 1) % (n + 1) : 0);
    return 0;
}
