// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA V+D downlink channel coding (EN 300 392-2 clause 8) for the
// test-signal generator (RAD-70). Clause numbers refer to EN 300 392-2.

#include "yantra/gen/coding.hpp"

#include <array>
#include <bit>
#include <stdexcept>
#include <string>

namespace yantra::gen {

namespace {

void require_size(const Bits& b, int expected, std::string_view what)
{
    if (static_cast<int>(b.size()) != expected) {
        std::string msg(what);
        msg += ": expected ";
        msg += std::to_string(expected);
        msg += " bits, got ";
        msg += std::to_string(b.size());
        throw std::invalid_argument(msg);
    }
}

}  // namespace

uint16_t crc16_register(const uint8_t* bits, size_t n, uint16_t init)
{
    uint16_t crc = init;
    for (size_t i = 0; i < n; ++i) {
        const bool feedback = (((crc >> 15) & 1u) ^ (bits[i] & 1u)) != 0;
        crc = static_cast<uint16_t>(crc << 1);
        if (feedback)
            crc ^= 0x1021;
    }
    return crc;
}

// 8.2.3.3: the CRC bits are the ones complement of the register.
Bits add_crc_and_tail(const Bits& type1)
{
    Bits out = type1;
    const auto crc = static_cast<uint16_t>(~crc16_register(type1.data(), type1.size()));
    for (int i = 15; i >= 0; --i)
        out.push_back(static_cast<uint8_t>((crc >> i) & 1u));
    out.insert(out.end(), kTailBits, 0);
    return out;
}

// 8.2.3.1.1: 16-state rate-1/4 mother code.
Bits conv_encode_mother(const Bits& type2)
{
    // Tap masks over (b(k), b(k-1), b(k-2), b(k-3), b(k-4)) as bits 0..4.
    static constexpr std::array<unsigned, 4> kTaps{
        0b10011u,  // G1 = 1 + D + D^4
        0b11101u,  // G2 = 1 + D^2 + D^3 + D^4
        0b10111u,  // G3 = 1 + D + D^2 + D^4
        0b11011u,  // G4 = 1 + D + D^3 + D^4
    };
    Bits out;
    out.reserve(type2.size() * 4);
    unsigned reg = 0;  // bit j = b(k - j)
    for (uint8_t b : type2) {
        reg = ((reg << 1) | (b & 1u)) & 0x1Fu;
        for (unsigned taps : kTaps)
            out.push_back(static_cast<uint8_t>(std::popcount(reg & taps) & 1));
    }
    return out;
}

// 8.2.3.1.3: rate 2/3, P(1) = 1, P(2) = 2, P(3) = 5, t = 3, period 8;
// b3(j) = V(k), k = 8 * ((j - 1) div t) + P(j - t * ((j - 1) div t)).
Bits puncture_2_3(const Bits& mother)
{
    if (mother.size() % 8 != 0)
        throw std::invalid_argument("puncture_2_3: mother code size not a multiple of 8");
    static constexpr std::array<size_t, 3> kP{1, 2, 5};
    const size_t out_len = mother.size() / 8 * 3;
    Bits out(out_len);
    for (size_t j = 1; j <= out_len; ++j) {
        const size_t q = (j - 1) / 3;
        out[j - 1] = mother[8 * q + kP[j - 1 - 3 * q] - 1];
    }
    return out;
}

// 8.2.4.1: (K, a) block interleaver.
Bits block_interleave(const Bits& type3, int a)
{
    const size_t k_size = type3.size();
    if (k_size == 0 || a <= 0)
        throw std::invalid_argument("block_interleave: empty block or a <= 0");
    Bits out(k_size);
    for (size_t i = 1; i <= k_size; ++i)
        out[(static_cast<size_t>(a) * i) % k_size] = type3[i - 1];
    return out;
}

// 8.2.5.2: extended colour code e(1..30) = MCC, MNC, colour code, then the
// register is completed with two 1s.
uint32_t scrambling_init(uint16_t mcc, uint16_t mnc, uint8_t colour_code)
{
    if (mcc > 0x3FF || mnc > 0x3FFF || colour_code > 0x3F)
        throw std::out_of_range("scrambling_init: MCC 10, MNC 14, colour code 6 bits");
    const uint32_t e = colour_code | (uint32_t{mnc} << 6) | (uint32_t{mcc} << 20);
    return (e << 2) | 3u;
}

// 8.2.5.2: p(k) = sum of c(j) p(k - j) for the connection polynomial
// 1 + x + x^2 + x^4 + x^5 + x^7 + x^8 + x^10 + x^11 + x^12 + x^16 + x^22 +
// x^23 + x^26 + x^32, as a 32-bit Fibonacci register.
Bits scrambling_sequence(uint32_t init, size_t n)
{
    static constexpr std::array<int, 14> kTaps{32, 26, 23, 22, 16, 12, 11, 10, 8, 7, 5, 4, 2, 1};
    Bits out(n);
    uint32_t reg = init;
    for (size_t i = 0; i < n; ++i) {
        uint32_t bit = 0;
        for (int t : kTaps)
            bit ^= reg >> (32 - t);
        bit &= 1u;
        reg = (reg >> 1) | (bit << 31);
        out[i] = static_cast<uint8_t>(bit);
    }
    return out;
}

void scramble(Bits& bits, uint32_t init)
{
    const Bits seq = scrambling_sequence(init, bits.size());
    for (size_t i = 0; i < bits.size(); ++i)
        bits[i] = static_cast<uint8_t>((bits[i] ^ seq[i]) & 1u);
}

// 8.2.3.2: generator matrix [I14 | P]; the rows of the parity part P.
uint32_t rm3014_encode(uint16_t in14)
{
    static constexpr std::array<uint16_t, 14> kParity{
        0b1001101101100000, 0b0010110111100000, 0b1111110000100000, 0b1110000000111100,
        0b1001100000111010, 0b0101010000110110, 0b0010110000101110, 0b1111111111011111,
        0b1000001100111001, 0b0100001010110101, 0b0010000110101101, 0b0001001001110011,
        0b0000100101101011, 0b0000010011100111,
    };
    if (in14 > 0x3FFF)
        throw std::out_of_range("rm3014_encode: input has more than 14 bits");
    uint32_t cw = 0;
    for (int i = 0; i < 14; ++i) {
        if ((in14 >> (13 - i)) & 1u)
            cw ^= (1u << (29 - i)) | kParity[static_cast<size_t>(i)];
    }
    return cw;
}

Bits encode_block(const Bits& type1, const BlockSpec& spec, uint32_t scrambling)
{
    require_size(type1, spec.type1, spec.name);
    Bits b = puncture_2_3(conv_encode_mother(add_crc_and_tail(type1)));
    require_size(b, spec.type5, spec.name);
    b = block_interleave(b, spec.a);
    scramble(b, scrambling);
    return b;
}

Bits encode_bsch(const Bits& sync60)
{
    return encode_block(sync60, kBsch, kScramblingInitBsch);
}

Bits encode_sch_hd(const Bits& block124, uint32_t scrambling)
{
    return encode_block(block124, kSchHd, scrambling);
}

Bits encode_sch_f(const Bits& block268, uint32_t scrambling)
{
    return encode_block(block268, kSchF, scrambling);
}

Bits encode_aach(uint16_t access_assign14, uint32_t scrambling)
{
    Bits b = to_bits(rm3014_encode(access_assign14), kAachBits);
    scramble(b, scrambling);
    return b;
}

}  // namespace yantra::gen
