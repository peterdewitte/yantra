// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden vectors of the TETRA test-signal generator (RAD-70, RAD-93),
// committed under golden/:
// - for every coded broadcast channel a fixed PDU, its type-1 bits and the
//   type-5 bits sent on air (one file per channel);
// - the SHA-256 digest of every coded block of one hyperframe's worth of
//   multiframes (60 x 18 x 4 slots) for the fixed default cell and for a
//   cell configuration drawn from a fixed seed (downlink.txt).
// Any change of the encoders' output fails here. Rewrite on purpose only,
// with the reason in the commit message: test_gen --update (or the build
// target update-golden-gen). The vectors are also decoded by osmo-tetra's
// lower MAC in tests/gen_osmo/.
// Copied from yantra 68ea20a (tests/tetra_gen/test_golden.cpp), adapted
// (no bursts or samples here, digests added).

#include "ref_decoder.hpp"
#include "ref_pdu.hpp"

#include "golden.hpp"

#include "yantra/gen/digest.hpp"
#include "yantra/gen/downlink.hpp"
#include "yantra/gen/coding.hpp"
#include "yantra/gen/pdu.hpp"

#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>

using namespace yantra::gen;
namespace fs = std::filesystem;

namespace {

// The fictitious test cell of the vectors: MCC 901, MNC 1, colour code 1,
// main carrier 3600 in band 3 (390.000 MHz).
constexpr uint16_t kMcc = 901;
constexpr uint16_t kMnc = 1;
constexpr uint8_t kColourCode = 1;
constexpr uint16_t kCarrier = 3600;

struct Vector {
    std::string name;         // file name without .txt
    std::string description;  // first comment line
    std::string channel;
    uint32_t scrambling;
    Bits type1;
    Bits type5;
};

std::string hex32(uint32_t v)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08X", v);
    return buf;
}

std::string render(const Vector& v)
{
    std::ostringstream s;
    // The tag names are split so that REUSE does not read them as this
    // file's own header.
    s << "# SPDX" "-FileCopyrightText: 2026 The Yantra contributors\n"
      << "# SPDX" "-License-Identifier: GPL-3.0-or-later\n"
      << "#\n"
      << "# TETRA generator golden vector (RAD-70): " << v.description << "\n"
      << "# Written by tests/gen/test_golden.cpp; rewrite only on purpose\n"
      << "# (test_gen --update, target update-golden-gen). Bits first transmitted first.\n"
      << "channel " << v.channel << "\n"
      << "scrambling " << hex32(v.scrambling) << "\n"
      << "type1 " << to_string(v.type1) << "\n"
      << "type5 " << to_string(v.type5) << "\n";
    return s.str();
}

std::map<std::string, std::string> parse(const std::string& text)
{
    std::map<std::string, std::string> kv;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        const auto sp = line.find(' ');
        if (sp != std::string::npos)
            kv[line.substr(0, sp)] = line.substr(sp + 1);
    }
    return kv;
}

Vector block_vector(std::string name, std::string description, const BlockSpec& spec, uint32_t scrambling,
                    const Bits& type1)
{
    return {std::move(name), std::move(description), std::string(spec.name), scrambling, type1,
            encode_block(type1, spec, scrambling)};
}

std::vector<Vector> vectors()
{
    const uint32_t cell = scrambling_init(kMcc, kMnc, kColourCode);
    std::vector<Vector> out;

    MacSync sync;
    sync.colour_code = kColourCode;
    sync.timeslot = 1;
    sync.frame = 18;
    sync.multiframe = 1;
    sync.mle.mcc = kMcc;
    sync.mle.mnc = kMnc;
    out.push_back(block_vector("bsch_sync", "BSCH (SB1), MAC-SYNC + D-MLE-SYNC, TN 1 FN 18 MN 1", kBsch,
                               kScramblingInitBsch, encode_sync(sync)));

    MacSysinfo sys;
    sys.main_carrier = kCarrier;
    sys.frequency_band = 3;
    sys.hyperframe_or_cck = 0;
    sys.option = SysinfoOption::ExtendedServices;
    sys.option_value = ExtendedServices{}.pack();
    sys.mle.location_area = 1;
    out.push_back(block_vector("bnch_sysinfo", "BNCH (SB2), MAC-SYSINFO + D-MLE-SYSINFO, LA 1", kSchHd,
                               cell, encode_sysinfo(sys)));

    out.push_back(block_vector("sch_hd_null", "SCH/HD half slot with a MAC Null PDU", kSchHd, cell,
                               null_block(kSchHd.type1)));

    NwrkBroadcast nb;
    nb.network_time = NetworkTime{};
    NeighbourCell n1;
    n1.cell_id = 1;
    n1.main_carrier = kCarrier + 4;
    n1.location_area = 2;
    NeighbourCell n2;
    n2.cell_id = 2;
    n2.main_carrier = kCarrier - 4;
    n2.location_area = 3;
    n2.max_ms_txpwr = 5;
    nb.neighbours = std::vector<NeighbourCell>{n1, n2};
    out.push_back(block_vector("sch_f_nwrk_broadcast",
                               "SCH/F, MAC-RESOURCE + BL-UDATA + D-NWRK-BROADCAST, 2 neighbours", kSchF, cell,
                               nwrk_broadcast_block(nb)));

    out.push_back(block_vector("sch_f_null", "SCH/F full slot with a MAC Null PDU", kSchF, cell,
                               null_block(kSchF.type1)));

    const uint16_t aa = encode_access_assign(AccessAssign{{0, 1}, {0, 1}});
    out.push_back({"aach_access_assign", "AACH, ACCESS-ASSIGN header 00, access code A, 1 frame", "AACH", cell,
                   to_bits(aa, 14), encode_aach(aa, cell)});
    return out;
}

}  // namespace

TEST_SUITE("golden")
{
    TEST_CASE("Golden vectors of every coded broadcast channel")
    {
        const bool update = yantra::tests::update_golden();
        const fs::path dir = YANTRA_GEN_GOLDEN_DIR;
        for (const Vector& v : vectors()) {
            CAPTURE(v.name);
            const fs::path file = dir / (v.name + ".txt");
            if (update) {
                std::ofstream(file, std::ios::binary) << render(v);
                MESSAGE("rewrote " << file.string());
                continue;
            }
            std::ifstream in(file, std::ios::binary);
            REQUIRE_MESSAGE(in.good(), "missing golden file; create it with test_gen --update");
            std::stringstream text;
            text << in.rdbuf();
            const auto kv = parse(text.str());
            CHECK(kv.at("channel") == v.channel);
            CHECK(kv.at("scrambling") == hex32(v.scrambling));
            CHECK(kv.at("type1") == to_string(v.type1));
            CHECK(kv.at("type5") == to_string(v.type5));
            CHECK(text.str() == render(v));
        }
    }

    TEST_CASE("Golden vectors decode with the reference decoder")
    {
        for (const Vector& v : vectors()) {
            CAPTURE(v.name);
            if (v.channel == "AACH") {
                int distance = -1;
                CHECK(ref::decode_aach(v.type5, v.scrambling, &distance) == get_bits(v.type1, 0, 14));
                CHECK(distance == 0);
                continue;
            }
            const BlockSpec& spec = v.channel == "BSCH" ? kBsch : v.channel == "SCH/F" ? kSchF : kSchHd;
            const auto d = ref::decode_block(v.type5, spec, v.scrambling);
            CHECK(d.crc_ok);
            CHECK(d.type1 == v.type1);
        }
    }
}

// ---- downlink digests ------------------------------------------------------

namespace {

constexpr uint32_t kSeed = 20261103;
constexpr int kSlotsPerHyperframe = kSlotsPerFrame * kFramesPerMultiframe * kMultiframesPerHyperframe;

// A cell configuration drawn from `seed` with the raw mt19937 output only
// (the standard distributions differ between standard libraries).
CellConfig seeded_cell(uint32_t seed)
{
    std::mt19937 rng(seed);
    const auto r = [&rng](int bits) { return static_cast<uint32_t>(rng() & ((1u << bits) - 1u)); };
    CellConfig c = CellConfig::defaults();
    c.sync.colour_code = static_cast<uint8_t>(1 + r(16) % 63);
    c.sync.mle.mcc = static_cast<uint16_t>(r(10));
    c.sync.mle.mnc = static_cast<uint16_t>(r(14));
    c.sync.mle.cell_service_level = static_cast<uint8_t>(r(2));
    c.sysinfo.main_carrier = static_cast<uint16_t>(r(12));
    c.sysinfo.frequency_band = static_cast<uint8_t>(r(4));
    c.sysinfo.offset = static_cast<uint8_t>(r(2));
    c.sysinfo.duplex_spacing = static_cast<uint8_t>(r(3));
    c.sysinfo.ms_txpwr_max_cell = static_cast<uint8_t>(r(3));
    c.sysinfo.rxlev_access_min = static_cast<uint8_t>(r(4));
    c.sysinfo.access_parameter = static_cast<uint8_t>(r(4));
    c.sysinfo.radio_downlink_timeout = static_cast<uint8_t>(r(4));
    ExtendedServices es;
    es.security_information = static_cast<uint8_t>(r(8));
    c.sysinfo.option_value = es.pack();
    c.sysinfo.mle.location_area = static_cast<uint16_t>(r(14));
    c.sysinfo.mle.subscriber_class = static_cast<uint16_t>(r(16));
    c.sysinfo.mle.bs_service_details = ref::parse_bs_service_details(static_cast<uint16_t>(r(12)));
    c.access_assign = AccessAssign{{static_cast<uint8_t>(r(2)), static_cast<uint8_t>(r(4))},
                                   {static_cast<uint8_t>(r(2)), static_cast<uint8_t>(r(4))}};

    NwrkBroadcast all;
    all.reselect = {static_cast<uint8_t>(r(4)), static_cast<uint8_t>(r(4)), static_cast<uint8_t>(r(4)),
                    static_cast<uint8_t>(r(4))};
    all.cell_load = static_cast<uint8_t>(r(2));
    all.network_time = NetworkTime{r(24), false, static_cast<uint8_t>(r(6)), 26, 0x7FF};
    std::vector<NeighbourCell> list;
    const int count = 1 + static_cast<int>(r(8) % 31);  // up to 31 neighbours (DEC-02)
    for (int i = 0; i < count; ++i) {
        NeighbourCell n;
        n.cell_id = static_cast<uint8_t>(i + 1);
        n.reselection_types = static_cast<uint8_t>(r(2));
        n.synchronised = r(1) != 0;
        n.cell_load = static_cast<uint8_t>(r(2));
        n.main_carrier = static_cast<uint16_t>(r(12));
        if (r(1))
            n.location_area = static_cast<uint16_t>(r(14));
        if (r(1))
            n.min_rx_access_level = static_cast<uint8_t>(r(4));
        if (r(1))
            n.max_ms_txpwr = static_cast<uint8_t>(r(3));
        if (r(1))
            n.carrier_extension = CarrierExtension{static_cast<uint8_t>(r(4)), static_cast<uint8_t>(r(2)),
                                                   static_cast<uint8_t>(r(3)), false};
        list.push_back(n);
    }
    all.neighbours = list;
    std::vector<NwrkBroadcast> pdus = split_nwrk_broadcast(all);
    c.nwrk_broadcast = pdus.front();
    c.nwrk_more.assign(pdus.begin() + 1, pdus.end());
    c.nwrk_every_frames = 1 + static_cast<int>(r(3));
    return c;
}

// SHA-256 over every slot from TN 1 FN 1 MN 1 HN `hn` for one hyperframe:
// per slot the content kind (1 byte) and the AACH, block 1 and block 2
// bits (1 byte per bit).
std::string downlink_digest(const CellConfig& cfg, uint16_t hn)
{
    Sha256 h;
    TdmaTime t{1, 1, 1, hn};
    for (int i = 0; i < kSlotsPerHyperframe; ++i, t.advance()) {
        const SlotBlocks b = downlink_blocks(cfg, t);
        const auto kind = static_cast<uint8_t>(b.content);
        h.update(&kind, 1);
        h.update(b.aach);
        h.update(b.block1);
        h.update(b.block2);
    }
    return h.hex();
}

std::string render_digests()
{
    std::ostringstream s;
    s << "# SPDX" "-FileCopyrightText: 2026 The Yantra contributors\n"
      << "# SPDX" "-License-Identifier: GPL-3.0-or-later\n"
      << "#\n"
      << "# TETRA generator golden digests (RAD-93): SHA-256 of the coded blocks\n"
      << "# (content kind, AACH, block 1, block 2) of " << kSlotsPerHyperframe
      << " slots from TN 1 FN 1 MN 1 HN 7.\n"
      << "# default: CellConfig::defaults() with two neighbours; seeded: drawn from the seed.\n"
      << "# Written by tests/gen/test_golden.cpp; rewrite only on purpose\n"
      << "# (test_gen --update, target update-golden-gen).\n";
    CellConfig def = CellConfig::defaults();
    NwrkBroadcast nb;
    nb.network_time = NetworkTime{};
    NeighbourCell n1;
    n1.cell_id = 1;
    n1.main_carrier = kCarrier + 4;
    n1.location_area = 2;
    NeighbourCell n2;
    n2.cell_id = 2;
    n2.main_carrier = kCarrier - 4;
    n2.location_area = 3;
    def.nwrk_broadcast = nb;
    def.nwrk_broadcast->neighbours = std::vector<NeighbourCell>{n1, n2};
    s << "digest default " << downlink_digest(def, 7) << "\n";
    s << "digest seeded-" << kSeed << " " << downlink_digest(seeded_cell(kSeed), 7) << "\n";
    return s.str();
}

}  // namespace

TEST_SUITE("golden")
{
    TEST_CASE("Golden digests of a hyperframe of coded downlink blocks")
    {
        const fs::path file = fs::path(YANTRA_GEN_GOLDEN_DIR) / "downlink.txt";
        const std::string text = render_digests();
        if (yantra::tests::update_golden()) {
            std::ofstream(file, std::ios::binary) << text;
            MESSAGE("rewrote " << file.string());
            return;
        }
        std::ifstream in(file, std::ios::binary);
        REQUIRE_MESSAGE(in.good(), "missing golden file; create it with test_gen --update");
        std::stringstream stored;
        stored << in.rdbuf();
        CHECK(stored.str() == text);
    }

    TEST_CASE("The seeded cell is reproducible and uses the whole schedule")
    {
        const CellConfig a = seeded_cell(kSeed);
        const CellConfig b = seeded_cell(kSeed);
        CHECK(a.sync == b.sync);
        CHECK(a.sysinfo == b.sysinfo);
        CHECK(a.nwrk_broadcast == b.nwrk_broadcast);
        CHECK(a.nwrk_more == b.nwrk_more);
        validate(a);
        CHECK(downlink_digest(a, 7) == downlink_digest(b, 7));
        CHECK(downlink_digest(a, 7) != downlink_digest(a, 8));  // hyperframe number in SYSINFO
        CHECK(downlink_digest(a, 7) != downlink_digest(seeded_cell(kSeed + 1), 7));
    }
}
