// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fragment reassembly and the upper layers behind it (DEC-04, DEC-05,
// DEC-06, DEC-10, DEC-11): fragmented TM-SDUs from the generator put
// together in order, a missing fragment, a timeout counted in slots,
// interleaved timeslots, a new start, the length limit, orphans; the LLC
// FCS right and wrong; encrypted TM-SDUs only counted, MM PDUs named and
// CMCE PDUs only typed (RAD-17); and over the air: fragments, counters
// and the AACH of every slot against the generator's plan.
// Copied from yantra 68ea20a: tests/tetra_decoder/test_fragments.cpp,
// adapted (no SDS counter, test-only MM/CMCE encoders).

#include "signal.hpp"

#include "yantra/receiver/fragment.hpp"
#include "yantra/receiver/mle.hpp"
#include "yantra/gen/pdu.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <string>

using namespace dectest;

namespace {

gen::NwrkBroadcast long_nwrk(int count, uint8_t first_id = 1)
{
    gen::NwrkBroadcast p;
    p.cell_load = 2;
    p.neighbours.emplace();
    for (int i = 0; i < count; ++i) {
        gen::NeighbourCell n;
        n.cell_id = static_cast<uint8_t>(first_id + i);
        n.reselection_types = 1;
        n.main_carrier = static_cast<uint16_t>(3500 + first_id + i);
        n.carrier_extension = gen::CarrierExtension{4, 2, 1, true};
        n.mcc = 901;
        n.mnc = 77;
        n.location_area = static_cast<uint16_t>(0x100 + i);
        n.max_ms_txpwr = 6;
        n.min_rx_access_level = 9;
        n.subscriber_class = 0xA5F0;
        p.neighbours->push_back(n);
    }
    return p;
}

// Test-only MM and CMCE PDUs (the generator has no such encoders, RAD-17):
// they check that the decoder names or types them and never shows an
// address. Copied from yantra 68ea20a: plugins/tetra/gen/src/pdu.cpp.
gen::Bits mm_location_update_reject(uint8_t location_update_type, uint8_t reject_cause)
{
    gen::BitWriter w;
    w.put(1, 3, "protocol discriminator: MM");
    w.put(0b0111, 4, "D-LOCATION UPDATE REJECT");
    w.put(location_update_type, 3, "location update type");
    w.put(reject_cause, 5, "reject cause");
    w.put(0, 1, "cipher control");
    w.put(0, 1, "optional bit: no address extension, no elements");
    return w.bits();
}

gen::Bits cmce_typed(uint8_t pdu_type, int payload_bits)
{
    gen::BitWriter w;
    w.put(2, 3, "protocol discriminator: CMCE");
    w.put(pdu_type, 5, "CMCE PDU type");
    gen::Bits b = w.bits();
    b.resize(b.size() + static_cast<std::size_t>(payload_bits), 0);
    return b;
}

std::vector<gen::Bits> fragmented(const gen::NwrkBroadcast& p, const gen::ResourceOptions& o = {},
                                  const std::optional<gen::ChannelAllocation>& ca = std::nullopt,
                                  bool corrupt_fcs = false)
{
    return gen::fragment_blocks(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(p), corrupt_fcs), 268, o, ca);
}

/// Feeds every MAC PDU of `block`; the TM-SDU completed by it, if any.
std::optional<dec::BlockPdu> feed(dec::Reassembler& r, int ts, uint64_t slot, const gen::Bits& block)
{
    std::optional<dec::BlockPdu> out;
    for (const dec::BlockPdu& p : dec::parse_block_pdus(block.data(), static_cast<int>(block.size())))
        if (auto w = r.push(ts, slot, block.data(), p))
            out = std::move(w);
    return out;
}

}  // namespace

TEST_CASE("fragments: in order, with FCS and a channel allocation")
{
    const gen::NwrkBroadcast p = long_nwrk(7);
    gen::ChannelAllocation ca;
    ca.allocation_type = 1;
    ca.timeslots = 0b0110;
    ca.up_downlink = 3;
    ca.carrier = 3612;
    const auto blocks = fragmented(p, {}, ca);
    REQUIRE(blocks.size() >= 3);  // start, MAC-FRAG(s), MAC-END
    const auto first = dec::parse_block_pdus(blocks[0].data(), 268);
    REQUIRE_FALSE(first.empty());
    CHECK(first[0].fragment == dec::Fragment::Start);
    CHECK(first[0].summary() == "first fragment");
    dec::Reassembler r;
    std::optional<dec::BlockPdu> whole;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        whole = feed(r, 1, 10 + 4 * i, blocks[i]);
        CHECK(whole.has_value() == (i + 1 == blocks.size()));
    }
    REQUIRE(whole);
    CHECK(whole->reassembled);
    CHECK(whole->fragments == static_cast<int>(blocks.size()));
    CHECK(whole->fcs == dec::Fcs::Ok);
    CHECK(whole->sdu_status == 0);
    REQUIRE(whole->nwrk);
    CHECK(whole->nwrk->neighbours.size() == 7);
    CHECK(whole->nwrk->neighbours[6].cell_id == 7);
    CHECK(whole->nwrk->cell_load == 2);
    REQUIRE(whole->channel_allocation);
    CHECK(whole->channel_allocation->carrier == 3612);
    CHECK(whole->channel_allocation->text() == "carrier 3612, timeslots 2,3, uplink and downlink");
    CHECK(whole->summary().starts_with("reassembled from " + std::to_string(blocks.size()) + " fragments: "));
    CHECK(whole->summary().find("D-NWRK-BROADCAST") != std::string::npos);
    const std::string text = dec::describe(dec::DescribeKind::TmSdu, whole->sdu_bits.data(),
                                           static_cast<int>(whole->sdu_bits.size()));
    CHECK(text.find("frame check sequence, correct") != std::string::npos);
    CHECK(r.counters().started == 1);
    CHECK(r.counters().completed == 1);
    CHECK(r.counters().lost() == 0);
}

TEST_CASE("fragments: a lost fragment, a timeout, a new start, orphans")
{
    const auto blocks = fragmented(long_nwrk(7));
    REQUIRE(blocks.size() >= 3);
    SUBCASE("CRC error in the middle")
    {
        dec::Reassembler r;
        CHECK_FALSE(feed(r, 2, 1, blocks[0]));
        r.block_lost(2);
        CHECK_FALSE(r.active(2));
        for (std::size_t i = 1; i < blocks.size(); ++i)
            CHECK_FALSE(feed(r, 2, 1 + 4 * i, blocks[i]));
        CHECK(r.counters().lost_missing == 1);
        CHECK(r.counters().orphans == blocks.size() - 1);
        CHECK(r.counters().completed == 0);
    }
    SUBCASE("timeout in frame time")
    {
        dec::ReassemblyConfig cfg;
        cfg.timeout_slots = 20;
        dec::Reassembler r(cfg);
        CHECK_FALSE(feed(r, 1, 100, blocks[0]));
        r.tick(120);
        CHECK(r.active(1));  // 20 slots: still in time
        r.tick(121);
        CHECK_FALSE(r.active(1));
        CHECK(r.counters().lost_timeout == 1);
    }
    SUBCASE("a new start before the end")
    {
        dec::Reassembler r;
        CHECK_FALSE(feed(r, 1, 1, blocks[0]));
        CHECK_FALSE(feed(r, 1, 5, blocks[0]));
        std::optional<dec::BlockPdu> whole;
        for (std::size_t i = 1; i < blocks.size(); ++i)
            whole = feed(r, 1, 5 + 4 * i, blocks[i]);
        REQUIRE(whole);
        CHECK(whole->nwrk);
        CHECK(r.counters().lost_missing == 1);
        CHECK(r.counters().started == 2);
        CHECK(r.counters().completed == 1);
    }
    SUBCASE("longer than the limit")
    {
        dec::ReassemblyConfig cfg;
        cfg.max_bits = 300;
        dec::Reassembler r(cfg);
        for (std::size_t i = 0; i < blocks.size(); ++i)
            CHECK_FALSE(feed(r, 1, 1 + 4 * i, blocks[i]));
        CHECK(r.counters().lost_length == 1);
    }
    SUBCASE("sync lost")
    {
        dec::Reassembler r;
        CHECK_FALSE(feed(r, 3, 1, blocks[0]));
        r.reset();
        CHECK(r.counters().lost_missing == 1);
        CHECK(r.take_counters().lost() == 1);
        CHECK(r.counters().lost() == 0);
    }
}

TEST_CASE("fragments: interleaved timeslots are kept apart")
{
    const auto a = fragmented(long_nwrk(6, 1));
    const auto b = fragmented(long_nwrk(7, 20));
    dec::Reassembler r;
    std::optional<dec::BlockPdu> wa, wb;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        if (i < a.size())
            if (auto w = feed(r, 1, 4 * i, a[i]))
                wa = w;
        if (i < b.size())
            if (auto w = feed(r, 3, 4 * i + 2, b[i]))
                wb = w;
    }
    REQUIRE(wa);
    REQUIRE(wb);
    REQUIRE(wa->nwrk);
    REQUIRE(wb->nwrk);
    CHECK(wa->nwrk->neighbours.size() == 6);
    CHECK(wa->nwrk->neighbours[0].cell_id == 1);
    CHECK(wb->nwrk->neighbours.size() == 7);
    CHECK(wb->nwrk->neighbours[0].cell_id == 20);
    CHECK(r.counters().completed == 2);
}

TEST_CASE("fragments: wrong FCS and encrypted TM-SDUs are not decoded")
{
    SUBCASE("FCS error")
    {
        const auto blocks = fragmented(long_nwrk(7), {}, std::nullopt, true);
        dec::Reassembler r;
        std::optional<dec::BlockPdu> whole;
        for (std::size_t i = 0; i < blocks.size(); ++i)
            whole = feed(r, 1, i, blocks[i]);
        REQUIRE(whole);
        CHECK(whole->fcs == dec::Fcs::Error);
        CHECK_FALSE(whole->nwrk);
        CHECK(whole->sdu_status != 0);
        CHECK(whole->summary().find("FCS error, not decoded") != std::string::npos);
    }
    SUBCASE("FCS error in one block")
    {
        const auto block = gen::mac_resource_block(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(long_nwrk(1)), true), 268);
        const auto pdus = dec::parse_block_pdus(block.data(), 268);
        REQUIRE_FALSE(pdus.empty());
        CHECK(pdus[0].fcs == dec::Fcs::Error);
        CHECK_FALSE(pdus[0].nwrk);
    }
    SUBCASE("encrypted")
    {
        gen::ResourceOptions o;
        o.encryption_mode = 2;
        const auto blocks = fragmented(long_nwrk(7), o);
        dec::Reassembler r;
        std::optional<dec::BlockPdu> whole;
        for (std::size_t i = 0; i < blocks.size(); ++i)
            whole = feed(r, 1, i, blocks[i]);
        REQUIRE(whole);
        CHECK(whole->encrypted);
        CHECK_FALSE(whole->pdisc);
        CHECK_FALSE(whole->nwrk);
        CHECK(whole->sdu_bits.empty());
        CHECK(whole->summary().find("encrypted, not decoded") != std::string::npos);
    }
}

TEST_CASE("MM and CMCE PDUs: named or counted, never their addresses")
{
    const auto mm = gen::mac_resource_block(gen::llc_bl_udata(mm_location_update_reject(1, 3)), 268);
    auto pdus = dec::parse_block_pdus(mm.data(), 268);
    REQUIRE_FALSE(pdus.empty());
    CHECK(pdus[0].pdisc == 1);
    CHECK(pdus[0].pdu_type == 7);
    CHECK(pdus[0].summary() == "BL-UDATA MM D-LOCATION UPDATE REJECT");
    const std::string text = dec::describe(dec::DescribeKind::DlPdu, mm.data(), 268);
    CHECK(text.find("reject cause") != std::string::npos);
    CHECK(text.find("broadcast") != std::string::npos);
    CHECK(text.find("16777215") == std::string::npos);  // the SSI value itself
    CHECK(text.find("FFFFFF") == std::string::npos);

    const auto sds = gen::mac_resource_block(gen::llc_bl_udata(cmce_typed(15, 64)), 268);
    pdus = dec::parse_block_pdus(sds.data(), 268);
    REQUIRE_FALSE(pdus.empty());
    CHECK(pdus[0].pdisc == 2);
    CHECK(pdus[0].pdu_type == 15);
    CHECK(pdus[0].summary() == "BL-UDATA CMCE (not decoded)");  // calls and SDS: typed only (RAD-17)
}

TEST_CASE("over the air: fragments, FCS, CMCE only typed and the AACH plan")
{
    CellSignal s;
    s.seconds = 3.3;  // at least two whole multiframes after sync
    s.snr_db = 30.0;
    gen::ChannelAllocation ca;
    ca.timeslots = 0b0001;
    ca.carrier = 3605;
    gen::CellConfig& c = s.cell;
    c.block_sequences.push_back({1, 2, fragmented(long_nwrk(7), {}, ca)});
    c.block_sequences.push_back(
        {1, 8, {gen::mac_resource_block(gen::llc_bl_udata(cmce_typed(15, 64)), 268),
                gen::mac_resource_block(gen::llc_bl_udata(mm_location_update_reject(1, 3)), 268),
                gen::mac_resource_block(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(long_nwrk(1)), true), 268)}});
    gen::AccessAssign a0, a1, a2, a3;
    a0.field1 = {1, 3};
    a0.field2 = {2, 5};
    a1.header = 1;
    a1.dl_usage = 2;
    a1.field2 = {3, 1};
    a2.header = 2;
    a2.dl_usage = 1;
    a2.field2 = {0, 9};
    a3.header = 3;
    a3.dl_usage = 17;  // traffic
    a3.ul_usage = 23;
    // Traffic (a3) only on timeslot 4: the MCCH on timeslot 1 stays control.
    c.access_assign_plan = {a0, a1, a2, a3, a1, a0, a2, a3};
    const dec::DecoderOutput out = decode(make_cell(s));

    int reassembled = 0, aach_checked = 0, known = 0;
    for (const dec::DecodedSlot& sl : out.slots) {
        if (!sl.found || !sl.result.time_known)
            continue;
        ++known;
        for (const dec::DecodedBlock& b : sl.result.blocks)
            for (const dec::BlockPdu& p : b.pdus)
                if (p.reassembled) {
                    ++reassembled;
                    CHECK(sl.result.time.timeslot == 1);
                    CHECK(p.fcs == dec::Fcs::Ok);
                    REQUIRE(p.nwrk);
                    CHECK(p.nwrk->neighbours.size() == 7);
                    REQUIRE(p.channel_allocation);
                    CHECK(p.channel_allocation->carrier == 3605);
                    CHECK(p.channel_allocation->timeslots == 1);
                }
        if (!sl.result.aach_present || !sl.result.aach.decoded || sl.result.aach.corrected != 0)
            continue;
        gen::TdmaTime t;
        t.tn = sl.result.time.timeslot;
        t.fn = sl.result.time.frame;
        t.mn = sl.result.time.multiframe;
        const gen::AccessAssign& want = gen::planned_access_assign(c, t);
        const uint16_t bits = gen::encode_access_assign(want);
        const dec::AccessAssignInfo& got = sl.result.aach.info;
        CHECK(got.header == (bits >> 12));
        CHECK(got.field1 == ((bits >> 6) & 63));
        CHECK(got.field2 == (bits & 63));
        if (t.fn != 18 && want.header != 0) {
            CHECK(got.dl_usage == want.dl_usage);
            CHECK(got.ul_usage.has_value() == (want.header == 3));
        }
        ++aach_checked;
    }
    CHECK(reassembled >= 2);
    CHECK(known > 150);
    CHECK(aach_checked >= known * 9 / 10);  // the rest had bit errors at 30 dB
    dec::ReassemblyCounters f;
    uint64_t fcs = 0;
    for (const dec::PeriodReport& p : out.periods) {
        f += p.fragments;
        fcs += p.channels[static_cast<std::size_t>(dec::LogicalChannel::SchF)].fcs_errors;
    }
    // CMCE PDUs (here a D-SDS-DATA type) are typed, never decoded: no
    // content field of theirs exists on this side (RAD-17).
    uint64_t sds = 0;
    for (const dec::DecodedSlot& sl : out.slots)
        for (const dec::DecodedBlock& b : sl.result.blocks)
            for (const dec::BlockPdu& p : b.pdus)
                if (p.pdisc == 2 && p.pdu_type == 15) {
                    ++sds;
                    CHECK_FALSE(p.nwrk);
                    CHECK(p.summary() == "BL-UDATA CMCE (not decoded)");
                }
    CHECK(f.completed >= 2);
    CHECK(f.lost() == 0);
    CHECK(f.orphans == 0);
    CHECK(sds >= 2);
    CHECK(fcs >= 2);
}
