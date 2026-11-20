// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// osmo-tetra cross-checks of the generator (RAD-93, RAD-101): the
// generator's coding functions against osmo-tetra's independent
// implementation (pinned 59fb603), its coded blocks decoded with
// osmo-tetra's receive chain (descramble, deinterleave, depuncture,
// Viterbi, CRC; as in its lower_mac/tetra_lower_mac.c), its PDUs parsed by
// osmo-tetra's MAC PDU decoders, and osmo-tetra's own test vectors
// (src/testpdu.c) compared with the generator's encoders.
// Partly copied from yantra-v1 c24e8f5 (tests/tetra_gen/test_osmo_vectors.cpp),
// adapted for RAD-93.

#include "yantra/gen/coding.hpp"
#include "yantra/gen/downlink.hpp"
#include "yantra/gen/pdu.hpp"

#include <doctest/doctest.h>

#include <random>
#include <vector>

extern "C" {
#include <lower_mac/crc_simple.h>
#include <lower_mac/tetra_conv_enc.h>
#include <lower_mac/tetra_interleave.h>
#include <lower_mac/tetra_rm3014.h>
#include <lower_mac/tetra_scramb.h>
#include <lower_mac/viterbi.h>
#include <tetra_common.h>
#include <tetra_mac_pdu.h>
#include <testpdu.h>

// Implemented in lower_mac/tetra_rm3014.c, but declared there under the
// name tetra_rm3013_decode (typo at the pinned commit). It takes the 14
// systematic bits without correcting errors.
int tetra_rm3014_decode(const uint32_t inp, uint16_t* out);
}

using namespace yantra::gen;

namespace {

constexpr uint16_t kOsmoCrcOk = 0x1D0F;  // TETRA_CRC_OK in osmo-tetra

Bits random_bits(std::mt19937& rng, int n)
{
    Bits b(static_cast<size_t>(n));
    for (auto& v : b)
        v = static_cast<uint8_t>(rng() & 1u);
    return b;
}

Bits unpack(const uint8_t* bytes, size_t nbits)
{
    Bits b(nbits);
    for (size_t i = 0; i < nbits; ++i)
        b[i] = static_cast<uint8_t>((bytes[i / 8] >> (7 - i % 8)) & 1u);
    return b;
}

struct OsmoDecoded {
    Bits type1;
    bool crc_ok = false;
};

// osmo-tetra's receive chain of tetra_lower_mac.c for one block.
OsmoDecoded osmo_decode(const Bits& type5, const BlockSpec& spec, uint32_t scrambling)
{
    std::vector<uint8_t> type4 = type5;
    tetra_scramb_bits(scrambling, type4.data(), static_cast<int>(type4.size()));
    std::vector<uint8_t> type3(type4.size());
    block_deinterleave(static_cast<uint32_t>(type4.size()), static_cast<uint32_t>(spec.a), type4.data(),
                       type3.data());
    const int type2_bits = spec.type1 + kCrcBits + kTailBits;
    std::vector<uint8_t> type3dp(static_cast<size_t>(type2_bits) * 4, 0xff);
    tetra_rcpc_depunct(TETRA_RCPC_PUNCT_2_3, type3.data(), static_cast<int>(type3.size()), type3dp.data());
    std::vector<uint8_t> type2(static_cast<size_t>(type2_bits));
    viterbi_dec_sb1_wrapper(type3dp.data(), type2.data(), static_cast<unsigned>(type2_bits));
    OsmoDecoded d;
    d.crc_ok = crc16_ccitt_bits(type2.data(), static_cast<unsigned>(spec.type1 + kCrcBits)) == kOsmoCrcOk;
    d.type1.assign(type2.begin(), type2.begin() + spec.type1);
    return d;
}

}  // namespace

TEST_CASE("CRC-16: osmo-tetra accepts the generator's type-2 blocks")
{
    std::mt19937 rng(11);
    for (const BlockSpec* spec : {&kBsch, &kSchHd, &kSchF}) {
        for (int i = 0; i < 50; ++i) {
            Bits t2 = add_crc_and_tail(random_bits(rng, spec->type1));
            CHECK(crc16_ccitt_bits(t2.data(), static_cast<unsigned>(spec->type1 + kCrcBits)) == kOsmoCrcOk);
            t2[rng() % static_cast<unsigned>(spec->type1)] ^= 1u;
            CHECK(crc16_ccitt_bits(t2.data(), static_cast<unsigned>(spec->type1 + kCrcBits)) != kOsmoCrcOk);
        }
    }
}

TEST_CASE("Mother code and 2/3 puncturing equal osmo-tetra's encoder")
{
    std::mt19937 rng(12);
    for (const BlockSpec* spec : {&kBsch, &kSchHd, &kSchF}) {
        CAPTURE(spec->name);
        Bits t2 = add_crc_and_tail(random_bits(rng, spec->type1));
        const Bits mother = conv_encode_mother(t2);
        std::vector<uint8_t> osmo_mother(t2.size() * 4);
        conv_enc_state ces{};
        conv_enc_init(&ces);
        conv_enc_input(&ces, t2.data(), static_cast<int>(t2.size()), osmo_mother.data());
        CHECK(Bits(osmo_mother.begin(), osmo_mother.end()) == mother);
        const Bits type3 = puncture_2_3(mother);
        REQUIRE(static_cast<int>(type3.size()) == spec->type5);
        std::vector<uint8_t> osmo_type3(type3.size());
        get_punctured_rate(TETRA_RCPC_PUNCT_2_3, osmo_mother.data(), static_cast<int>(osmo_type3.size()),
                           osmo_type3.data());
        CHECK(Bits(osmo_type3.begin(), osmo_type3.end()) == type3);
    }
}

TEST_CASE("Block interleaving and scrambling equal osmo-tetra's")
{
    std::mt19937 rng(13);
    for (const BlockSpec* spec : {&kBsch, &kSchHd, &kSchF}) {
        CAPTURE(spec->name);
        const Bits in = random_bits(rng, spec->type5);
        std::vector<uint8_t> out(in.size());
        block_interleave(static_cast<uint32_t>(in.size()), static_cast<uint32_t>(spec->a), in.data(), out.data());
        CHECK(Bits(out.begin(), out.end()) == block_interleave(in, spec->a));
    }
    for (const auto& [mcc, mnc, cc] : {std::tuple<uint16_t, uint16_t, uint8_t>{901, 1, 1},
                                       {262, 42, 0},
                                       {1023, 16383, 63},
                                       {0, 0, 0}}) {
        const uint32_t init = scrambling_init(mcc, mnc, cc);
        CHECK(init == tetra_scramb_get_init(mcc, mnc, cc));
        std::vector<uint8_t> seq(432);
        tetra_scramb_get_bits(init, seq.data(), static_cast<int>(seq.size()));
        CHECK(Bits(seq.begin(), seq.end()) == scrambling_sequence(init, seq.size()));
    }
    CHECK(kScramblingInitBsch == SCRAMB_INIT);
}

TEST_CASE("RM(30,14): every code word equals osmo-tetra's and decodes with it")
{
    tetra_rm3014_init();
    for (uint32_t in = 0; in < (1u << 14); ++in) {
        const uint32_t cw = rm3014_encode(static_cast<uint16_t>(in));
        if (cw != tetra_rm3014_compute(static_cast<uint16_t>(in))) {
            FAIL_CHECK("code word differs for input " << in);
            break;
        }
    }
    std::mt19937 rng(14);
    for (int i = 0; i < 200; ++i) {
        const auto in = static_cast<uint16_t>(rng() & 0x3FFFu);
        uint16_t out = 0;
        CHECK(tetra_rm3014_decode(rm3014_encode(in), &out) == 0);
        CHECK(out == in);
    }
}

TEST_CASE("osmo-tetra's receive chain decodes every coded block of a multiframe")
{
    CellConfig c = CellConfig::defaults();
    NwrkBroadcast nb;
    NeighbourCell n;
    n.cell_id = 2;
    n.main_carrier = 3604;
    n.location_area = 7;
    nb.neighbours = std::vector<NeighbourCell>{n};
    c.nwrk_broadcast = nb;
    const uint32_t cell = c.scrambling();
    TdmaTime t{1, 1, 9, 77};
    for (int i = 0; i < 4 * 18; ++i, t.advance()) {
        CAPTURE(i);
        const SlotPdus p = downlink_pdus(c, t);
        const SlotBlocks b = downlink_blocks(c, t);
        if (p.content == SlotContent::Sync) {
            const OsmoDecoded d1 = osmo_decode(b.block1, kBsch, SCRAMB_INIT);
            CHECK(d1.crc_ok);
            CHECK(d1.type1 == p.first);
            const OsmoDecoded d2 = osmo_decode(b.block2, kSchHd, cell);
            CHECK(d2.crc_ok);
            CHECK(d2.type1 == p.second);
        } else {
            Bits sch_f = b.block1;
            sch_f.insert(sch_f.end(), b.block2.begin(), b.block2.end());
            const OsmoDecoded d = osmo_decode(sch_f, kSchF, cell);
            CHECK(d.crc_ok);
            CHECK(d.type1 == p.first);
        }
        // AACH: descramble, RM decode, ACCESS-ASSIGN parse.
        std::vector<uint8_t> bb = b.aach;
        tetra_scramb_bits(cell, bb.data(), static_cast<int>(bb.size()));
        uint32_t cw = 0;
        for (uint8_t v : bb)
            cw = (cw << 1) | v;
        uint16_t aa = 0;
        REQUIRE(tetra_rm3014_decode(cw, &aa) == 0);
        CHECK(aa == p.access_assign);
    }
}

TEST_CASE("osmo-tetra's MAC parsers read the generator's SYSINFO, ACCESS-ASSIGN and MAC-RESOURCE")
{
    std::mt19937 rng(20261103);
    const auto r = [&rng](int bits) { return static_cast<uint32_t>(rng() & ((1u << bits) - 1u)); };
    for (int i = 0; i < 1000; ++i) {
        CAPTURE(i);
        MacSysinfo p;
        p.main_carrier = static_cast<uint16_t>(r(12));
        p.frequency_band = static_cast<uint8_t>(r(4));
        p.offset = static_cast<uint8_t>(r(2));
        p.duplex_spacing = static_cast<uint8_t>(r(3));
        p.reverse_operation = r(1) != 0;
        p.num_common_secondary_cch = static_cast<uint8_t>(r(2));
        p.ms_txpwr_max_cell = static_cast<uint8_t>(r(3));
        p.rxlev_access_min = static_cast<uint8_t>(r(4));
        p.access_parameter = static_cast<uint8_t>(r(4));
        p.radio_downlink_timeout = static_cast<uint8_t>(r(4));
        p.hyperframe_or_cck = static_cast<uint16_t>(r(16));
        p.option = static_cast<SysinfoOption>(r(2));
        p.option_value = r(20);
        p.mle.location_area = static_cast<uint16_t>(r(14));
        p.mle.subscriber_class = static_cast<uint16_t>(r(16));
        const Bits b = encode_sysinfo(p);
        REQUIRE(b.size() == 124);
        tetra_si_decoded o{};
        macpdu_decode_sysinfo(&o, b.data());
        CHECK(o.main_carrier == p.main_carrier);
        CHECK(o.freq_band == p.frequency_band);
        CHECK(o.freq_offset == p.offset);
        CHECK(o.duplex_spacing == p.duplex_spacing);
        CHECK(o.reverse_operation == p.reverse_operation);
        CHECK(o.num_of_csch == p.num_common_secondary_cch);
        CHECK(o.ms_txpwr_max_cell == p.ms_txpwr_max_cell);
        CHECK(o.rxlev_access_min == p.rxlev_access_min);
        CHECK(o.access_parameter == p.access_parameter);
        CHECK(o.radio_dl_timeout == p.radio_downlink_timeout);
        CHECK(o.hyperframe_number == p.hyperframe_or_cck);
        CHECK(o.option_field == static_cast<uint8_t>(p.option));
        CHECK(o.mle_si.la == p.mle.location_area);
        CHECK(o.mle_si.subscr_class == p.mle.subscriber_class);
        CHECK(o.mle_si.bs_service_details == p.mle.bs_service_details.pack());
    }

    for (uint8_t code = 0; code < 4; ++code) {
        for (uint8_t len = 0; len < 16; ++len) {
            const AccessAssign a{{code, len}, {static_cast<uint8_t>(3 - code), static_cast<uint8_t>(15 - len)}};
            const Bits bits = to_bits(encode_access_assign(a), 14);
            tetra_acc_ass_decoded o{};
            macpdu_decode_access_assign(&o, bits.data(), 0);
            CHECK(o.hdr == 0);
            CHECK(o.access[0].access_code == code);
            CHECK(o.access[0].base_frame_len == len);
            CHECK(o.access[1].access_code == 3 - code);
            CHECK(o.access[1].base_frame_len == 15 - len);
        }
    }

    NwrkBroadcast nb;
    nb.neighbours = std::vector<NeighbourCell>{NeighbourCell{}};
    const Bits block = nwrk_broadcast_block(nb);
    const Bits sdu = llc_bl_udata(encode_nwrk_broadcast(nb));
    tetra_resrc_decoded rsd{};
    const int header = macpdu_decode_resource(&rsd, block.data(), 0);
    CHECK(header == kMacResourceHeaderBits);
    CHECK(rsd.addr.type == ADDR_TYPE_SSI);
    CHECK(rsd.addr.ssi == kBroadcastSsi);
    CHECK(rsd.encryption_mode == 0);
    CHECK(rsd.macpdu_length == (kMacResourceHeaderBits + static_cast<int>(sdu.size()) + 7) / 8);
    CHECK(Bits(block.begin() + header, block.begin() + header + static_cast<long>(sdu.size())) == sdu);

    const Bits null = null_block(kSchF.type1);
    tetra_resrc_decoded nrs{};
    CHECK(macpdu_decode_resource(&nrs, null.data(), 0) == 0);
    CHECK(nrs.addr.type == ADDR_TYPE_NULL);
    CHECK(nrs.macpdu_length == 2);
}

TEST_CASE("osmo-tetra testpdu.c: its SYNC and SYSINFO vectors equal the generator's encoders")
{
    testpdu_init();
    // SYNC: MCC 262, MNC 42, TN 1, FN 1, MN 1, everything else 0.
    MacSync s;
    s.colour_code = 0;
    s.timeslot = 1;
    s.frame = 1;
    s.multiframe = 1;
    s.mle.mcc = 262;
    s.mle.mnc = 42;
    s.mle.neighbour_cell_broadcast = 0;
    s.mle.late_entry_supported = false;
    CHECK(encode_sync(s) == unpack(pdu_sync, 60));

    // SYSINFO: carrier 3711 in band 3 (392.775 MHz), MS_TXPWR 1, option 0,
    // LA 0, subscriber class 0xFFFF, no service details.
    MacSysinfo p;
    p.main_carrier = 3711;
    p.frequency_band = 3;
    p.ms_txpwr_max_cell = 1;
    p.rxlev_access_min = 0;
    p.access_parameter = 0;
    p.radio_downlink_timeout = 0;
    p.option = SysinfoOption::EvenMultiframe;
    p.option_value = 0;
    p.mle.location_area = 0;
    p.mle.subscriber_class = 0xFFFF;
    p.mle.bs_service_details = BsServiceDetails{false, false, false, false, false, false,
                                                false, false, false, false, false, false};
    CHECK(encode_sysinfo(p) == unpack(pdu_sysinfo, 124));
    CHECK(downlink_frequency_hz(p.frequency_band, p.main_carrier, p.offset) == 392.775e6);
}

TEST_CASE("Downlink frequencies equal osmo-tetra's tetra_dl_carrier_hz")
{
    for (int band : {3, 4, 8})
        for (int carrier : {0, 1, 2000, 3600, 4095})
            for (int offset = 0; offset < 4; ++offset)
                CHECK(downlink_frequency_hz(band, carrier, offset) ==
                      static_cast<double>(tetra_dl_carrier_hz(static_cast<uint8_t>(band),
                                                              static_cast<uint16_t>(carrier),
                                                              static_cast<uint8_t>(offset))));
}

TEST_CASE("osmo-tetra crc_test.c: the SB1 vector's CRC equals the generator's")
{
    // input2 of src/crc_test.c: 60 type-1 bits, the 16-bit checksum, 4 tail bits.
    const Bits t2 = {0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
                     0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1,
                     1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0};
    REQUIRE(t2.size() == 80);
    const Bits type1(t2.begin(), t2.begin() + 60);
    CHECK(add_crc_and_tail(type1) == t2);
}
