// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round trip of yantra-gen's IQ files through osmo-tetra (RAD-71, RAD-93,
// RAD-101): the yantra-gen program writes a two-carrier cs16 recording
// with noise and a receiver frequency error; each carrier is taken from
// the file at the offset and frequency error of the ground truth, matched
// filtered by the test-only demodulator of tests/gen (symbol timing from
// the generator's documented start), and the bits go through osmo-tetra's
// own burst synchroniser and receive chain. Every block osmo-tetra
// delivers has a good CRC, and SYNC, SYSINFO and D-NWRK-BROADCAST carry
// what the ground-truth CSV and the cell CSV say. osmo-tetra is the
// independent cross-check oracle (owner, 2026-11-03).

#include "osmo_support.hpp"
#include "ref_demod.hpp"

#include "yantra/gen/modulator.hpp"
#include "yantra/gen/timing.hpp"

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include <lower_mac/tetra_scramb.h>
#include <phy/tetra_burst.h>
#include <phy/tetra_burst_sync.h>
#include <tetra_common.h>
#include <tetra_mac_pdu.h>
}

namespace fs = std::filesystem;
using namespace yantra::gen;
using osmo_test::captured_blocks;
using osmo_test::osmo_decode;
using cf = std::complex<float>;

namespace {

std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::map<std::string, std::string>> read_csv(const fs::path& path)
{
    std::istringstream in(slurp(path));
    std::string line;
    std::vector<std::string> head;
    std::vector<std::map<std::string, std::string>> rows;
    auto split = [](const std::string& l) {
        std::vector<std::string> v(1);
        for (char c : l) {
            if (c == ',')
                v.emplace_back();
            else
                v.back() += c;
        }
        return v;
    };
    while (std::getline(in, line)) {
        if (head.empty()) {
            head = split(line);
            continue;
        }
        const auto f = split(line);
        std::map<std::string, std::string> r;
        for (size_t k = 0; k < head.size() && k < f.size(); ++k)
            r[head[k]] = f[k];
        rows.push_back(r);
    }
    return rows;
}

}  // namespace

TEST_CASE("yantra-gen IQ files decode with osmo-tetra as the ground truth says")
{
    const fs::path dir = fs::current_path() / "yantra_gen_osmo_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path stem = dir / "two_cells";
    const std::string cmd = std::string("\"") + YANTRA_GEN_EXE + "\" --quiet" +
                            " --cell id=A,carrier=3600,la=1234,cc=21,number=1,level=-70,neighbours=B" +
                            " --cell id=B,carrier=3606,la=1235,cc=22,number=2,level=-82,ppm=0.2,neighbours=A" +
                            " --mcc 901 --mnc 77 --ppm 0.3 --noise-figure 9 --seed 11 --rate 500k --duration 2.5" +
                            " --format cs16 --out \"" + (stem.string() + ".cs16") + "\"";
    REQUIRE(std::system(cmd.c_str()) == 0);

    const auto meta = nlohmann::json::parse(slurp(stem.string() + ".sigmf-meta"));
    REQUIRE(meta["global"]["core:datatype"] == "ci16_le");
    REQUIRE(meta["global"]["yantra:simulated"] == true);
    const double fs = meta["global"]["core:sample_rate"].get<double>();
    const std::string bytes = slurp(stem.string() + ".cs16");
    std::vector<cf> iq(bytes.size() / 4);
    const auto* p = reinterpret_cast<const uint8_t*>(bytes.data());
    for (size_t i = 0; i < iq.size(); ++i) {
        const auto re = static_cast<int16_t>(p[4 * i] | (p[4 * i + 1] << 8));
        const auto im = static_cast<int16_t>(p[4 * i + 2] | (p[4 * i + 3] << 8));
        iq[i] = cf(static_cast<float>(re) / 32767.0f, static_cast<float>(im) / 32767.0f);
    }
    REQUIRE(iq.size() == 1250000);

    const auto truth = read_csv(stem.string() + ".truth.csv");
    const auto cells = read_csv(stem.string() + ".cells.csv");
    REQUIRE(cells.size() == 2);
    for (const auto& cell : cells) {
        const std::string id = cell.at("cell");
        CAPTURE(id);
        std::map<std::string, std::string> t0;
        for (const auto& r : truth)
            if (r.at("cell") == id) {
                t0 = r;
                break;
            }
        REQUIRE(!t0.empty());
        const double offset = std::stod(t0.at("offset_hz")) + std::stod(t0.at("cfo_hz"));
        const unsigned cc = static_cast<unsigned>(std::stoul(t0.at("colour_code")));
        const unsigned mcc = static_cast<unsigned>(std::stoul(t0.at("mcc")));
        const unsigned mnc = static_cast<unsigned>(std::stoul(t0.at("mnc")));
        const unsigned la = static_cast<unsigned>(std::stoul(t0.at("la")));
        const unsigned carrier = static_cast<unsigned>(std::stoul(cell.at("main_carrier")));

        // Cells without a time offset start at t = 0: symbol k centred at
        // (k + kHalfSpanSymbols) / 18000 s.
        const double delay = Pi4DqpskModulator::kHalfSpanSymbols;
        const size_t symbols = static_cast<size_t>((static_cast<double>(iq.size()) / fs) * kSymbolRate - delay - 9.0);
        const auto z = ref::matched_symbols(iq, fs, delay, 0, symbols, offset);
        Bits bits = ref::differential_decode(z, {1.0, 0.0});

        captured_blocks().clear();
        tetra_rx_state trs{};
        for (size_t pos = 0; pos < bits.size(); pos += 200)
            tetra_burst_sync_in(&trs, bits.data() + pos,
                                static_cast<unsigned>(std::min<size_t>(200, bits.size() - pos)));

        const uint32_t scrambling = scrambling_init(static_cast<uint16_t>(mcc), static_cast<uint16_t>(mnc),
                                                    static_cast<uint8_t>(cc));
        int sb1 = 0, sb2 = 0, schf = 0, nwrk = 0;
        for (const auto& b : captured_blocks()) {
            bool ok = false;
            if (b.type == TPSAP_T_SB1) {
                const Bits t1 = osmo_decode(b.bits, kBsch, kScramblingInitBsch, ok);
                REQUIRE(ok);
                CHECK(get_bits(t1, 4, 6) == cc);
                CHECK(get_bits(t1, 12, 5) == 18);
                CHECK(get_bits(t1, 31, 10) == mcc);
                CHECK(get_bits(t1, 41, 14) == mnc);
                ++sb1;
            } else if (b.type == TPSAP_T_SB2) {
                const Bits t1 = osmo_decode(b.bits, kSchHd, scrambling, ok);
                REQUIRE(ok);
                tetra_si_decoded si{};
                macpdu_decode_sysinfo(&si, t1.data());
                CHECK(si.main_carrier == carrier);
                CHECK(si.mle_si.la == la);
                ++sb2;
            } else if (b.type == TPSAP_T_SCH_F) {
                const Bits t1 = osmo_decode(b.bits, kSchF, scrambling, ok);
                REQUIRE(ok);
                tetra_resrc_decoded r{};
                macpdu_decode_resource(&r, t1.data(), 0);
                if (r.addr.type == ADDR_TYPE_SSI)
                    ++nwrk;  // D-NWRK-BROADCAST (broadcast SSI)
                ++schf;
            }
        }
        INFO("SB1 " << sb1 << ", SB2 " << sb2 << ", SCH/F " << schf << ", D-NWRK-BROADCAST " << nwrk);
        // 2.5 s = 44 frames: two frame-18 sync frames of 4 sync bursts
        // each; the synchroniser locks on the first burst and delivers
        // every burst after it.
        CHECK(sb1 >= 7);
        CHECK(sb2 == sb1);
        CHECK(schf > 0);
        CHECK(nwrk > 0);
    }
    fs::remove_all(dir);
}
