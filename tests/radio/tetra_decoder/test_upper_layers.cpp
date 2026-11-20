// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The upper layers of the carrier decoder end to end on generator signals
// (RAD-101, DEC-02..04, DEC-06, DEC-10): a 31-entry neighbour list split
// over several D-NWRK-BROADCAST PDUs, each sent fragmented (MAC-RESOURCE
// start, MAC-FRAG, MAC-END), merged back to the generator's ground truth;
// channel allocations counted per carrier and timeslot (DEC-06, counts
// only); a fragmented encrypted TM-SDU counted, its parts never kept; and
// the protocol crate's frequency helpers against broadcast.hpp's.

#include "network_check.hpp"
#include "signal.hpp"

#include "yantra/gen/pdu.hpp"
#include "yantra/receiver/fragment.hpp"
#include "yantra/receiver/mle.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <vector>

using namespace dectest;

namespace {

gen::NeighbourCell rich_neighbour(uint8_t id)
{
    gen::NeighbourCell n;
    n.cell_id = id;
    n.reselection_types = static_cast<uint8_t>(id % 4);
    n.synchronised = id % 2 == 0;
    n.cell_load = static_cast<uint8_t>(id % 4);
    n.main_carrier = static_cast<uint16_t>(3601 + id);
    if (id % 3 == 0)
        n.carrier_extension = gen::CarrierExtension{4, static_cast<uint8_t>(id % 4), 1, id % 2 != 0};
    if (id % 2 == 0)
        n.location_area = static_cast<uint16_t>(0x200 + id);
    n.max_ms_txpwr = static_cast<uint8_t>(1 + id % 7);
    n.min_rx_access_level = static_cast<uint8_t>(id % 16);
    if (id % 5 == 0)
        n.subscriber_class = static_cast<uint16_t>(0xF000 | id);
    return n;
}

/// Lays the block lists out as block sequences, one list per run of
/// consecutive frames 1..17 of one timeslot (fragments of one TM-SDU stay
/// on one timeslot, DEC-04).
void lay_out(gen::CellConfig& c, const std::vector<std::vector<gen::Bits>>& lists)
{
    int ts = 1;
    int frame = 1;
    for (const auto& blocks : lists) {
        REQUIRE(blocks.size() <= 17);
        if (frame + static_cast<int>(blocks.size()) - 1 > 17) {
            ++ts;
            frame = 1;
        }
        REQUIRE(ts <= 4);
        c.block_sequences.push_back({ts, frame, blocks});
        frame += static_cast<int>(blocks.size());
    }
}

}  // namespace

TEST_CASE("Frequency helpers of the protocol crate equal broadcast.hpp's")
{
    for (uint8_t band = 0; band < 16; ++band)
        for (uint8_t offset = 0; offset < 4; ++offset)
            for (uint16_t carrier = 0; carrier < 4096; carrier = static_cast<uint16_t>(carrier + 37))
                CHECK(dec::carrier_downlink_hz_proto(band, carrier, offset) ==
                      dec::carrier_downlink_hz(band, carrier, offset));
    for (uint8_t band = 0; band < 16; ++band)
        for (uint8_t duplex = 0; duplex < 8; ++duplex) {
            INFO("band ", int(band), " duplex ", int(duplex));
            CHECK(dec::duplex_spacing_hz_proto(band, duplex) == dec::duplex_spacing_hz(band, duplex));
        }
}

TEST_CASE("A 31-entry neighbour list in several PDUs, each fragmented, merges to the ground truth (DEC-02, DEC-04)")
{
    gen::NwrkBroadcast all;
    all.reselect = {6, 2, 11, 3};
    all.cell_load = 3;
    all.neighbours = std::vector<gen::NeighbourCell>{};
    for (uint8_t id = 1; id <= 31; ++id)
        all.neighbours->push_back(rich_neighbour(id));
    // Up to seven neighbours per PDU (the most one PDU holds), so the PDUs
    // are longer than one block (split_nwrk_broadcast would make each
    // fit); the sizes avoid TM-SDUs between one block and one fragment.
    std::vector<gen::NwrkBroadcast> pdus;
    std::size_t at = 0;
    for (const std::size_t n : {7u, 7u, 7u, 6u, 4u}) {
        gen::NwrkBroadcast p = all;
        p.neighbours = std::vector<gen::NeighbourCell>(all.neighbours->begin() + static_cast<std::ptrdiff_t>(at),
                                                       all.neighbours->begin() + static_cast<std::ptrdiff_t>(at + n));
        pdus.push_back(p);
        at += n;
    }
    REQUIRE(at == 31);

    CellSignal s;
    s.snr_db = 35.0;
    s.seconds = 3.3;  // two whole multiframes after the first frame 18
    std::vector<std::vector<gen::Bits>> lists;
    std::size_t fragmented = 0;
    for (const gen::NwrkBroadcast& p : pdus) {
        const gen::Bits sdu = gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(p));
        if (static_cast<int>(sdu.size()) <= gen::max_tm_sdu_bits(268)) {  // the short last PDU fits one block
            lists.push_back({gen::mac_resource_block(sdu, 268)});
            continue;
        }
        lists.push_back(gen::fragment_blocks(sdu, 268));
        ++fragmented;
    }
    CHECK(fragmented >= 4);
    lay_out(s.cell, lists);
    rx::CarrierDecoder d(test_config());
    rx::DecoderOutput out;
    const std::vector<cf> iq = make_cell(s);
    for (std::size_t i = 0; i < iq.size(); i += 3000)
        d.process(iq.data() + i, std::min<std::size_t>(3000, iq.size() - i), out);

    REQUIRE(d.network().has_value());
    check_network(*d.network(), pdus);
    REQUIRE(d.serving_cell().has_value());
    REQUIRE(d.serving_cell()->reselect.has_value());
    CHECK(*d.serving_cell()->reselect == dec::ReselectParams{6, 2, 11, 3});
    // Every neighbour's downlink: its own band/offset with an extension,
    // else the serving cell's.
    for (const dec::NeighbourInfo& n : d.network()->neighbours) {
        const gen::NeighbourCell g = rich_neighbour(n.cell_id);
        const uint64_t want = g.carrier_extension
                                  ? dec::carrier_downlink_hz(4, g.main_carrier, g.carrier_extension->offset)
                                  : dec::carrier_downlink_hz(3, g.main_carrier, 0);
        CHECK(n.downlink_hz(3, 0) == want);
    }
    // The fragmented D-NWRK-BROADCASTs arrive as reassembled TM-SDUs.
    std::size_t reassembled = 0;
    for (const dec::DecodedSlot& sl : out.slots)
        for (const dec::DecodedBlock& b : sl.result.blocks)
            for (const dec::BlockPdu& p : b.pdus)
                if (p.nwrk) {
                    CHECK(p.fcs == dec::Fcs::Ok);
                    reassembled += p.reassembled ? 1 : 0;
                }
    CHECK(reassembled >= fragmented);
    const dec::ReassemblyCounters f = d.reassembly_totals();
    CHECK(f.completed >= fragmented);
    CHECK(f.orphans <= pdus.size());  // only fragments before the first frame 18
    CHECK(f.lost_length == 0);
    uint64_t broadcasts = 0;
    for (const dec::PeriodReport& p : out.periods)
        broadcasts += p.nwrk_broadcasts;
    CHECK(broadcasts >= pdus.size());
    // The cell reports grew to the whole list.
    REQUIRE_FALSE(out.cells.empty());
    REQUIRE(out.cells.back().cell.network);
    CHECK(out.cells.back().cell.network->neighbours.size() == 31);
}

TEST_CASE("Channel allocations are counted per carrier and timeslot (DEC-06)")
{
    gen::NwrkBroadcast p;
    p.neighbours = std::vector<gen::NeighbourCell>{};
    for (uint8_t id = 1; id <= 7; ++id)
        p.neighbours->push_back(rich_neighbour(id));
    const gen::Bits sdu = gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(p));
    gen::ChannelAllocation a;
    a.carrier = 3605;
    a.timeslots = 0b0110;  // timeslots 2 and 3
    gen::ChannelAllocation b;
    b.carrier = 3611;
    b.timeslots = 0b1000;  // timeslot 1
    b.up_downlink = 1;
    gen::ChannelAllocation m;
    m.carrier = 3605;
    m.timeslots = 0;  // the MCCH sharing timeslots
    CellSignal s;
    s.snr_db = 35.0;
    s.seconds = 3.3;
    lay_out(s.cell, {gen::fragment_blocks(sdu, 268, {}, a), gen::fragment_blocks(sdu, 268, {}, b),
                     gen::fragment_blocks(sdu, 268, {}, m)});
    rx::CarrierDecoder d(test_config());
    rx::DecoderOutput out;
    const std::vector<cf> iq = make_cell(s);
    d.process(iq.data(), iq.size(), out);

    // What the decoder saw: every MAC-END with an allocation.
    struct Seen {
        uint64_t n3605 = 0, n3611 = 0, mcch = 0;
    } seen;
    for (const dec::DecodedSlot& sl : out.slots)
        for (const dec::DecodedBlock& blk : sl.result.blocks)
            for (const dec::BlockPdu& q : blk.pdus)
                if (q.channel_allocation && !q.reassembled) {
                    CHECK(q.fragment == dec::Fragment::End);
                    if (q.channel_allocation->carrier == 3611)
                        ++seen.n3611;
                    else if (q.channel_allocation->timeslots == 0)
                        ++seen.mcch;
                    else
                        ++seen.n3605;
                }
    CHECK(seen.n3605 >= 2);
    CHECK(seen.n3611 >= 2);
    CHECK(seen.mcch >= 2);

    const std::vector<dec::ChannelAssignmentCount> t = d.assignment_totals();
    REQUIRE(t.size() == 2);  // sorted by carrier
    CHECK(t[0].carrier == 3605);
    CHECK_FALSE(t[0].extension);
    CHECK(t[0].downlink_hz == dec::carrier_downlink_hz(3, 3605, 0));
    CHECK(t[0].allocations == seen.n3605 + seen.mcch);
    CHECK(t[0].timeslots == std::array<uint64_t, 4>{0, seen.n3605, seen.n3605, 0});
    CHECK(t[0].mcch_timeslots == seen.mcch);
    CHECK(t[1].carrier == 3611);
    CHECK(t[1].allocations == seen.n3611);
    CHECK(t[1].timeslots == std::array<uint64_t, 4>{seen.n3611, 0, 0, 0});
    // The periods add up to the totals.
    uint64_t allocations = 0;
    for (const dec::PeriodReport& r : out.periods)
        for (const dec::ChannelAssignmentCount& c : r.assignments)
            allocations += c.allocations;
    CHECK(allocations <= t[0].allocations + t[1].allocations);  // the open period is in the totals only
    CHECK(allocations > 0);
}

TEST_CASE("A fragmented encrypted TM-SDU is counted, its parts never kept (DEC-10, RAD-17)")
{
    gen::NwrkBroadcast p;
    p.neighbours = std::vector<gen::NeighbourCell>{};
    for (uint8_t id = 1; id <= 7; ++id)
        p.neighbours->push_back(rich_neighbour(id));
    const std::vector<gen::Bits> blocks =
        gen::fragment_blocks(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(p)), 268, {.encryption_mode = 2});
    REQUIRE(blocks.size() >= 2);
    CellSignal s;
    s.snr_db = 35.0;
    s.seconds = 3.3;
    s.cell.sysinfo.mle.bs_service_details.air_interface_encryption = true;
    lay_out(s.cell, {blocks});
    rx::CarrierDecoder d(test_config());
    rx::DecoderOutput out;
    const std::vector<cf> iq = make_cell(s);
    d.process(iq.data(), iq.size(), out);

    CHECK_FALSE(d.network().has_value());  // nothing decoded from it
    int whole = 0, parts = 0;
    for (const dec::DecodedSlot& sl : out.slots)
        for (const dec::DecodedBlock& b : sl.result.blocks) {
            if (b.channel != dec::LogicalChannel::SchF || !b.crc_ok)
                continue;
            for (const dec::BlockPdu& q : b.pdus) {
                if (q.reassembled) {
                    ++whole;
                    CHECK(q.encrypted);
                    CHECK_FALSE(q.nwrk);
                    CHECK(q.sdu_bits.empty());
                    CHECK(q.fragments == static_cast<int>(blocks.size()));
                    continue;
                }
                if (q.fragment == dec::Fragment::None)
                    continue;
                ++parts;
                // The start is cleared with its block; the MAC-FRAG and
                // MAC-END parts after it (or without a start seen) are
                // cleared by the reassembly.
                CHECK(std::all_of(b.bits.begin() + q.sdu_start, b.bits.begin() + q.sdu_end,
                                  [](uint8_t v) { return v == 0; }));
            }
        }
    CHECK(whole >= 2);
    CHECK(parts >= 2 * static_cast<int>(blocks.size()));
    const dec::ReassemblyCounters f = d.reassembly_totals();
    CHECK(f.completed >= 2);
    CHECK(f.lost_missing == 0);
    // The start block is the encrypted MAC PDU counted per DEC-10.
    REQUIRE(d.serving_cell().has_value());
    CHECK(d.serving_cell()->encrypted_blocks >= 2);
}
