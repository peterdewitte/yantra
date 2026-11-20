// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test-only reference decoder for the TETRA test-signal generator (RAD-70).

#include "ref_decoder.hpp"

#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace yantra::gen::ref {

namespace {

// Mother code outputs for a 5-bit window w = b(k) b(k-1) b(k-2) b(k-3)
// b(k-4) (b(k) as the most significant bit), written as the polynomials
// coefficient lists of EN 300 392-2 8.2.3.1.1 (index = power of D).
constexpr std::array<std::array<int, 5>, 4> kPolys{{
    {1, 1, 0, 0, 1},  // G1
    {1, 0, 1, 1, 1},  // G2
    {1, 1, 1, 0, 1},  // G3
    {1, 1, 0, 1, 1},  // G4
}};

int mother_output(unsigned window, int g)
{
    int v = 0;
    for (int p = 0; p < 5; ++p)
        v ^= kPolys[static_cast<size_t>(g)][static_cast<size_t>(p)] & static_cast<int>((window >> (4 - p)) & 1u);
    return v;
}

// RM(30,14) generator rows [I14 | P] of 8.2.3.2, one string per row.
constexpr std::array<std::string_view, 14> kRmRows{
    "10000000000000" "1001101101100000",
    "01000000000000" "0010110111100000",
    "00100000000000" "1111110000100000",
    "00010000000000" "1110000000111100",
    "00001000000000" "1001100000111010",
    "00000100000000" "0101010000110110",
    "00000010000000" "0010110000101110",
    "00000001000000" "1111111111011111",
    "00000000100000" "1000001100111001",
    "00000000010000" "0100001010110101",
    "00000000001000" "0010000110101101",
    "00000000000100" "0001001001110011",
    "00000000000010" "0000100101101011",
    "00000000000001" "0000010011100111",
};

uint32_t row_value(std::string_view row)
{
    uint32_t v = 0;
    for (char c : row)
        v = (v << 1) | (c == '1' ? 1u : 0u);
    return v;
}

}  // namespace

Bits scrambling_sequence(uint32_t init, size_t n)
{
    // p(k) = p(k-1) + p(k-2) + p(k-4) + p(k-5) + p(k-7) + p(k-8) + p(k-10) +
    //        p(k-11) + p(k-12) + p(k-16) + p(k-22) + p(k-23) + p(k-26) +
    //        p(k-32) (mod 2); the init word holds p(-32) in bit 0 up to
    //        p(-1) in bit 31.
    static constexpr std::array<size_t, 14> kDelays{1, 2, 4, 5, 7, 8, 10, 11, 12, 16, 22, 23, 26, 32};
    std::vector<uint8_t> p(32 + n);
    for (size_t i = 0; i < 32; ++i)
        p[i] = static_cast<uint8_t>((init >> i) & 1u);
    for (size_t k = 32; k < p.size(); ++k) {
        uint8_t v = 0;
        for (size_t d : kDelays)
            v ^= p[k - d];
        p[k] = v;
    }
    return Bits(p.begin() + 32, p.end());
}

Bits deinterleave(const Bits& type4, int a)
{
    const size_t k_size = type4.size();
    Bits out(k_size);
    for (size_t i = 1; i <= k_size; ++i)
        out[i - 1] = type4[(static_cast<size_t>(a) * i) % k_size];
    return out;
}

std::vector<int> depuncture_2_3(const Bits& type3)
{
    if (type3.size() % 3 != 0)
        throw std::invalid_argument("depuncture_2_3: size not a multiple of 3");
    static constexpr std::array<size_t, 3> kPositions{0, 1, 4};  // P - 1
    std::vector<int> soft(type3.size() / 3 * 8, 0);
    for (size_t j = 0; j < type3.size(); ++j)
        soft[8 * (j / 3) + kPositions[j % 3]] = type3[j] ? -1 : 1;
    return soft;
}

Bits viterbi_decode(const std::vector<int>& soft)
{
    if (soft.size() % 4 != 0)
        throw std::invalid_argument("viterbi_decode: 4 soft values per bit");
    const size_t steps = soft.size() / 4;
    constexpr int kStates = 16;  // state = b(k-1) b(k-2) b(k-3) b(k-4), b(k-1) MSB
    constexpr long kInf = std::numeric_limits<long>::max() / 4;

    std::array<long, kStates> metric{};
    metric.fill(kInf);
    metric[0] = 0;
    std::vector<std::array<uint8_t, kStates>> from(steps);  // predecessor state

    for (size_t k = 0; k < steps; ++k) {
        std::array<long, kStates> next{};
        next.fill(kInf);
        for (unsigned s = 0; s < kStates; ++s) {
            if (metric[s] >= kInf)
                continue;
            for (unsigned b = 0; b < 2; ++b) {
                const unsigned window = (b << 4) | s;
                long m = metric[s];
                for (int g = 0; g < 4; ++g) {
                    const int expect = mother_output(window, g) ? -1 : 1;
                    m -= static_cast<long>(expect) * soft[4 * k + static_cast<size_t>(g)];
                }
                const unsigned ns = window >> 1;
                if (m < next[ns]) {
                    next[ns] = m;
                    from[k][ns] = static_cast<uint8_t>(s);
                }
            }
        }
        metric = next;
    }

    Bits out(steps);
    unsigned s = 0;  // terminated by the tail bits
    for (size_t k = steps; k-- > 0;) {
        out[k] = static_cast<uint8_t>((s >> 3) & 1u);
        s = from[k][s];
    }
    return out;
}

bool crc_ok(const Bits& type1_and_crc)
{
    return crc16_register(type1_and_crc.data(), type1_and_crc.size()) == kCrcResidual;
}

DecodedBlock decode_block(const Bits& type5, const BlockSpec& spec, uint32_t scrambling)
{
    if (static_cast<int>(type5.size()) != spec.type5)
        throw std::invalid_argument("decode_block: wrong type-5 size");
    Bits b = type5;
    const Bits seq = scrambling_sequence(scrambling, b.size());
    for (size_t i = 0; i < b.size(); ++i)
        b[i] ^= seq[i];
    const Bits type3 = deinterleave(b, spec.a);
    Bits type2 = viterbi_decode(depuncture_2_3(type3));
    type2.resize(static_cast<size_t>(spec.type1 + kCrcBits));

    DecodedBlock d;
    d.crc_ok = crc_ok(type2);
    d.type1.assign(type2.begin(), type2.begin() + spec.type1);
    const Bits again = encode_block(d.type1, spec, scrambling);
    for (size_t i = 0; i < again.size(); ++i)
        d.corrected += again[i] != type5[i] ? 1 : 0;
    return d;
}

uint16_t rm3014_decode(uint32_t received30, int* distance)
{
    static const std::array<uint32_t, 14> rows = [] {
        std::array<uint32_t, 14> r{};
        for (size_t i = 0; i < r.size(); ++i)
            r[i] = row_value(kRmRows[i]);
        return r;
    }();
    int best_d = 31;
    uint16_t best = 0;
    for (uint32_t m = 0; m < (1u << 14); ++m) {
        uint32_t cw = 0;
        for (int i = 0; i < 14; ++i)
            if ((m >> (13 - i)) & 1u)
                cw ^= rows[static_cast<size_t>(i)];
        const int d = std::popcount(cw ^ received30);
        if (d < best_d) {
            best_d = d;
            best = static_cast<uint16_t>(m);
        }
    }
    if (distance)
        *distance = best_d;
    return best;
}

uint16_t decode_aach(const Bits& type5, uint32_t scrambling, int* distance)
{
    if (type5.size() != static_cast<size_t>(kAachBits))
        throw std::invalid_argument("decode_aach: 30 bits expected");
    Bits b = type5;
    const Bits seq = scrambling_sequence(scrambling, b.size());
    for (size_t i = 0; i < b.size(); ++i)
        b[i] ^= seq[i];
    return rm3014_decode(static_cast<uint32_t>(get_bits(b, 0, kAachBits)), distance);
}

}  // namespace yantra::gen::ref
