// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// osmo-tetra cross-checks of the generator's bursts and modulated samples
// (RAD-70, RAD-93, RAD-101):
// - the generator's continuous downlink bursts have osmo-tetra's layout:
//   its training-sequence search (phy/tetra_burst.c) finds the sync or
//   normal training sequence at osmo-tetra's offset, and its burst
//   splitter (tetra_burst_rx_cb) returns the generator's SB1/BB/SB2 or
//   BKN1+BKN2/BB fields. (osmo-tetra's burst builders are not used: at
//   59fb603 put_phase_adj_bits() indexes its phase2bits table without the
//   PHASE() offset, out of bounds; the generator's phase adjustment bits
//   are checked against clause 9.4.4.3.6 in tests/gen/test_burst.cpp.)
// - demodulation round trip: a cell transmitter's samples (level,
//   frequency and timing offset, noise) are demodulated by the test-only
//   demodulator of tests/gen (ref_demod.*, symbol timing and frequency
//   known), and the bits go through osmo-tetra's own burst synchroniser
//   (phy/tetra_burst_sync.c: training-sequence search and lock) and burst
//   splitter (tetra_burst_rx_cb); every block it delivers decodes with
//   osmo-tetra's receive chain with a good CRC and the expected contents.
// Partly copied from yantra-v1 c24e8f5 (tests/tetra_gen/test_cell_tx.cpp,
// tp_sap_capture.cpp), adapted for RAD-93.

#include "osmo_support.hpp"
#include "ref_demod.hpp"

#include "yantra/gen/burst.hpp"
#include "yantra/gen/cell_tx.hpp"
#include "yantra/gen/coding.hpp"
#include "yantra/gen/pdu.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <vector>

extern "C" {
#include <lower_mac/crc_simple.h>
#include <lower_mac/tetra_conv_enc.h>
#include <lower_mac/tetra_interleave.h>
#include <lower_mac/tetra_scramb.h>
#include <lower_mac/viterbi.h>
#include <phy/tetra_burst.h>
#include <phy/tetra_burst_sync.h>
#include <tetra_common.h>
#include <tetra_mac_pdu.h>

// Defined in phy/tetra_burst.c, declared only inside tetra_burst_sync.c.
void tetra_burst_rx_cb(const uint8_t* burst, unsigned int len, enum tetra_train_seq type, void* priv);
}

using namespace yantra::gen;
using cf = std::complex<float>;

using osmo_test::CapturedBlock;
using osmo_test::captured_blocks;
using osmo_test::osmo_decode;

std::vector<CapturedBlock>& osmo_test::captured_blocks()
{
    static std::vector<CapturedBlock> blocks;
    return blocks;
}

// osmo-tetra's TP-SAP entry point (normally its lower MAC): records every
// block the PHY delivers.
extern "C" void tp_sap_udata_ind(enum tp_sap_data_type type, int blk_num, const uint8_t* bits, unsigned int len,
                                 void* priv)
{
    (void)priv;
    captured_blocks().push_back({static_cast<int>(type), blk_num, Bits(bits, bits + len)});
}

namespace {

constexpr uint16_t kOsmoCrcOk = 0x1D0F;  // TETRA_CRC_OK in osmo-tetra

Bits random_bits(std::mt19937& rng, size_t n)
{
    Bits b(n);
    for (auto& v : b)
        v = static_cast<uint8_t>(rng() & 1u);
    return b;
}

// osmo-tetra's training-sequence search over one burst (padded: the
// search looks 21 bits ahead), restricted to `mask`.
int find_train_seq(const Bits& burst, uint32_t mask, unsigned& offset)
{
    Bits padded = burst;
    padded.resize(burst.size() + 64, 0);
    return tetra_find_train_seq(padded.data(), static_cast<unsigned>(burst.size()), mask, &offset);
}

Bits concat(const Bits& a, const Bits& b)
{
    Bits c = a;
    c.insert(c.end(), b.begin(), b.end());
    return c;
}

}  // namespace

Bits osmo_test::osmo_decode(Bits type5, const BlockSpec& spec, uint32_t scrambling, bool& crc_ok)
{
    tetra_scramb_bits(scrambling, type5.data(), static_cast<int>(type5.size()));
    std::vector<uint8_t> type3(type5.size());
    block_deinterleave(static_cast<uint32_t>(type5.size()), static_cast<uint32_t>(spec.a), type5.data(),
                       type3.data());
    const int type2_bits = spec.type1 + kCrcBits + kTailBits;
    std::vector<uint8_t> dp(static_cast<size_t>(type2_bits) * 4, 0xff);
    tetra_rcpc_depunct(TETRA_RCPC_PUNCT_2_3, type3.data(), static_cast<int>(type3.size()), dp.data());
    std::vector<uint8_t> type2(static_cast<size_t>(type2_bits));
    viterbi_dec_sb1_wrapper(dp.data(), type2.data(), static_cast<unsigned>(type2_bits));
    crc_ok = crc16_ccitt_bits(type2.data(), static_cast<unsigned>(spec.type1 + kCrcBits)) == kOsmoCrcOk;
    return Bits(type2.begin(), type2.begin() + spec.type1);
}


TEST_CASE("Bursts have osmo-tetra's layout: its training-sequence search and burst splitter")
{
    std::mt19937 rng(31);
    for (int round = 0; round < 20; ++round) {
        CAPTURE(round);
        const Bits sb1 = random_bits(rng, 120);
        const Bits bb = random_bits(rng, 30);
        const Bits sb2 = random_bits(rng, 216);
        const Bits sync = sync_downlink_burst(sb1, bb, sb2);
        unsigned offset = 0;
        CHECK(find_train_seq(sync, 1u << TETRA_TRAIN_SYNC, offset) == TETRA_TRAIN_SYNC);
        CHECK(offset == 214);  // y after q, HC, f and SB1
        captured_blocks().clear();
        tetra_burst_rx_cb(sync.data(), kSlotBits, TETRA_TRAIN_SYNC, nullptr);
        REQUIRE(captured_blocks().size() == 3);
        CHECK(captured_blocks()[0].type == TPSAP_T_SB1);
        CHECK(captured_blocks()[0].bits == sb1);
        CHECK(captured_blocks()[1].type == TPSAP_T_BBK);
        CHECK(captured_blocks()[1].bits == bb);
        CHECK(captured_blocks()[2].type == TPSAP_T_SB2);
        CHECK(captured_blocks()[2].bits == sb2);

        const Bits bkn1 = random_bits(rng, 216);
        const Bits bkn2 = random_bits(rng, 216);
        const uint32_t normal = (1u << TETRA_TRAIN_NORM_1) | (1u << TETRA_TRAIN_NORM_2);
        for (bool two : {false, true}) {
            CAPTURE(two);
            const Bits b = normal_downlink_burst(bkn1, bb, bkn2, two);
            const int type = find_train_seq(b, normal, offset);
            CHECK(type == (two ? TETRA_TRAIN_NORM_2 : TETRA_TRAIN_NORM_1));
            CHECK(offset == 244);  // n or p after q, HA, BKN1 and BB 1..14
            captured_blocks().clear();
            tetra_burst_rx_cb(b.data(), kSlotBits, static_cast<tetra_train_seq>(type), nullptr);
            if (two) {
                REQUIRE(captured_blocks().size() == 3);
                CHECK(captured_blocks()[0].bits == bb);
                CHECK(captured_blocks()[1].type == TPSAP_T_NDB);
                CHECK(captured_blocks()[1].bits == bkn1);
                CHECK(captured_blocks()[2].type == TPSAP_T_NDB);
                CHECK(captured_blocks()[2].bits == bkn2);
            } else {
                REQUIRE(captured_blocks().size() == 2);
                CHECK(captured_blocks()[0].type == TPSAP_T_BBK);
                CHECK(captured_blocks()[0].bits == bb);
                CHECK(captured_blocks()[1].type == TPSAP_T_SCH_F);
                CHECK(captured_blocks()[1].bits == concat(bkn1, bkn2));
            }
        }
    }
}

TEST_CASE("Demodulated cell transmitter samples pass osmo-tetra's burst sync and decode with good CRCs")
{
    CellConfig cfg = CellConfig::defaults();
    cfg.sync.colour_code = 13;
    cfg.sync.mle.mcc = 901;
    cfg.sync.mle.mnc = 77;
    cfg.sysinfo.mle.location_area = 4660;
    cfg.sysinfo.ms_txpwr_max_cell = 6;
    NwrkBroadcast nb;
    nb.network_time = NetworkTime{};
    NeighbourCell n;
    n.cell_id = 2;
    n.main_carrier = 3602;
    n.location_area = 4661;
    nb.neighbours = std::vector<NeighbourCell>{n};
    cfg.nwrk_broadcast = nb;
    cfg.nwrk_every_frames = 2;
    cfg.start = {1, 17, 1, 5};

    const double fs = 72000.0;
    const CarrierSettings s{.offset_hz = 4000.0, .level_db = -3.0, .timing_offset_s = 0.7e-3};
    CellTransmitter tx(cfg, fs, s);
    const size_t slots = 4 * 25;  // frame 17 of MN 1 .. frame 5 of MN 2
    const size_t symbols = slots * kSlotSymbols;
    const double delay_symbols = Pi4DqpskModulator::kHalfSpanSymbols + s.timing_offset_s * kSymbolRate;
    std::vector<cf> iq(static_cast<size_t>(std::ceil((static_cast<double>(symbols) + delay_symbols + 8.0) * 4.0)));
    tx.read(iq.data(), iq.size());
    // Noise: 25 dB below the carrier in the 72 kHz band.
    std::mt19937 rng(41);
    std::normal_distribution<float> noise(0.0f, std::sqrt(std::pow(10.0f, -2.8f) / 2.0f));
    for (cf& v : iq)
        v += cf(noise(rng), noise(rng));

    const auto z = ref::matched_symbols(iq, fs, delay_symbols, 0, symbols, s.offset_hz);
    Bits bits = ref::differential_decode(z, {1.0, 0.0});
    REQUIRE(bits.size() == slots * kSlotBits);
    // The demodulated bits are the transmitted bursts.
    Bits sent;
    TdmaTime t = cfg.start;
    for (size_t i = 0; i < slots; ++i, t.advance()) {
        const Bits b = downlink_burst(cfg, t);
        sent.insert(sent.end(), b.begin(), b.end());
    }
    CHECK(bits == sent);

    captured_blocks().clear();
    tetra_rx_state trs{};
    for (size_t pos = 0; pos < bits.size(); pos += 200)
        tetra_burst_sync_in(&trs, bits.data() + pos, static_cast<unsigned>(std::min<size_t>(200, bits.size() - pos)));

    const uint32_t cell = cfg.scrambling();
    int sb1 = 0, sb2 = 0, bbk = 0, schf = 0, nwrk = 0, other = 0;
    for (const CapturedBlock& b : captured_blocks()) {
        bool ok = false;
        if (b.type == TPSAP_T_SB1) {
            const Bits t1 = osmo_decode(b.bits, kBsch, kScramblingInitBsch, ok);
            REQUIRE(ok);
            CHECK(get_bits(t1, 4, 6) == 13);   // colour code
            CHECK(get_bits(t1, 12, 5) == 18);  // frame
            CHECK(get_bits(t1, 31, 10) == 901);
            CHECK(get_bits(t1, 41, 14) == 77);
            ++sb1;
        } else if (b.type == TPSAP_T_SB2) {
            const Bits t1 = osmo_decode(b.bits, kSchHd, cell, ok);
            REQUIRE(ok);
            tetra_si_decoded si{};
            macpdu_decode_sysinfo(&si, t1.data());
            CHECK(si.main_carrier == 3600);
            CHECK(si.mle_si.la == 4660);
            CHECK(si.ms_txpwr_max_cell == 6);
            ++sb2;
        } else if (b.type == TPSAP_T_BBK) {
            Bits d = b.bits;
            tetra_scramb_bits(cell, d.data(), static_cast<int>(d.size()));
            CHECK(get_bits(d, 0, 14) == encode_access_assign(cfg.access_assign));  // RM(30,14) is systematic
            ++bbk;
        } else if (b.type == TPSAP_T_SCH_F) {
            const Bits t1 = osmo_decode(b.bits, kSchF, cell, ok);
            REQUIRE(ok);
            tetra_resrc_decoded r{};
            macpdu_decode_resource(&r, t1.data(), 0);
            if (r.addr.type == ADDR_TYPE_SSI)
                ++nwrk;
            ++schf;
        } else {
            ++other;
        }
    }
    INFO("SB1 " << sb1 << ", SB2 " << sb2 << ", BBK " << bbk << ", SCH/F " << schf << ", D-NWRK-BROADCAST " << nwrk);
    // 25 frames: FN 17 of MN 1, FN 18 (MN 1), FN 1..18 (MN 2), FN 1..5
    // (MN 3). The synchroniser locks on the first sync burst and delivers
    // every burst after it: 7 sync bursts, 88 normal bursts, D-NWRK-
    // BROADCAST on TS 1 of the odd frames (nwrk_every_frames 2): 9 + 3.
    CHECK(sb1 == 7);
    CHECK(sb2 == sb1);
    CHECK(schf == 88);
    CHECK(nwrk == 12);
    CHECK(bbk == sb1 + schf);
    CHECK(other == 0);
}
