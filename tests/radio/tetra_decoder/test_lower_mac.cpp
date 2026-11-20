// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lower MAC on osmo-tetra (RAD-101, RAD-123, DEC-01, DEC-02, DEC-05,
// DEC-10, DEC-11): blocks with bit errors still pass their CRC (soft
// Viterbi), half-slot bursts, the AACH's RM(30,14) code with up to 3
// errors, encryption flags, the TDMA time from SYNC, and osmo-tetra's own
// test PDUs (testpdu.c) through the wrapper's parsers and through a full
// encode/decode round trip. Copied from yantra 68ea20a:
// tests/tetra_decoder/test_lower_mac.cpp, extended.

#include "signal.hpp"

#include "yantra/gen/burst.hpp"
#include "yantra/gen/coding.hpp"
#include "yantra/gen/downlink.hpp"
#include "yantra/gen/pdu.hpp"
#include "yantra/receiver/broadcast.hpp"
#include "yantra/receiver/lower_mac.hpp"

#include <doctest/doctest.h>

#include <bit>
#include <random>

extern "C" {
#include "testpdu.h"
}

using namespace dectest;

namespace {

gen::Bits unpack(const uint8_t* bytes, int nbits, int skip = 0)
{
    gen::Bits b;
    for (int i = skip; i < skip + nbits; ++i)
        b.push_back(static_cast<uint8_t>((bytes[i / 8] >> (7 - i % 8)) & 1));
    return b;
}

}  // namespace

TEST_CASE("Blocks with bit errors pass their CRC; soft decisions repair what hard ones cannot")
{
    gen::CellConfig cell = gen::CellConfig::defaults();
    cell.start = {1, 17, 1, 0};
    // 2 wrong bits per 216-bit block field (4 in an SCH/F block of 432
    // coded bits, 2 in the 120 bits of SB1), none in the training
    // sequences: about 1 % bit errors. As confident (hard) errors the
    // punctured rate-2/3 code loses an occasional block; the same errors
    // with the low confidence a demodulator gives a doubtful symbol are
    // all corrected.
    for (const bool soft : {false, true}) {
        rx::LowerMac mac;
        std::mt19937_64 rng(3);
        int blocks = 0;
        int ok = 0;
        int errors_seen = 0;
        for (const gen::Bits& b : bursts(cell, 4 * 18 * 4)) {
            rx::Burst burst = burst_of(b, type_of(b));
            for (int k = 0; k < 4; ++k) {
                const auto at = static_cast<std::size_t>(rng() % 200) + (k % 2 == 0 ? 20u : 282u);
                const int flipped = -burst.soft[at];
                burst.soft[at] = static_cast<rx::SoftBit>(soft ? flipped / 8 : flipped);
            }
            const rx::SlotResult r = mac.decode(burst);
            for (const rx::DecodedBlock& blk : r.blocks) {
                ++blocks;
                ok += blk.crc_ok ? 1 : 0;
                errors_seen += blk.bit_errors;
            }
        }
        INFO("soft ", soft);
        CHECK(blocks > 250);
        CHECK(errors_seen > 0);  // the re-encoded BER estimate sees the flipped bits
        if (soft)
            CHECK(ok == blocks);
        else
            CHECK(ok >= blocks * 97 / 100);
    }
}

TEST_CASE("Soft bits keep osmo-tetra's sign convention; -128 is accepted")
{
    gen::CellConfig cell = gen::CellConfig::defaults();
    cell.start = {1, 18, 1, 0};
    const gen::Bits b = bursts(cell, 1).front();
    rx::Burst burst = burst_of(b, rx::BurstType::Sync);
    for (auto& s : burst.soft)
        s = s < 0 ? static_cast<rx::SoftBit>(-128) : static_cast<rx::SoftBit>(127);
    rx::LowerMac mac;
    const rx::SlotResult r = mac.decode(burst);
    REQUIRE(r.blocks.size() == 2);
    CHECK(r.blocks[0].crc_ok);
    CHECK(r.blocks[1].crc_ok);
    // Inverted soft bits (the opposite sign convention) never decode.
    for (auto& s : burst.soft)
        s = static_cast<rx::SoftBit>(s == -128 ? 127 : -s);
    rx::LowerMac other;
    const rx::SlotResult wrong = other.decode(burst);
    REQUIRE_FALSE(wrong.blocks.empty());
    CHECK_FALSE(wrong.blocks[0].crc_ok);
    CHECK_FALSE(wrong.time_known);
}

TEST_CASE("The TDMA time comes from SYNC and every block carries it (DEC-11)")
{
    gen::CellConfig cell = gen::CellConfig::defaults();
    cell.start = {2, 17, 59, 4321};  // the first sync burst: frame 18, multiframe 59
    rx::LowerMac mac;
    const std::vector<gen::Bits> bs = bursts(cell, 2 * 18 * 4 + 6);
    gen::TdmaTime t = cell.start;
    int checked = 0;
    for (std::size_t i = 0; i < bs.size(); ++i, t.advance()) {
        if (i == 30) {
            mac.missed_slot();  // a slot without a burst: the time still moves on
            continue;
        }
        const rx::SlotResult r = mac.decode(burst_of(bs[i], type_of(bs[i])));
        if (t.fn < 18 && t.mn == 59) {
            // Before the first BSCH: no time, nothing descrambled.
            CHECK_FALSE(r.time_known);
            CHECK(r.blocks.empty());
            CHECK(r.aach_present);
            CHECK_FALSE(r.aach.decoded);
            continue;
        }
        REQUIRE(r.time_known);
        CHECK_FALSE(r.time_jump);
        CHECK(r.time.timeslot == t.tn);
        CHECK(r.time.frame == t.fn);
        CHECK(r.time.multiframe == t.mn);
        // The hyperframe comes with the first SYSINFO (SB2 of the first
        // sync burst).
        CHECK(r.time.hyperframe_known);
        CHECK(r.time.hyperframe == t.hn);
        CHECK(r.aach.decoded);
        for (const rx::DecodedBlock& b : r.blocks) {
            CHECK(b.crc_ok);
            CHECK(b.time == r.time);
        }
        ++checked;
    }
    CHECK(checked > 2 * 18 * 4 - 8);
    CHECK(mac.time().hyperframe == 4322);  // wrapped from multiframe 60 into the next hyperframe
    REQUIRE(mac.cell().has_value());
    CHECK(mac.cell()->mcc == 901);
    CHECK(mac.scrambling_code() == gen::scrambling_init(901, 1, 1));

    // Lost sync: unknown until the next BSCH; a BSCH that disagrees with
    // the counted time is flagged as a jump.
    mac.lost_sync();
    CHECK_FALSE(mac.time_known());
    rx::LowerMac m2;
    gen::CellConfig c2 = cell;
    c2.start = {1, 18, 5, 0};
    const auto b2 = bursts(c2, 2);
    m2.decode(burst_of(b2[0], rx::BurstType::Sync));
    m2.missed_slot();
    m2.missed_slot();  // now one slot ahead of the truth
    const rx::SlotResult jump = m2.decode(burst_of(b2[1], rx::BurstType::Sync));
    CHECK(jump.time_jump);
    CHECK(jump.time.timeslot == 2);
}

TEST_CASE("BNCH and SCH/HD in frame 18, as osmo-tetra's is_bnch")
{
    gen::CellConfig cell = gen::CellConfig::defaults();
    cell.start = {1, 18, 1, 0};
    for (int mn = 1; mn <= 4; ++mn) {
        cell.start.mn = static_cast<uint8_t>(mn);
        rx::LowerMac mac;
        int bnch = 0;
        for (const gen::Bits& b : bursts(cell, 4)) {
            const rx::SlotResult r = mac.decode(burst_of(b, rx::BurstType::Sync));
            REQUIRE(r.blocks.size() == 2);
            CHECK(r.blocks[0].channel == rx::LogicalChannel::Bsch);
            CHECK(r.blocks[0].block == 1);
            CHECK(r.blocks[1].block == 2);
            const bool is = r.blocks[1].channel == rx::LogicalChannel::Bnch;
            CHECK((is || r.blocks[1].channel == rx::LogicalChannel::SchHd));
            if (is) {
                ++bnch;
                CHECK(r.time.timeslot == 4 - ((mn + 3) % 4));
            }
            REQUIRE(r.blocks[1].sysinfo.has_value());
        }
        CHECK(bnch == 1);
    }
}

TEST_CASE("Half-slot bursts decode both blocks, and the AACH marks STCH and TCH")
{
    gen::CellConfig cell = gen::CellConfig::defaults();
    const uint32_t code = gen::scrambling_init(901, 1, 1);
    rx::LowerMac mac;
    // Learn the cell from the last sync burst of frame 18, so the next
    // slots are in frame 1 (frame 18 has no DL usage marker).
    cell.start = {4, 18, 1, 0};
    mac.decode(burst_of(bursts(cell, 1).front(), rx::BurstType::Sync));
    REQUIRE(mac.scrambling_code() == code);

    gen::MacSysinfo si = cell.sysinfo;
    const gen::Bits half1 = gen::encode_sch_hd(gen::encode_sysinfo(si), code);
    const gen::Bits half2 = gen::encode_sch_hd(gen::null_block(124), code);
    // ACCESS-ASSIGN header 11: field 1 DL usage marker 5 = traffic.
    const uint16_t traffic_aach = static_cast<uint16_t>((0b11u << 12) | (5u << 6) | 5u);
    for (const bool traffic : {false, true}) {
        const uint16_t aa = traffic ? traffic_aach : gen::encode_access_assign(cell.access_assign);
        const gen::Bits burst = gen::normal_downlink_burst(half1, gen::encode_aach(aa, code), half2, true);
        const rx::SlotResult r = mac.decode(burst_of(burst, rx::BurstType::NormalHalf));
        REQUIRE(r.blocks.size() == 2);
        CHECK(r.aach.decoded);
        CHECK(r.aach.info.traffic() == traffic);
        for (const auto& b : r.blocks) {
            CHECK(b.crc_ok);
            CHECK(b.channel == (traffic ? rx::LogicalChannel::Stch : rx::LogicalChannel::SchHd));
        }
        CHECK(r.blocks[0].mac.kind == rx::MacPduKind::MacSysinfo);
        CHECK(r.blocks[0].sysinfo.has_value());
        CHECK(r.blocks[1].mac.kind == rx::MacPduKind::NullPdu);
        CHECK(r.blocks[0].block == 1);
        CHECK(r.blocks[1].block == 2);
    }
    // A full-slot burst on a traffic slot is TCH: counted, never decoded.
    const gen::Bits full = gen::encode_sch_f(gen::null_block(268), code);
    const gen::Bits bkn1(full.begin(), full.begin() + 216);
    const gen::Bits bkn2(full.begin() + 216, full.end());
    const rx::SlotResult r = mac.decode(burst_of(
        gen::normal_downlink_burst(bkn1, gen::encode_aach(traffic_aach, code), bkn2, false),
        rx::BurstType::NormalFull));
    REQUIRE(r.blocks.size() == 1);
    CHECK(r.blocks[0].channel == rx::LogicalChannel::Tch);
    CHECK_FALSE(r.blocks[0].crc_ok);
}

TEST_CASE("AACH: ACCESS-ASSIGN fields per header, frame 18 meanings (DEC-05)")
{
    gen::CellConfig cell = gen::CellConfig::defaults();
    const uint32_t code = gen::scrambling_init(901, 1, 1);
    struct Case {
        gen::AccessAssign aa;
        std::optional<uint8_t> dl;
        std::optional<uint8_t> ul;
        bool access1;
        bool access2;
    };
    const std::vector<Case> cases = {
        {{{1, 3}, {2, 5}, 0, 0, 0}, std::nullopt, std::nullopt, true, true},
        {{{0, 0}, {2, 5}, 1, 2, 0}, 2, std::nullopt, false, true},
        {{{0, 0}, {3, 1}, 2, 1, 0}, 1, std::nullopt, false, true},
        {{{0, 0}, {0, 0}, 3, 9, 4}, 9, 4, false, false},
    };
    for (const Case& c : cases) {
        CAPTURE(c.aa.header);
        const uint16_t word = gen::encode_access_assign(c.aa);
        const gen::Bits bits = gen::to_bits(word, 14);
        const rx::AccessAssignInfo a = rx::parse_access_assign(bits.data(), false);
        CHECK(a.header == c.aa.header);
        CHECK(a.dl_usage == c.dl);
        CHECK(a.ul_usage == c.ul);
        CHECK(a.access1 == c.access1);
        CHECK(a.access2 == c.access2);
        CHECK(a.traffic() == (c.dl && *c.dl >= 4));
    }
    // Frame 18: no downlink usage marker in any header.
    const gen::Bits f18 = gen::to_bits((0b11u << 12) | (5u << 6) | 5u, 14);
    CHECK_FALSE(rx::parse_access_assign(f18.data(), true).dl_usage.has_value());

    // Up to 3 wrong bits in the coded AACH are corrected in the burst.
    cell.start = {4, 18, 1, 0};
    rx::LowerMac mac;
    mac.decode(burst_of(bursts(cell, 1).front(), rx::BurstType::Sync));
    const gen::Bits full = gen::encode_sch_f(gen::null_block(268), code);
    const gen::Bits bkn1(full.begin(), full.begin() + 216);
    const gen::Bits bkn2(full.begin() + 216, full.end());
    const uint16_t aa = static_cast<uint16_t>((0b01u << 12) | (2u << 6) | 7u);
    for (int errors = 0; errors <= 4; ++errors) {
        gen::Bits bb = gen::encode_aach(aa, code);
        for (int e = 0; e < errors; ++e)
            bb[static_cast<std::size_t>(3 + 7 * e)] ^= 1u;
        const rx::SlotResult r = mac.decode(burst_of(gen::normal_downlink_burst(bkn1, bb, bkn2, false),
                                                     rx::BurstType::NormalFull));
        CAPTURE(errors);
        if (errors <= 3) {
            REQUIRE(r.aach.decoded);
            CHECK(r.aach.corrected == errors);
            CHECK(r.aach.info.dl_usage == 2);
            CHECK(gen::get_bits(gen::Bits(r.aach.bits.begin(), r.aach.bits.end()), 0, 14) == aa);
        } else if (r.aach.decoded) {
            CHECK(r.aach.corrected <= 3);
        }
    }
}

TEST_CASE("RM(30,14) corrects up to 3 bit errors and detects 4")
{
    std::mt19937_64 rng(11);
    for (int n = 0; n < 2000; ++n) {
        const uint16_t info = static_cast<uint16_t>(rng() & 0x3FFF);
        const uint32_t word = gen::rm3014_encode(info);
        const int errors = n % 5;
        uint32_t e = 0;
        while (std::popcount(e) < errors)
            e |= 1u << (rng() % 30);
        const auto d = rx::rm3014_decode(word ^ e);
        if (errors <= 3) {
            REQUIRE(d.has_value());
            CHECK(d->first == info);
            CHECK(d->second == errors);
        } else if (d) {
            CHECK(d->second <= 3);
        }
    }
}

TEST_CASE("Encrypted MAC-RESOURCE PDUs are flagged, never decoded (DEC-10)")
{
    // MAC-RESOURCE: type 00, fill 0, grant 0, encryption mode 01, random
    // access 0, length 8 octets, address type 001 + broadcast address.
    gen::Bits b = gen::to_bits(0b00'0'0'01'0, 7);
    const gen::Bits len = gen::to_bits(8, 6);
    b.insert(b.end(), len.begin(), len.end());
    const gen::Bits addr = gen::to_bits(1, 3);
    b.insert(b.end(), addr.begin(), addr.end());
    const gen::Bits ssi = gen::to_bits(0xFFFFFF, 24);
    b.insert(b.end(), ssi.begin(), ssi.end());
    b.resize(268, 0);
    const rx::MacPduInfo m = rx::parse_mac_pdu(b.data(), 268, rx::LogicalChannel::SchF);
    CHECK(m.kind == rx::MacPduKind::MacResource);
    CHECK(m.encrypted);
    CHECK_FALSE(m.addressed);
    CHECK(m.length_octets == 8);

    // An individual address is only reported as "addressed".
    gen::Bits a = b;
    for (int i = 0; i < 24; ++i)
        a[static_cast<std::size_t>(16 + i)] = static_cast<uint8_t>((0x123456 >> (23 - i)) & 1);
    a[4] = a[5] = 0;  // clear
    const rx::MacPduInfo ma = rx::parse_mac_pdu(a.data(), 268, rx::LogicalChannel::SchF);
    CHECK(ma.kind == rx::MacPduKind::MacResource);
    CHECK(ma.addressed);
    CHECK_FALSE(ma.encrypted);

    // The generator's encrypted broadcast block, and the other MAC kinds.
    const gen::Bits enc = gen::mac_resource_block(gen::to_bits(0xABCD, 16), 268, {.encryption_mode = 2});
    CHECK(rx::parse_mac_pdu(enc.data(), 268, rx::LogicalChannel::SchF).encrypted);
    CHECK(rx::parse_mac_pdu(gen::null_block(268).data(), 268, rx::LogicalChannel::SchF).kind ==
          rx::MacPduKind::NullPdu);
    const gen::Bits si = gen::encode_sysinfo(gen::CellConfig::defaults().sysinfo);
    CHECK(rx::parse_mac_pdu(si.data(), 124, rx::LogicalChannel::Bnch).kind == rx::MacPduKind::MacSysinfo);
    CHECK(rx::pdu_kind_name(rx::MacPduKind::MacEnd) == "MAC-END");
}

TEST_CASE("SYNC and SYSINFO parsers give back every generator field (DEC-02, DEC-03)")
{
    std::mt19937_64 rng(20261103);
    for (int n = 0; n < 200; ++n) {
        gen::MacSync s;
        s.system_code = static_cast<uint8_t>(rng() % 16);
        s.colour_code = static_cast<uint8_t>(rng() % 64);
        s.timeslot = static_cast<uint8_t>(rng() % 4 + 1);
        s.frame = static_cast<uint8_t>(rng() % 18 + 1);
        s.multiframe = static_cast<uint8_t>(rng() % 60 + 1);
        s.sharing_mode = static_cast<uint8_t>(rng() % 4);
        s.ts_reserved_frames = static_cast<uint8_t>(rng() % 8);
        s.u_plane_dtx = rng() % 2 != 0;
        s.frame18_extension = rng() % 2 != 0;
        s.mle.mcc = static_cast<uint16_t>(rng() % 1024);
        s.mle.mnc = static_cast<uint16_t>(rng() % 16384);
        s.mle.neighbour_cell_broadcast = static_cast<uint8_t>(rng() % 4);
        s.mle.cell_service_level = static_cast<uint8_t>(rng() % 4);
        s.mle.late_entry_supported = rng() % 2 != 0;
        const gen::Bits sb = gen::encode_sync(s);
        const auto d = rx::parse_sync(sb.data());
        REQUIRE(d.has_value());
        CHECK(d->system_code == s.system_code);
        CHECK(d->colour_code == s.colour_code);
        CHECK(d->timeslot == s.timeslot);
        CHECK(d->frame == s.frame);
        CHECK(d->multiframe == s.multiframe);
        CHECK(d->sharing_mode == s.sharing_mode);
        CHECK(d->ts_reserved_frames == s.ts_reserved_frames);
        CHECK(d->u_plane_dtx == s.u_plane_dtx);
        CHECK(d->frame18_extension == s.frame18_extension);
        CHECK(d->mcc == s.mle.mcc);
        CHECK(d->mnc == s.mle.mnc);
        CHECK(d->neighbour_broadcast == s.mle.neighbour_cell_broadcast);
        CHECK(d->cell_service_level == s.mle.cell_service_level);
        CHECK(d->late_entry == s.mle.late_entry_supported);

        gen::MacSysinfo i;
        i.main_carrier = static_cast<uint16_t>(rng() % 4096);
        i.frequency_band = static_cast<uint8_t>(rng() % 16);
        i.offset = static_cast<uint8_t>(rng() % 4);
        i.duplex_spacing = static_cast<uint8_t>(rng() % 8);
        i.reverse_operation = rng() % 2 != 0;
        i.num_common_secondary_cch = static_cast<uint8_t>(rng() % 4);
        i.ms_txpwr_max_cell = static_cast<uint8_t>(rng() % 8);
        i.rxlev_access_min = static_cast<uint8_t>(rng() % 16);
        i.access_parameter = static_cast<uint8_t>(rng() % 16);
        i.radio_downlink_timeout = static_cast<uint8_t>(rng() % 16);
        i.cck_flag = rng() % 4 == 0;
        i.hyperframe_or_cck = static_cast<uint16_t>(rng() % 65536);
        i.option = static_cast<gen::SysinfoOption>(rng() % 4);
        i.option_value = static_cast<uint32_t>(rng() % (1u << 20));
        i.mle.location_area = static_cast<uint16_t>(rng() % 16384);
        i.mle.subscriber_class = static_cast<uint16_t>(rng() % 65536);
        i.mle.bs_service_details.air_interface_encryption = rng() % 2 != 0;
        i.mle.bs_service_details.system_wide_services = rng() % 2 != 0;
        i.mle.bs_service_details.priority_cell = rng() % 2 != 0;
        const gen::Bits si = gen::encode_sysinfo(i);
        const auto e = rx::parse_sysinfo(si.data());
        REQUIRE(e.has_value());
        CHECK(e->main_carrier == i.main_carrier);
        CHECK(e->frequency_band == i.frequency_band);
        CHECK(e->offset == i.offset);
        CHECK(e->duplex_spacing == i.duplex_spacing);
        CHECK(e->reverse_operation == i.reverse_operation);
        CHECK(e->common_scch == i.num_common_secondary_cch);
        CHECK(e->ms_txpwr_max_cell == i.ms_txpwr_max_cell);
        CHECK(e->rxlev_access_min == i.rxlev_access_min);
        CHECK(e->access_parameter == i.access_parameter);
        CHECK(e->radio_downlink_timeout == i.radio_downlink_timeout);
        CHECK(e->hyperframe_present == !i.cck_flag);
        CHECK(e->hyperframe == (i.cck_flag ? 0 : i.hyperframe_or_cck));  // a key id is never kept
        CHECK(e->option == static_cast<uint8_t>(i.option));
        CHECK(e->option_value == i.option_value);
        CHECK(e->location_area == i.mle.location_area);
        CHECK(e->subscriber_class == i.mle.subscriber_class);
        CHECK(e->service_details == i.mle.bs_service_details.pack());
        CHECK(e->air_encryption() == i.mle.bs_service_details.air_interface_encryption);
        CHECK(e->system_wide_services() == i.mle.bs_service_details.system_wide_services);
        CHECK(e->priority_cell() == i.mle.bs_service_details.priority_cell);
        CHECK(e->downlink_hz() == static_cast<uint64_t>(gen::downlink_frequency_hz(i.frequency_band,
                                                                                    i.main_carrier, i.offset)));
    }
    // Not a SYSINFO PDU.
    CHECK_FALSE(rx::parse_sysinfo(gen::null_block(124).data()).has_value());
    // Impossible frame number.
    gen::Bits bad = gen::encode_sync(gen::MacSync{});
    for (int k = 0; k < 5; ++k)
        bad[static_cast<std::size_t>(12 + k)] = 0;  // frame 0
    CHECK_FALSE(rx::parse_sync(bad.data()).has_value());
}

TEST_CASE("Serving-cell parameter set, duplex spacing and security class (DEC-03, DEC-10)")
{
    gen::CellConfig cell = gen::CellConfig::defaults();
    cell.sync.system_code = 2;
    cell.sync.mle.mnc = 77;
    cell.sysinfo.main_carrier = 3611;
    cell.sysinfo.offset = 3;
    cell.sysinfo.duplex_spacing = 4;  // band 3: 18 MHz (TS 100 392-15 table 2)
    cell.sysinfo.reverse_operation = false;
    cell.sysinfo.ms_txpwr_max_cell = 4;
    cell.sysinfo.rxlev_access_min = 3;
    cell.sysinfo.access_parameter = 5;
    cell.sysinfo.mle.location_area = 0x1A2;
    cell.sysinfo.mle.subscriber_class = 0xF00F;
    cell.sysinfo.mle.bs_service_details.system_wide_services = false;
    gen::ExtendedServices ext;
    ext.security_information = 0b01100000;  // class 1 + class 2 or 3
    cell.sysinfo.option = gen::SysinfoOption::ExtendedServices;
    cell.sysinfo.option_value = ext.pack();
    cell.sysinfo.mle.bs_service_details.air_interface_encryption = true;
    const auto s = rx::parse_sync(gen::encode_sync(cell.sync).data());
    const auto i = rx::parse_sysinfo(gen::encode_sysinfo(cell.sysinfo).data());
    REQUIRE(s.has_value());
    REQUIRE(i.has_value());
    const rx::ServingCellParams p = rx::serving_cell_params(*s, *i);
    CHECK(p.mcc == 901);
    CHECK(p.mnc == 77);
    CHECK(p.colour_code == 1);
    CHECK(p.system_code == 2);
    CHECK(p.location_area == 0x1A2);
    CHECK(p.main_carrier == 3611);
    CHECK(p.offset_hz == 12500);
    CHECK(p.downlink_hz == 390275000u + 12500u);
    CHECK(p.mcch_carrier == 3611);
    CHECK(p.mcch_hz == p.downlink_hz);
    REQUIRE(p.duplex_spacing_hz.has_value());
    CHECK(*p.duplex_spacing_hz == 18000000u);
    REQUIRE(p.uplink_hz.has_value());
    CHECK(*p.uplink_hz == p.downlink_hz - 18000000u);
    CHECK_FALSE(p.reverse_operation);
    CHECK(p.ms_txpwr_max_dbm == 30);
    CHECK(p.rxlev_access_min_dbm == -110);
    CHECK(p.access_parameter_dbm == -43);
    CHECK(p.trunking == rx::TrunkingMode::LocalSite);
    CHECK(p.subscriber_class == 0xF00F);
    CHECK(p.air_encryption);
    CHECK(p.security_class == 3);
    CHECK(p.security_information == 0b01100000);
    CHECK(p.hyperframe.has_value());
    CHECK_FALSE(p.reselect.has_value());
    CHECK_FALSE(p.cell_load_percent.has_value());

    // Reverse operation: RX above TX; 36 and 45 MHz spacings are kept
    // (osmo-tetra's int16_t table wraps them).
    CHECK(rx::duplex_spacing_hz(3, 1) == 36000000u);
    CHECK(rx::duplex_spacing_hz(8, 1) == 45000000u);
    CHECK_FALSE(rx::duplex_spacing_hz(0, 0).has_value());
    rx::SysinfoInfo r = *i;
    r.reverse_operation = true;
    CHECK(r.uplink_hz() == r.downlink_hz() + 18000000u);
    // Without air-interface encryption: class 1; encryption without the
    // extended services information: class 2.
    r.service_details = 0;
    CHECK(r.security_class() == 1);
    r.service_details = 0x002;
    r.option = 0;
    CHECK(r.security_class() == 2);
}

TEST_CASE("osmo-tetra's own test PDUs (testpdu.c) parse and survive a round trip (RAD-123)")
{
    testpdu_init();
    // SYNC: MCC 262, MNC 42, colour code 0, TN 1, FN 1, MN 1.
    const gen::Bits sync = unpack(pdu_sync, 60);
    const auto s = rx::parse_sync(sync.data());
    REQUIRE(s.has_value());
    CHECK(s->mcc == 262);
    CHECK(s->mnc == 42);
    CHECK(s->colour_code == 0);
    CHECK(s->timeslot == 1);
    CHECK(s->frame == 1);
    CHECK(s->multiframe == 1);

    // SYSINFO: main carrier (392.775 - 300) / 0.025 = 3711, band 3,
    // MS_TXPWR_MAX_CELL 15 dBm, RXLEV_ACCESS_MIN -125 dBm, ACCESS_PARAMETER
    // -53 dBm, hyperframe 0, LA 0, subscriber class 0xFFFF.
    const gen::Bits si = unpack(pdu_sysinfo, 124);
    const auto i = rx::parse_sysinfo(si.data());
    REQUIRE(i.has_value());
    CHECK(i->main_carrier == 3711);
    CHECK(i->frequency_band == 3);
    CHECK(i->downlink_hz() == 392775000u);
    CHECK(i->ms_txpwr_max_dbm() == 15);
    CHECK(i->rxlev_access_min_dbm() == -125);
    CHECK(i->access_parameter_dbm() == -53);
    CHECK(i->hyperframe_present);
    CHECK(i->hyperframe == 0);
    CHECK(i->location_area == 0);
    CHECK(i->subscriber_class == 0xFFFF);
    CHECK(i->option == 0);

    // ACCESS-ASSIGN: 2 alignment bits, header 00 and two zero fields.
    const gen::Bits aa = unpack(pdu_acc_ass, 14, 2);
    const rx::AccessAssignInfo a = rx::parse_access_assign(aa.data(), false);
    CHECK(a.header == 0);
    CHECK(a.field1 == 0);
    CHECK_FALSE(a.dl_usage.has_value());

    // Round trip: the test PDUs coded by the generator, decoded by the
    // wrapper on osmo-tetra (colour code 0 of the test SYNC PDU).
    const uint32_t code = gen::scrambling_init(262, 42, 0);
    const gen::Bits burst = gen::sync_downlink_burst(
        gen::encode_bsch(sync), gen::encode_aach(static_cast<uint16_t>(gen::get_bits(aa, 0, 14)), code),
        gen::encode_sch_hd(si, code));
    rx::LowerMac mac;
    const rx::SlotResult r = mac.decode(burst_of(burst, rx::BurstType::Sync));
    REQUIRE(r.blocks.size() == 2);
    CHECK(r.blocks[0].crc_ok);
    CHECK(r.blocks[1].crc_ok);
    CHECK(r.blocks[0].bit_errors == 0);
    CHECK(r.blocks[0].coded_bits == 120);
    CHECK(std::equal(sync.begin(), sync.end(), r.blocks[0].bits.begin()));
    CHECK(std::equal(si.begin(), si.end(), r.blocks[1].bits.begin()));
    CHECK(r.aach.decoded);
    CHECK(r.time_known);
    CHECK(r.time.frame == 1);
    CHECK(r.time.hyperframe_known);
}
