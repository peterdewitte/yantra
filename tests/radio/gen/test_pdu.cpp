// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Broadcast PDUs of the TETRA test-signal generator (RAD-70): field
// packing checked bit by bit against the EN 300 392-2 tables, range
// checks, and round trips through the test-only parsers and the full
// channel coding chain.

#include "ref_decoder.hpp"
#include "ref_pdu.hpp"

#include "yantra/gen/coding.hpp"
#include "yantra/gen/pdu.hpp"

#include <doctest/doctest.h>

#include <random>
#include <stdexcept>

using namespace yantra::gen;

namespace {

template <typename T>
T rnd(std::mt19937& rng, int bits)
{
    return static_cast<T>(rng() & ((1u << bits) - 1u));
}

BsServiceDetails random_details(std::mt19937& rng)
{
    return ref::parse_bs_service_details(rnd<uint16_t>(rng, 12));
}

NeighbourCell random_neighbour(std::mt19937& rng, bool all_optional)
{
    NeighbourCell n;
    n.cell_id = rnd<uint8_t>(rng, 5);
    n.reselection_types = rnd<uint8_t>(rng, 2);
    n.synchronised = (rng() & 1u) != 0;
    n.cell_load = rnd<uint8_t>(rng, 2);
    n.main_carrier = rnd<uint16_t>(rng, 12);
    auto want = [&] { return all_optional || (rng() & 1u) != 0; };
    if (want())
        n.carrier_extension = CarrierExtension{rnd<uint8_t>(rng, 4), rnd<uint8_t>(rng, 2),
                                               rnd<uint8_t>(rng, 3), (rng() & 1u) != 0};
    if (want())
        n.mcc = rnd<uint16_t>(rng, 10);
    if (want())
        n.mnc = rnd<uint16_t>(rng, 14);
    if (want())
        n.location_area = rnd<uint16_t>(rng, 14);
    if (want())
        n.max_ms_txpwr = rnd<uint8_t>(rng, 3);
    if (want())
        n.min_rx_access_level = rnd<uint8_t>(rng, 4);
    if (want())
        n.subscriber_class = rnd<uint16_t>(rng, 16);
    if (want())
        n.bs_service_details = random_details(rng);
    if (want())
        n.timeshare_security = rnd<uint8_t>(rng, 5);
    if (want())
        n.tdma_frame_offset = rnd<uint8_t>(rng, 6);
    return n;
}

}  // namespace

TEST_CASE("MAC-SYNC + D-MLE-SYNC: field layout bit by bit")
{
    MacSync s;  // defaults: CC 1, TN 1, FN 18, MN 1, MCC 901, MNC 1
    const Bits b = encode_sync(s);
    REQUIRE(b.size() == 60);
    CHECK(to_string(b) ==
          "0000"            // system code
          "000001"          // colour code
          "00"              // TN - 1
          "10010"           // frame 18
          "000001"          // multiframe 1
          "00" "000" "0" "0" "0"  // sharing, TS reserved, DTX, F18 ext, reserved
          "1110000101"      // MCC 901
          "00000000000001"  // MNC 1
          "10" "00" "1");   // nbr cell broadcast, service level, late entry

    s.timeslot = 4;
    s.colour_code = 63;
    CHECK(get_bits(encode_sync(s), 4, 8) == 0xFF);  // CC 63, TN 4 -> 11
}

TEST_CASE("MAC-SYNC refuses out-of-range fields")
{
    auto bad = [](auto change) {
        MacSync s;
        change(s);
        CHECK_THROWS_AS(encode_sync(s), std::out_of_range);
    };
    bad([](MacSync& s) { s.timeslot = 0; });
    bad([](MacSync& s) { s.timeslot = 5; });
    bad([](MacSync& s) { s.frame = 0; });
    bad([](MacSync& s) { s.frame = 19; });
    bad([](MacSync& s) { s.multiframe = 0; });
    bad([](MacSync& s) { s.multiframe = 61; });
    bad([](MacSync& s) { s.colour_code = 64; });
    bad([](MacSync& s) { s.system_code = 16; });
    bad([](MacSync& s) { s.mle.mcc = 1024; });
    bad([](MacSync& s) { s.mle.mnc = 16384; });
    bad([](MacSync& s) { s.mle.neighbour_cell_broadcast = 4; });
}

TEST_CASE("MAC-SYNC round trip over random field values")
{
    std::mt19937 rng(1);
    for (int i = 0; i < 300; ++i) {
        MacSync s;
        s.system_code = rnd<uint8_t>(rng, 4);
        s.colour_code = rnd<uint8_t>(rng, 6);
        s.timeslot = static_cast<uint8_t>(1 + rng() % 4);
        s.frame = static_cast<uint8_t>(1 + rng() % 18);
        s.multiframe = static_cast<uint8_t>(1 + rng() % 60);
        s.sharing_mode = rnd<uint8_t>(rng, 2);
        s.ts_reserved_frames = rnd<uint8_t>(rng, 3);
        s.u_plane_dtx = (rng() & 1u) != 0;
        s.frame18_extension = (rng() & 1u) != 0;
        s.mle.mcc = rnd<uint16_t>(rng, 10);
        s.mle.mnc = rnd<uint16_t>(rng, 14);
        s.mle.neighbour_cell_broadcast = rnd<uint8_t>(rng, 2);
        s.mle.cell_service_level = rnd<uint8_t>(rng, 2);
        s.mle.late_entry_supported = (rng() & 1u) != 0;
        CHECK(ref::parse_sync(encode_sync(s)) == s);
    }
}

TEST_CASE("BS service details and extended services pack in transmission order")
{
    BsServiceDetails d{};
    d = BsServiceDetails{false, false, false, false, false, false, false, false, false, false, false, false};
    CHECK(d.pack() == 0);
    d.registration = true;
    CHECK(d.pack() == 0x800);
    d.registration = false;
    d.air_interface_encryption = true;
    CHECK(d.pack() == 0b10);
    d.advanced_link = true;
    CHECK(d.pack() == 0b11);
    for (uint16_t v = 0; v < 4096; ++v)
        CHECK(ref::parse_bs_service_details(v).pack() == v);

    ExtendedServices e;
    e.security_information = 0xA5;
    e.sds_tl_addressing_method = 2;
    e.gck_supported = true;
    e.section = 1;
    e.section_information = 0x55;
    CHECK(e.pack() == ((0xA5u << 12) | (2u << 10) | (1u << 9) | (1u << 7) | 0x55u));
    CHECK(ref::parse_extended_services(e.pack()) == e);
    e.section_information = 0x80;
    CHECK_THROWS_AS(e.pack(), std::out_of_range);
}

TEST_CASE("MAC-SYSINFO + D-MLE-SYSINFO: layout, sizes and round trip")
{
    MacSysinfo s;
    s.main_carrier = 0xABC;
    s.frequency_band = 3;
    s.mle.location_area = 0x1234;
    s.mle.bs_service_details.air_interface_encryption = true;
    s.option_value = ExtendedServices{}.pack();
    const Bits b = encode_sysinfo(s);
    REQUIRE(b.size() == 124);
    CHECK(to_string(Bits(b.begin(), b.begin() + 20)) == "10" "00" "101010111100" "0011");
    CHECK(get_bits(b, 60, 2) == 3);             // optional field flag: extended services
    CHECK(get_bits(b, 82, 14) == 0x1234);       // location area starts the MLE part
    CHECK(get_bits(b, 112, 12) == s.mle.bs_service_details.pack());
    CHECK(ref::parse_sysinfo(b) == s);

    std::mt19937 rng(2);
    for (int i = 0; i < 300; ++i) {
        MacSysinfo r;
        r.main_carrier = rnd<uint16_t>(rng, 12);
        r.frequency_band = rnd<uint8_t>(rng, 4);
        r.offset = rnd<uint8_t>(rng, 2);
        r.duplex_spacing = rnd<uint8_t>(rng, 3);
        r.reverse_operation = (rng() & 1u) != 0;
        r.num_common_secondary_cch = rnd<uint8_t>(rng, 2);
        r.ms_txpwr_max_cell = rnd<uint8_t>(rng, 3);
        r.rxlev_access_min = rnd<uint8_t>(rng, 4);
        r.access_parameter = rnd<uint8_t>(rng, 4);
        r.radio_downlink_timeout = rnd<uint8_t>(rng, 4);
        r.cck_flag = (rng() & 1u) != 0;
        r.hyperframe_or_cck = rnd<uint16_t>(rng, 16);
        r.option = static_cast<SysinfoOption>(rng() % 4);
        r.option_value = rnd<uint32_t>(rng, 20);
        r.mle.location_area = rnd<uint16_t>(rng, 14);
        r.mle.subscriber_class = rnd<uint16_t>(rng, 16);
        r.mle.bs_service_details = random_details(rng);
        CHECK(ref::parse_sysinfo(encode_sysinfo(r)) == r);
    }

    MacSysinfo bad;
    bad.main_carrier = 4096;
    CHECK_THROWS_AS(encode_sysinfo(bad), std::out_of_range);
    bad = MacSysinfo{};
    bad.option_value = 1u << 20;
    CHECK_THROWS_AS(encode_sysinfo(bad), std::out_of_range);
}

TEST_CASE("ACCESS-ASSIGN header 00 with two access fields")
{
    AccessAssign a{{1, 5}, {3, 15}};
    CHECK(encode_access_assign(a) == 0b00'01'0101'11'1111);
    for (unsigned v = 0; v < 4096; ++v) {
        const AccessAssign r{{static_cast<uint8_t>(v >> 10), static_cast<uint8_t>((v >> 6) & 15u)},
                             {static_cast<uint8_t>((v >> 4) & 3u), static_cast<uint8_t>(v & 15u)}};
        CHECK(ref::parse_access_assign(encode_access_assign(r)) == r);
    }
    a.field2.base_frame_length = 16;
    CHECK_THROWS_AS(encode_access_assign(a), std::out_of_range);
    CHECK_THROWS_AS(ref::parse_access_assign(0x1000), std::invalid_argument);
}

TEST_CASE("MAC Null PDU and null blocks")
{
    CHECK(to_string(encode_null_pdu()) == "00" "0" "0" "00" "0" "000010" "000");
    for (int block : {kSchHd.type1, kSchF.type1}) {
        const Bits b = null_block(block);
        REQUIRE(static_cast<int>(b.size()) == block);
        const auto pdus = ref::parse_mac_block(b);
        REQUIRE(pdus.size() == 1);
        CHECK(pdus[0].address_type == 0);
        CHECK(pdus[0].length_octets == 2);
    }
}

TEST_CASE("MAC-RESOURCE blocks: length in octets, fill bits, Null PDU after, size limit")
{
    CHECK(max_tm_sdu_bits(kSchF.type1) == 33 * 8 - 43);
    CHECK(max_tm_sdu_bits(kSchHd.type1) == 15 * 8 - 43);
    std::mt19937 rng(4);
    for (int block : {kSchHd.type1, kSchF.type1}) {
        for (int n = 0; n <= max_tm_sdu_bits(block); ++n) {
            CAPTURE(n);
            Bits sdu(static_cast<size_t>(n));
            for (auto& v : sdu)
                v = static_cast<uint8_t>(rng() & 1u);
            const Bits b = mac_resource_block(sdu, block);
            REQUIRE(static_cast<int>(b.size()) == block);
            const auto pdus = ref::parse_mac_block(b);
            REQUIRE(!pdus.empty());
            const auto& m = pdus[0];
            const int octets = (kMacResourceHeaderBits + n + 7) / 8;
            CHECK(m.length_octets == static_cast<unsigned>(octets));
            CHECK(m.fill_bits == ((kMacResourceHeaderBits + n) % 8 != 0));
            CHECK(m.address_type == 1);
            CHECK(m.ssi == kBroadcastSsi);
            CHECK(m.encryption_mode == 0);
            CHECK(m.tm_sdu == sdu);
            const bool room = block - octets * 8 >= kNullPduBits;
            REQUIRE(pdus.size() == (room ? 2u : 1u));
            if (room)
                CHECK(pdus[1].address_type == 0);
        }
        CHECK_THROWS_AS(mac_resource_block(Bits(static_cast<size_t>(max_tm_sdu_bits(block) + 1), 0), block),
                        std::length_error);
    }
}

TEST_CASE("D-NWRK-BROADCAST: layout of a PDU without optional elements")
{
    const NwrkBroadcast p;  // reselect 2/2/4/2, load 0, no options
    CHECK(to_string(encode_nwrk_broadcast(p)) ==
          "101" "010" "0010" "0010" "0100" "0010" "00" "0");
    CHECK(to_string(llc_bl_udata(Bits{1, 0, 1})) == "0010" "101");
}

TEST_CASE("Neighbour cell information sizes and p-bits")
{
    NeighbourCell n;
    CHECK(neighbour_cell_bits(n) == 5 + 2 + 1 + 2 + 12 + 1);
    n.mcc = 901;
    // o-bit, then 10 p-bits, MCC present.
    CHECK(neighbour_cell_bits(n) == 23 + 10 + 10);
    std::mt19937 rng(6);
    const NeighbourCell full = random_neighbour(rng, true);
    CHECK(neighbour_cell_bits(full) == 23 + 10 + (10 + 10 + 14 + 14 + 3 + 4 + 16 + 12 + 5 + 6));
    n.tdma_frame_offset = 64;
    NwrkBroadcast p;
    p.neighbours = std::vector<NeighbourCell>{n};
    CHECK_THROWS_AS(encode_nwrk_broadcast(p), std::out_of_range);
    p.neighbours = std::vector<NeighbourCell>(8);
    CHECK_THROWS_AS(encode_nwrk_broadcast(p), std::out_of_range);
}

TEST_CASE("D-NWRK-BROADCAST round trips through LLC and MAC-RESOURCE")
{
    std::mt19937 rng(9);
    int built = 0, too_long = 0;
    for (int i = 0; i < 400; ++i) {
        NwrkBroadcast p;
        p.reselect = {rnd<uint8_t>(rng, 4), rnd<uint8_t>(rng, 4), rnd<uint8_t>(rng, 4), rnd<uint8_t>(rng, 4)};
        p.cell_load = rnd<uint8_t>(rng, 2);
        if (rng() & 1u)
            p.network_time = NetworkTime{rnd<uint32_t>(rng, 24), (rng() & 1u) != 0, rnd<uint8_t>(rng, 6),
                                         rnd<uint8_t>(rng, 6), rnd<uint16_t>(rng, 11)};
        if (rng() % 4 != 0) {
            std::vector<NeighbourCell> list;
            const auto count = rng() % 8;
            for (unsigned k = 0; k < count; ++k)
                list.push_back(random_neighbour(rng, rng() % 4 == 0));
            p.neighbours = list;
        }
        const Bits mle = encode_nwrk_broadcast(p);
        CHECK(ref::parse_nwrk_broadcast_sdu(llc_bl_udata(mle)) == p);
        if (static_cast<int>(mle.size()) + 4 > max_tm_sdu_bits(kSchF.type1)) {
            CHECK_THROWS_AS(nwrk_broadcast_block(p), std::length_error);
            ++too_long;
            continue;
        }
        const Bits block = nwrk_broadcast_block(p);
        REQUIRE(block.size() == 268);
        const auto pdus = ref::parse_mac_block(block);
        REQUIRE(!pdus.empty());
        CHECK(ref::parse_nwrk_broadcast_sdu(pdus[0].tm_sdu) == p);
        ++built;
    }
    CHECK(built > 100);
    CHECK(too_long > 10);

    // Six minimal neighbours plus network time fit one SCH/F block.
    NwrkBroadcast six;
    six.network_time = NetworkTime{};
    six.neighbours = std::vector<NeighbourCell>(6);
    CHECK_NOTHROW(nwrk_broadcast_block(six));
    six.neighbours = std::vector<NeighbourCell>(7);
    CHECK_THROWS_AS(nwrk_broadcast_block(six), std::length_error);
}

TEST_CASE("Full chain: PDUs survive coding and the reference decoder")
{
    const uint32_t cell = scrambling_init(901, 1, 1);

    MacSync sync;
    sync.timeslot = 3;
    sync.multiframe = 42;
    auto d = ref::decode_block(encode_bsch(encode_sync(sync)), kBsch, kScramblingInitBsch);
    REQUIRE(d.crc_ok);
    CHECK(ref::parse_sync(d.type1) == sync);

    MacSysinfo sys;
    sys.main_carrier = 1600;
    sys.option_value = ExtendedServices{}.pack();
    d = ref::decode_block(encode_sch_hd(encode_sysinfo(sys), cell), kSchHd, cell);
    REQUIRE(d.crc_ok);
    CHECK(ref::parse_sysinfo(d.type1) == sys);

    const AccessAssign aa{{0, 1}, {2, 9}};
    CHECK(ref::parse_access_assign(ref::decode_aach(encode_aach(encode_access_assign(aa), cell), cell)) == aa);

    NwrkBroadcast nb;
    NeighbourCell n;
    n.cell_id = 2;
    n.main_carrier = 1604;
    n.location_area = 2;
    nb.neighbours = std::vector<NeighbourCell>{n};
    d = ref::decode_block(encode_sch_f(nwrk_broadcast_block(nb), cell), kSchF, cell);
    REQUIRE(d.crc_ok);
    const auto pdus = ref::parse_mac_block(d.type1);
    REQUIRE(!pdus.empty());
    CHECK(ref::parse_nwrk_broadcast_sdu(pdus[0].tm_sdu) == nb);

    d = ref::decode_block(encode_sch_f(null_block(kSchF.type1), cell), kSchF, cell);
    REQUIRE(d.crc_ok);
    CHECK(ref::parse_mac_block(d.type1).at(0).address_type == 0);
}

TEST_CASE("fragment_blocks: refuses a TM-SDU that fits one block or does not fill the first fragment")
{
    const int max = max_tm_sdu_bits(268);  // 221
    CHECK_THROWS_AS(fragment_blocks(Bits(static_cast<std::size_t>(max), 1), 268), std::invalid_argument);
    // 222..232 bits: longer than one block, but the first fragment takes
    // 225 bits and the MAC-END needs at least 8 (read past the end before).
    for (int n = max + 1; n < 268 - 43 + 8; ++n)
        CHECK_THROWS_AS(fragment_blocks(Bits(static_cast<std::size_t>(n), 1), 268), std::length_error);
    const auto blocks = fragment_blocks(Bits(268 - 43 + 8, 1), 268);
    CHECK(blocks.size() == 2);
    for (const Bits& b : blocks)
        CHECK(b.size() == 268u);
}
