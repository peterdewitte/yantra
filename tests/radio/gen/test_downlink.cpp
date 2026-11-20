// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Downlink logical-channel schedule of the generator (RAD-70): which PDU
// goes into which slot, the timing fields filled in per slot, and every
// coded block decoding with the test-only reference decoder and parsers.

#include "ref_decoder.hpp"
#include "ref_pdu.hpp"

#include "yantra/gen/coding.hpp"
#include "yantra/gen/downlink.hpp"

#include <doctest/doctest.h>

#include <stdexcept>

using namespace yantra::gen;

namespace {

CellConfig cell_with_neighbours()
{
    CellConfig c = CellConfig::defaults();
    NwrkBroadcast nb;
    NeighbourCell n;
    n.cell_id = 3;
    n.main_carrier = 3604;
    n.location_area = 2;
    nb.neighbours = std::vector<NeighbourCell>{n};
    c.nwrk_broadcast = nb;
    return c;
}

}  // namespace

TEST_CASE("Downlink frequency from band, carrier and offset")
{
    CHECK(downlink_frequency_hz(3, 3600) == 390.0e6);
    CHECK(downlink_frequency_hz(3, 3711, 0) == 392.775e6);
    CHECK(downlink_frequency_hz(4, 0, 1) == 400.00625e6);
    CHECK(downlink_frequency_hz(4, 0, 2) == 399.99375e6);
    CHECK(downlink_frequency_hz(4, 0, 3) == 400.0125e6);
    CHECK_THROWS_AS(downlink_frequency_hz(16, 0), std::out_of_range);
    CHECK_THROWS_AS(downlink_frequency_hz(3, 4096), std::out_of_range);
    CHECK_THROWS_AS(downlink_frequency_hz(3, 0, 4), std::out_of_range);
}

TEST_CASE("TDMA time advances slot, frame, multiframe and hyperframe")
{
    TdmaTime t{4, 18, 60, 65535};
    CHECK(t.valid());
    t.advance();
    CHECK(t == TdmaTime{1, 1, 1, 0});
    t = {4, 17, 3, 9};
    t.advance();
    CHECK(t == TdmaTime{1, 18, 3, 9});
    CHECK_FALSE(TdmaTime{0, 1, 1, 0}.valid());
    CHECK_FALSE(TdmaTime{1, 19, 1, 0}.valid());
    CHECK_FALSE(TdmaTime{1, 1, 61, 0}.valid());
}

TEST_CASE("Frame 18: every slot carries SYNC with its own TN/FN/MN and SYSINFO with the hyperframe")
{
    const CellConfig c = CellConfig::defaults();
    const uint32_t cell = c.scrambling();
    CHECK(cell == scrambling_init(901, 1, 1));
    for (uint8_t tn = 1; tn <= 4; ++tn) {
        CAPTURE(int(tn));
        const TdmaTime t{tn, 18, 37, 1234};
        const SlotBlocks b = downlink_blocks(c, t);
        CHECK(b.content == SlotContent::Sync);
        REQUIRE(b.block1.size() == 120);
        REQUIRE(b.block2.size() == 216);
        REQUIRE(b.aach.size() == 30);
        const auto sb1 = ref::decode_block(b.block1, kBsch, kScramblingInitBsch);
        REQUIRE(sb1.crc_ok);
        const MacSync s = ref::parse_sync(sb1.type1);
        CHECK(s.timeslot == tn);
        CHECK(s.frame == 18);
        CHECK(s.multiframe == 37);
        CHECK(s.colour_code == 1);
        CHECK(s.mle.mcc == 901);
        CHECK(s.mle.mnc == 1);
        const auto sb2 = ref::decode_block(b.block2, kSchHd, cell);
        REQUIRE(sb2.crc_ok);
        const MacSysinfo si = ref::parse_sysinfo(sb2.type1);
        CHECK(si.hyperframe_or_cck == 1234);
        CHECK(si.main_carrier == 3600);
        CHECK(si.mle.location_area == 1);
        CHECK(ref::parse_access_assign(ref::decode_aach(b.aach, cell)) == c.access_assign);
    }
    // With the CCK flag the field is sent as configured.
    CellConfig k = c;
    k.sysinfo.cck_flag = true;
    k.sysinfo.hyperframe_or_cck = 0xBEEF;
    const SlotPdus p = downlink_pdus(k, TdmaTime{2, 18, 1, 5});
    CHECK(ref::parse_sysinfo(p.second).hyperframe_or_cck == 0xBEEF);
}

TEST_CASE("Frames 1..17: D-NWRK-BROADCAST on the MCCH every nwrk_every_frames, Null PDUs elsewhere")
{
    const CellConfig c = cell_with_neighbours();
    for (uint8_t fn = 1; fn <= 17; ++fn) {
        for (uint8_t tn = 1; tn <= 4; ++tn) {
            CAPTURE(int(fn));
            CAPTURE(int(tn));
            const SlotBlocks b = downlink_blocks(c, TdmaTime{tn, fn, 1, 0});
            const bool nwrk = tn == 1 && (fn == 1 || fn == 7 || fn == 13);
            CHECK(b.content == (nwrk ? SlotContent::NwrkBroadcast : SlotContent::Null));
            REQUIRE(b.block1.size() == 216);
            REQUIRE(b.block2.size() == 216);
            Bits sch_f = b.block1;
            sch_f.insert(sch_f.end(), b.block2.begin(), b.block2.end());
            const auto d = ref::decode_block(sch_f, kSchF, c.scrambling());
            REQUIRE(d.crc_ok);
            const auto pdus = ref::parse_mac_block(d.type1);
            REQUIRE_FALSE(pdus.empty());
            if (nwrk) {
                CHECK(pdus[0].ssi == kBroadcastSsi);
                CHECK(ref::parse_nwrk_broadcast_sdu(pdus[0].tm_sdu) == *c.nwrk_broadcast);
            } else {
                CHECK(pdus[0].address_type == 0);
            }
        }
    }
    CellConfig none = CellConfig::defaults();
    CHECK(downlink_pdus(none, TdmaTime{1, 1, 1, 0}).content == SlotContent::Null);
    none.nwrk_every_frames = 0;
    CHECK_THROWS_AS(downlink_pdus(none, TdmaTime{1, 1, 1, 0}), std::invalid_argument);
    CHECK_THROWS_AS(downlink_pdus(none, TdmaTime{5, 1, 1, 0}), std::invalid_argument);
}

TEST_CASE("A long neighbour list is sent as several D-NWRK-BROADCAST PDUs in turn")
{
    NwrkBroadcast all;
    std::vector<NeighbourCell> list;
    for (int i = 0; i < 31; ++i) {
        NeighbourCell n;
        n.cell_id = static_cast<uint8_t>(i + 1);
        n.main_carrier = static_cast<uint16_t>(3500 + i);
        n.location_area = static_cast<uint16_t>(100 + i);
        n.min_rx_access_level = 3;
        list.push_back(n);
    }
    all.neighbours = list;
    const std::vector<NwrkBroadcast> pdus = split_nwrk_broadcast(all);
    REQUIRE(pdus.size() >= 5);  // at most 7 neighbours per PDU
    CellConfig c = CellConfig::defaults();
    c.nwrk_broadcast = pdus.front();
    c.nwrk_more.assign(pdus.begin() + 1, pdus.end());
    validate(c);
    // Collect every neighbour sent in one hyperframe.
    std::vector<NeighbourCell> seen;
    TdmaTime t{1, 1, 1, 0};
    size_t n_pdus = 0;
    for (int i = 0; i < 4 * 18 * 60 && n_pdus < pdus.size(); ++i, t.advance()) {
        const SlotPdus p = downlink_pdus(c, t);
        if (p.content != SlotContent::NwrkBroadcast)
            continue;
        const auto mac = ref::parse_mac_block(p.first);
        const NwrkBroadcast got = ref::parse_nwrk_broadcast_sdu(mac.at(0).tm_sdu);
        CHECK(got == pdus[n_pdus]);
        seen.insert(seen.end(), got.neighbours->begin(), got.neighbours->end());
        ++n_pdus;
    }
    CHECK(seen == list);
}

TEST_CASE("Block sequences and the ACCESS-ASSIGN plan replace the defaults")
{
    CellConfig c = cell_with_neighbours();
    CellConfig::BlockSequence q;
    q.timeslot = 3;
    q.first_frame = 4;
    q.blocks = {null_block(kSchF.type1), nwrk_broadcast_block(*c.nwrk_broadcast)};
    c.block_sequences.push_back(q);
    c.access_assign_plan = {AccessAssign{{1, 2}, {2, 3}}, AccessAssign{{3, 4}, {0, 5}}};
    validate(c);
    CHECK(downlink_pdus(c, TdmaTime{3, 3, 1, 0}).content == SlotContent::Null);
    CHECK(downlink_pdus(c, TdmaTime{3, 4, 1, 0}).content == SlotContent::BlockSequence);
    CHECK(downlink_pdus(c, TdmaTime{3, 5, 1, 0}).first == q.blocks[1]);
    CHECK(downlink_pdus(c, TdmaTime{3, 6, 1, 0}).content == SlotContent::Null);
    // Slot k = (fn - 1) * 4 + tn - 1 sends plan entry k mod 2; frame 18 the default.
    CHECK(planned_access_assign(c, TdmaTime{1, 1, 1, 0}) == c.access_assign_plan[0]);
    CHECK(planned_access_assign(c, TdmaTime{2, 1, 1, 0}) == c.access_assign_plan[1]);
    CHECK(planned_access_assign(c, TdmaTime{1, 2, 1, 0}) == c.access_assign_plan[0]);
    CHECK(planned_access_assign(c, TdmaTime{2, 18, 1, 0}) == c.access_assign);
    const SlotBlocks b = downlink_blocks(c, TdmaTime{2, 1, 1, 0});
    CHECK(ref::parse_access_assign(ref::decode_aach(b.aach, c.scrambling())) == c.access_assign_plan[1]);

    CellConfig bad = c;
    bad.block_sequences[0].first_frame = 17;
    CHECK_THROWS_AS(validate(bad), std::out_of_range);
    bad = c;
    bad.block_sequences[0].blocks[0].pop_back();
    CHECK_THROWS_AS(validate(bad), std::length_error);
    bad = c;
    bad.sync.colour_code = 64;
    CHECK_THROWS_AS(validate(bad), std::out_of_range);
}

TEST_CASE("Every slot of a multiframe decodes without errors")
{
    const CellConfig c = cell_with_neighbours();
    TdmaTime t{1, 1, 5, 3};
    for (int i = 0; i < 4 * 18; ++i, t.advance()) {
        CAPTURE(i);
        const SlotBlocks b = downlink_blocks(c, t);
        int distance = -1;
        ref::decode_aach(b.aach, c.scrambling(), &distance);
        CHECK(distance == 0);
        if (b.content == SlotContent::Sync) {
            const auto d1 = ref::decode_block(b.block1, kBsch, kScramblingInitBsch);
            const auto d2 = ref::decode_block(b.block2, kSchHd, c.scrambling());
            CHECK(d1.crc_ok);
            CHECK(d1.corrected == 0);
            CHECK(d2.crc_ok);
            CHECK(d2.corrected == 0);
        } else {
            Bits sch_f = b.block1;
            sch_f.insert(sch_f.end(), b.block2.begin(), b.block2.end());
            const auto d = ref::decode_block(sch_f, kSchF, c.scrambling());
            CHECK(d.crc_ok);
            CHECK(d.corrected == 0);
        }
    }
}
