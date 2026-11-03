// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Upper layers through the Rust protocol crate (RAD-101, DEC-02, DEC-03,
// RAD-123): D-MLE-SYSINFO against osmo-tetra's decode, D-NWRK-BROADCAST of
// generated cells back to the generator's ground truth (re-select
// parameters, network time, every neighbour element), neighbour lists
// longer than one PDU merged up to 31 cells, a new cell starting a new
// list, the field-by-field text and the C ABI version.
// Copied from yantra 68ea20a: tests/tetra_decoder/test_neighbours.cpp,
// adapted (osmo-tetra's parse_sysinfo against the crate's
// parse_sysinfo_proto; re-select parameters in the DEC-03 set).

#include "network_check.hpp"
#include "signal.hpp"

#include "yantra/receiver/mle.hpp"
#include "yantra/gen/pdu.hpp"

#include <doctest/doctest.h>

#include <random>
#include <string>

using namespace dectest;

namespace {

gen::NeighbourCell full_neighbour(uint8_t id)
{
    gen::NeighbourCell n;
    n.cell_id = id;
    n.reselection_types = 2;
    n.synchronised = false;
    n.cell_load = 3;
    n.main_carrier = static_cast<uint16_t>(3500 + id);
    n.carrier_extension = gen::CarrierExtension{4, 2, 1, true};
    n.mcc = 901;
    n.mnc = 77;
    n.location_area = static_cast<uint16_t>(0x1234 + id);
    n.max_ms_txpwr = 6;
    n.min_rx_access_level = 9;
    n.subscriber_class = 0xA5F0;
    gen::BsServiceDetails s;
    s.air_interface_encryption = true;
    s.priority_cell = true;
    n.bs_service_details = s;
    n.timeshare_security = 0x15;
    n.tdma_frame_offset = 33;
    return n;
}

}  // namespace

TEST_CASE("D-MLE-SYSINFO through the protocol crate equals osmo-tetra's decode (cross-check)")
{
    REQUIRE(dec::proto_abi_matches());
    std::mt19937 rng(20261102);
    int compared = 0;
    for (int run = 0; run < 2000; ++run) {
        uint8_t bits[124];
        for (uint8_t& b : bits)
            b = static_cast<uint8_t>(rng() & 1);
        bits[0] = 1;  // MAC PDU type 10: broadcast
        bits[1] = 0;
        bits[2] = 0;  // broadcast type 00: SYSINFO
        bits[3] = 0;
        const auto a = dec::parse_sysinfo_proto(bits);
        const auto b = dec::parse_sysinfo(bits);  // osmo-tetra's macpdu_decode_sysinfo
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        INFO("run ", run);
        CHECK(*a == *b);
        ++compared;
        bits[0] = 0;  // not a broadcast PDU: neither parses it
        CHECK_FALSE(dec::parse_sysinfo_proto(bits));
        CHECK_FALSE(dec::parse_sysinfo(bits));
    }
    CHECK(compared == 2000);
}

TEST_CASE("Serving-cell values derived from SYSINFO (DEC-03)")
{
    gen::CellConfig cfg = gen::CellConfig::defaults();  // carrier 3600 band 3: 390.000 MHz
    cfg.sysinfo.offset = 1;
    cfg.sysinfo.duplex_spacing = 0;
    const gen::Bits si = gen::encode_sysinfo(cfg.sysinfo);
    const auto s = dec::parse_sysinfo(si.data());
    REQUIRE(s);
    CHECK(s->offset_hz() == 6250);
    CHECK(s->downlink_hz() == 390006250u);
    REQUIRE(s->duplex_spacing_hz());
    CHECK(*s->duplex_spacing_hz() == 10000000u);
    REQUIRE(s->uplink_hz());
    CHECK(*s->uplink_hz() == 380006250u);
    cfg.sysinfo.reverse_operation = true;
    const auto r = dec::parse_sysinfo(gen::encode_sysinfo(cfg.sysinfo).data());
    REQUIRE(r);
    CHECK(*r->uplink_hz() == 400006250u);  // RX above TX
    CHECK(dec::class_list_text(0xFFFF) == "1-16");
    CHECK(dec::class_list_text(0xA5F0) == "1,3,6,8-12");
    CHECK(dec::class_list_text(0) == "none");
}

TEST_CASE("A cell's D-NWRK-BROADCAST decodes to its re-select parameters and neighbours (DEC-02)")
{
    CellSignal s;
    gen::NwrkBroadcast nb;
    nb.reselect = {3, 1, 7, 2};
    nb.cell_load = 1;
    nb.network_time = gen::NetworkTime{4321, true, 6, 25, 0x7FF};
    // Every optional element; with the network time only one such
    // neighbour fits the block.
    nb.neighbours = std::vector<gen::NeighbourCell>{full_neighbour(4)};
    s.cell.nwrk_broadcast = nb;
    s.seconds = 2.2;  // the cell is known from frame 18; then frames 1, 7, 13
    const dec::DecoderOutput out = decode(make_cell(s));
    REQUIRE_FALSE(out.cells.empty());
    const dec::CellInfo& c = out.cells.back().cell;
    REQUIRE(c.network);
    check_network(*c.network, {nb});
    CHECK(c.network->network_time->text() == "2025-01-01 02:24:02 UTC-01:30");
    // The re-select parameters are part of the serving-cell set (DEC-03).
    REQUIRE(c.params.reselect);
    CHECK(*c.params.reselect == dec::ReselectParams{3, 1, 7, 2});
    CHECK(dec::ReselectParams::db(c.params.reselect->fast_threshold) == 14);
    // With a carrier extension: the neighbour's own band and offset.
    CHECK(c.network->neighbours[0].downlink_hz(3, 0) == dec::carrier_downlink_hz(4, 3504, 2));

    // The MAC-RESOURCE blocks carry it as one MLE PDU each, never an address.
    int nwrk_blocks = 0;
    for (const dec::DecodedSlot& sl : out.slots)
        for (const dec::DecodedBlock& b : sl.result.blocks)
            for (const dec::BlockPdu& p : b.pdus)
                if (p.nwrk) {
                    ++nwrk_blocks;
                    CHECK(p.broadcast);
                    CHECK(p.summary().find("MLE D-NWRK-BROADCAST: SRT 6 SRH 2 FRT 14 FRH 4 dB, load low, 1 neighbour") !=
                          std::string::npos);
                }
    CHECK(nwrk_blocks >= 3);
}

TEST_CASE("Neighbour lists longer than one PDU are merged up to 31 cells")
{
    CellSignal s;
    gen::NwrkBroadcast all;
    all.reselect = {5, 3, 9, 1};
    all.neighbours = std::vector<gen::NeighbourCell>{};
    for (uint8_t id = 31; id >= 1; --id) {  // sent in descending order, reported sorted
        gen::NeighbourCell n;
        n.cell_id = id;
        n.main_carrier = static_cast<uint16_t>(3700 + id);
        if (id % 4 == 0)
            n.location_area = static_cast<uint16_t>(100 + id);
        if (id % 5 == 0)
            n.min_rx_access_level = static_cast<uint8_t>(id % 16);
        all.neighbours->push_back(n);
    }
    const std::vector<gen::NwrkBroadcast> pdus = gen::split_nwrk_broadcast(all);
    REQUIRE(pdus.size() >= 5);
    for (const auto& p : pdus)
        CHECK(p.neighbours->size() <= 7);
    s.cell.nwrk_broadcast = pdus.front();
    s.cell.nwrk_more.assign(pdus.begin() + 1, pdus.end());
    // Three D-NWRK-BROADCAST frames per multiframe: every PDU within 3 multiframes.
    s.seconds = 3.2;
    const dec::DecoderOutput out = decode(make_cell(s));
    REQUIRE_FALSE(out.cells.empty());
    const dec::CellInfo& c = out.cells.back().cell;
    REQUIRE(c.network);
    CHECK(c.network->neighbours.size() == 31);
    check_network(*c.network, pdus);
    // Without a carrier extension: the serving cell's band and offset.
    CHECK(c.network->neighbours[0].downlink_hz(c.sysinfo.frequency_band, c.sysinfo.offset) == 392525000u);
    // The list grew in changed cell reports, never shrank.
    std::size_t last = 0;
    for (const dec::CellEvent& ev : out.cells)
        if (ev.cell.network) {
            CHECK(ev.cell.network->neighbours.size() >= last);
            last = ev.cell.network->neighbours.size();
        }
}

TEST_CASE("NetworkInfo merges by cell identifier and a new cell starts a new list")
{
    dec::NetworkInfo n;
    dec::NwrkBroadcastInfo a;
    a.reselect = {1, 2, 3, 4};
    dec::NeighbourInfo x;
    x.cell_id = 5;
    x.main_carrier = 100;
    dec::NeighbourInfo y = x;
    y.cell_id = 2;
    a.neighbours = {x, y};
    n.merge(a);
    REQUIRE(n.neighbours.size() == 2);
    CHECK(n.neighbours[0].cell_id == 2);
    dec::NwrkBroadcastInfo b;
    b.reselect = {4, 3, 2, 1};
    x.main_carrier = 200;  // the same cell id again: replaced, not added
    b.neighbours = {x};
    n.merge(b);
    REQUIRE(n.neighbours.size() == 2);
    CHECK(n.neighbours[1].main_carrier == 200);
    CHECK(n.reselect.slow_threshold == 4);
    CHECK(n.broadcasts == 2);

    // Two cells one after the other on the same channel: the second one's
    // list does not inherit the first one's neighbours.
    CellSignal s1;
    gen::NwrkBroadcast nb;
    gen::NeighbourCell m;
    m.cell_id = 7;
    m.main_carrier = 3333;
    nb.neighbours = std::vector<gen::NeighbourCell>{m};
    s1.cell.nwrk_broadcast = nb;
    s1.seconds = 1.2;
    CellSignal s2 = s1;
    s2.cell.sync.colour_code = 9;
    m.cell_id = 8;
    nb.neighbours = std::vector<gen::NeighbourCell>{m};
    s2.cell.nwrk_broadcast = nb;
    std::vector<cf> iq = make_cell(s1);
    const std::vector<cf> second = make_cell(s2);
    iq.insert(iq.end(), second.begin(), second.end());
    const dec::DecoderOutput out = decode(iq);
    REQUIRE_FALSE(out.cells.empty());
    const dec::CellInfo& last = out.cells.back().cell;
    CHECK(last.sync.colour_code == 9);
    REQUIRE(last.network);
    REQUIRE(last.network->neighbours.size() == 1);
    CHECK(last.network->neighbours[0].cell_id == 8);
}

TEST_CASE("Field-by-field text of SYNC, SYSINFO and a D-NWRK-BROADCAST block (DEC-04 groundwork)")
{
    gen::CellConfig cfg = gen::CellConfig::defaults();
    const gen::Bits sync = gen::encode_sync(cfg.sync);
    const std::string t1 = dec::describe(dec::DescribeKind::Sync, sync.data(), static_cast<int>(sync.size()));
    CHECK(t1.find("\tMCC\t901\t") != std::string::npos);
    const gen::Bits si = gen::encode_sysinfo(cfg.sysinfo);
    const std::string t2 = dec::describe(dec::DescribeKind::Sysinfo, si.data(), static_cast<int>(si.size()));
    CHECK(t2.find("location area") != std::string::npos);
    gen::NwrkBroadcast nb;
    nb.neighbours = std::vector<gen::NeighbourCell>{full_neighbour(3)};
    const gen::Bits blk = gen::nwrk_broadcast_block(nb);
    const std::string t3 = dec::describe(dec::DescribeKind::DlPdu, blk.data(), static_cast<int>(blk.size()));
    CHECK(t3.find("neighbour 1: location area") != std::string::npos);
    CHECK(t3.find("error") == std::string::npos);
    // The address is shown as skipped, never its value.
    CHECK(t3.find("16777215") == std::string::npos);
    CHECK(t3.find("FFFFFF") == std::string::npos);
    // Truncated: text up to the cut and an error line.
    const std::string t4 = dec::describe(dec::DescribeKind::DlPdu, blk.data(), 60);
    CHECK(t4.find("error\t") != std::string::npos);
}
