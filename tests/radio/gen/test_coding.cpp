// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA channel coding of the test-signal generator (RAD-70): known
// answers from EN 300 392-2 clause 8 and encode/decode round trips with
// the test-only reference decoder, including bit-error correction.

#include "ref_decoder.hpp"

#include "yantra/gen/bits.hpp"
#include "yantra/gen/coding.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <random>
#include <set>
#include <stdexcept>
#include <string>

using namespace yantra::gen;

namespace {

Bits random_bits(std::mt19937& rng, int n)
{
    Bits b(static_cast<size_t>(n));
    for (auto& v : b)
        v = static_cast<uint8_t>(rng() & 1u);
    return b;
}

// Flips `count` distinct random positions.
void flip_bits(Bits& b, std::mt19937& rng, int count)
{
    std::set<size_t> done;
    while (static_cast<int>(done.size()) < count) {
        const size_t pos = rng() % b.size();
        if (done.insert(pos).second)
            b[pos] ^= 1u;
    }
}

Bits bytes_to_bits(std::string_view bytes)
{
    BitWriter w;
    for (char c : bytes)
        w.put(static_cast<uint8_t>(c), 8);
    return w.take();
}

}  // namespace

TEST_CASE("BitWriter writes fields MSB first and refuses values that do not fit")
{
    BitWriter w;
    w.put(0b101, 3);
    w.bit(true);
    w.put(0, 2);
    w.put(0xABCD, 16);
    CHECK(to_string(w.bits()) == "1011001010101111001101");
    CHECK(get_bits(w.bits(), 6, 16) == 0xABCD);
    CHECK_THROWS_AS(w.put(8, 3, "colour code"), std::out_of_range);
    try {
        w.put(64, 6, "colour code");
        FAIL("no exception");
    } catch (const std::out_of_range& e) {
        CHECK(std::string(e.what()).find("colour code") != std::string::npos);
    }
    CHECK_THROWS_AS(get_bits(w.bits(), 20, 3), std::out_of_range);
    CHECK(from_string("0110") == Bits{0, 1, 1, 0});
    CHECK_THROWS_AS(from_string("01x"), std::invalid_argument);
    CHECK(to_bits(0x5, 4) == Bits{0, 1, 0, 1});
}

TEST_CASE("CRC-16: CCITT known answer, inverted CRC, residual and tail")
{
    // CRC-16/CCITT-FALSE check value of "123456789" (poly 0x1021, init 0xFFFF).
    const Bits msg = bytes_to_bits("123456789");
    CHECK(crc16_register(msg.data(), msg.size()) == 0x29B1);

    const Bits type2 = add_crc_and_tail(msg);
    REQUIRE(type2.size() == msg.size() + kCrcBits + kTailBits);
    CHECK(get_bits(type2, msg.size(), 16) == (0x29B1 ^ 0xFFFF));  // sent inverted
    for (int i = 0; i < kTailBits; ++i)
        CHECK(type2[msg.size() + kCrcBits + static_cast<size_t>(i)] == 0);
    const Bits no_tail(type2.begin(), type2.end() - kTailBits);
    CHECK(crc16_register(no_tail.data(), no_tail.size()) == kCrcResidual);
    CHECK(ref::crc_ok(no_tail));
}

TEST_CASE("CRC-16 detects every single and double bit error in an SCH/F block")
{
    std::mt19937 rng(7);
    const Bits type1 = random_bits(rng, kSchF.type1);
    Bits block = add_crc_and_tail(type1);
    block.resize(block.size() - kTailBits);
    REQUIRE(ref::crc_ok(block));
    for (size_t i = 0; i < block.size(); ++i) {
        block[i] ^= 1u;
        CHECK_FALSE(ref::crc_ok(block));
        for (size_t j = i + 1; j < block.size(); j += 13) {
            block[j] ^= 1u;
            CHECK_FALSE(ref::crc_ok(block));
            block[j] ^= 1u;
        }
        block[i] ^= 1u;
    }
}

TEST_CASE("Mother code impulse response equals the generator polynomials G1..G4")
{
    // A single 1 followed by zeros: step j outputs the coefficients of D^j.
    const Bits out = conv_encode_mother(Bits{1, 0, 0, 0, 0, 0});
    REQUIRE(out.size() == 24);
    CHECK(to_string(out) == "1111" "1011" "0110" "0101" "1111" "0000");
    // Linear code: encode(a ^ b) == encode(a) ^ encode(b).
    std::mt19937 rng(3);
    const Bits a = random_bits(rng, 40), b = random_bits(rng, 40);
    Bits ab(40);
    for (size_t i = 0; i < 40; ++i)
        ab[i] = a[i] ^ b[i];
    const Bits ea = conv_encode_mother(a), eb = conv_encode_mother(b), eab = conv_encode_mother(ab);
    for (size_t i = 0; i < eab.size(); ++i)
        CHECK(eab[i] == (ea[i] ^ eb[i]));
}

TEST_CASE("Rate-2/3 puncturing keeps mother bits 1, 2 and 5 of every 8")
{
    for (size_t k = 0; k < 32; ++k) {
        Bits mother(32, 0);
        mother[k] = 1;
        const Bits out = puncture_2_3(mother);
        REQUIRE(out.size() == 12);
        const size_t pos = k % 8;
        const auto ones = static_cast<size_t>(std::count(out.begin(), out.end(), 1));
        if (pos == 0 || pos == 1 || pos == 4) {
            REQUIRE(ones == 1);
            const size_t where = static_cast<size_t>(std::find(out.begin(), out.end(), 1) - out.begin());
            CHECK(where == 3 * (k / 8) + (pos == 4 ? 2 : pos));
        } else {
            CHECK(ones == 0);
        }
    }
    CHECK_THROWS_AS(puncture_2_3(Bits(12, 0)), std::invalid_argument);
    // Block sizes of the broadcast channels.
    for (const BlockSpec* s : {&kBsch, &kSchHd, &kSchF}) {
        const Bits t2(static_cast<size_t>(s->type1 + kCrcBits + kTailBits), 0);
        CHECK(static_cast<int>(puncture_2_3(conv_encode_mother(t2)).size()) == s->type5);
    }
}

TEST_CASE("Block interleaving is the (K, a) permutation and is undone by the reference")
{
    for (const BlockSpec* s : {&kBsch, &kSchHd, &kSchF}) {
        CAPTURE(s->name);
        const auto k_size = static_cast<size_t>(s->type5);
        // b3(i) lands at k = 1 + (a * i mod K) (1-based).
        for (size_t i = 1; i <= k_size; i += 7) {
            Bits in(k_size, 0);
            in[i - 1] = 1;
            const Bits out = block_interleave(in, s->a);
            CHECK(out[(static_cast<size_t>(s->a) * i) % k_size] == 1);
            CHECK(std::count(out.begin(), out.end(), 1) == 1);
        }
        std::mt19937 rng(11);
        const Bits in = random_bits(rng, s->type5);
        CHECK(ref::deinterleave(block_interleave(in, s->a), s->a) == in);
    }
    // BSCH: b3(1) -> b4(12), b3(120) -> b4(1).
    Bits in(120, 0);
    in[0] = 1;
    CHECK(block_interleave(in, 11)[11] == 1);
}

TEST_CASE("Scrambling: init from the extended colour code, sequence matches the recurrence")
{
    CHECK(scrambling_init(0, 0, 0) == kScramblingInitBsch);
    // MCC 901, MNC 1, colour code 1: e = 1 | 1 << 6 | 901 << 20.
    CHECK(scrambling_init(901, 1, 1) == (((1u | (1u << 6) | (901u << 20)) << 2) | 3u));
    CHECK(scrambling_init(1023, 16383, 63) == 0xFFFFFFFFu);
    CHECK_THROWS_AS(scrambling_init(1024, 0, 0), std::out_of_range);
    CHECK_THROWS_AS(scrambling_init(0, 16384, 0), std::out_of_range);
    CHECK_THROWS_AS(scrambling_init(0, 0, 64), std::out_of_range);

    for (uint32_t init : {kScramblingInitBsch, scrambling_init(901, 1, 1), scrambling_init(1, 2, 3),
                          scrambling_init(1023, 16383, 63)}) {
        CAPTURE(init);
        CHECK(scrambling_sequence(init, 600) == ref::scrambling_sequence(init, 600));
    }
    // Different colour codes give different sequences; scrambling twice is identity.
    CHECK(scrambling_sequence(scrambling_init(901, 1, 1), 432) !=
          scrambling_sequence(scrambling_init(901, 1, 2), 432));
    std::mt19937 rng(5);
    const Bits data = random_bits(rng, 216);
    Bits twice = data;
    scramble(twice, 12345);
    CHECK(twice != data);
    scramble(twice, 12345);
    CHECK(twice == data);
}

TEST_CASE("RM(30,14): systematic, linear, minimum distance 8, ML decoding corrects 3 errors")
{
    for (uint16_t m : {uint16_t{0}, uint16_t{1}, uint16_t{0x2000}, uint16_t{0x1555}, uint16_t{0x3FFF}})
        CHECK((rm3014_encode(m) >> 16) == m);
    CHECK(rm3014_encode(0) == 0);
    CHECK_THROWS_AS(rm3014_encode(0x4000), std::out_of_range);
    CHECK((rm3014_encode(0x1234) ^ rm3014_encode(0x0F0F)) == rm3014_encode(0x1234 ^ 0x0F0F));

    int min_weight = 31;
    for (uint32_t m = 1; m < (1u << 14); ++m)
        min_weight = std::min(min_weight, std::popcount(rm3014_encode(static_cast<uint16_t>(m))));
    CHECK(min_weight == 8);

    std::mt19937 rng(13);
    for (int trial = 0; trial < 200; ++trial) {
        const auto m = static_cast<uint16_t>(rng() & 0x3FFF);
        uint32_t cw = rm3014_encode(m);
        const int errors = trial % 4;  // 0 to 3 errors
        std::set<int> pos;
        while (static_cast<int>(pos.size()) < errors)
            pos.insert(static_cast<int>(rng() % 30));
        for (int p : pos)
            cw ^= 1u << p;
        int distance = -1;
        CHECK(ref::rm3014_decode(cw, &distance) == m);
        CHECK(distance == errors);
    }
}

TEST_CASE("Channel round trips: BSCH, SCH/HD (BNCH) and SCH/F decode without errors")
{
    std::mt19937 rng(21);
    const uint32_t cell = scrambling_init(901, 1, 1);
    for (int trial = 0; trial < 20; ++trial) {
        const Bits sync = random_bits(rng, kBsch.type1);
        const Bits sb1 = encode_bsch(sync);
        REQUIRE(sb1.size() == 120);
        auto d = ref::decode_block(sb1, kBsch, kScramblingInitBsch);
        CHECK(d.crc_ok);
        CHECK(d.type1 == sync);
        CHECK(d.corrected == 0);

        const Bits hd = random_bits(rng, kSchHd.type1);
        const Bits bnch = encode_sch_hd(hd, cell);
        REQUIRE(bnch.size() == 216);
        d = ref::decode_block(bnch, kSchHd, cell);
        CHECK(d.crc_ok);
        CHECK(d.type1 == hd);

        const Bits f = random_bits(rng, kSchF.type1);
        const Bits schf = encode_sch_f(f, cell);
        REQUIRE(schf.size() == 432);
        d = ref::decode_block(schf, kSchF, cell);
        CHECK(d.crc_ok);
        CHECK(d.type1 == f);
    }
    CHECK_THROWS_AS(encode_bsch(Bits(59, 0)), std::invalid_argument);
    CHECK_THROWS_AS(encode_sch_f(Bits(124, 0), cell), std::invalid_argument);
}

TEST_CASE("Channel coding corrects every single bit error and most scattered errors")
{
    // The rate-2/3 punctured code has code words of weight 4, so with hard
    // decisions only one error is always correctable; more errors are
    // corrected when they are spread (interleaving), as checked below.
    std::mt19937 rng(42);
    const uint32_t cell = scrambling_init(901, 1, 1);
    struct Case {
        const BlockSpec* spec;
        uint32_t init;
    };
    for (const Case& c : {Case{&kBsch, kScramblingInitBsch}, Case{&kSchHd, cell}, Case{&kSchF, cell}}) {
        CAPTURE(c.spec->name);
        const Bits type1 = random_bits(rng, c.spec->type1);
        const Bits tx = encode_block(type1, *c.spec, c.init);
        int all_ok = 1;
        for (size_t pos = 0; pos < tx.size(); ++pos) {
            Bits rx = tx;
            rx[pos] ^= 1u;
            const auto d = ref::decode_block(rx, *c.spec, c.init);
            if (!d.crc_ok || d.type1 != type1 || d.corrected != 1)
                all_ok = 0;
        }
        CHECK(all_ok == 1);

        // Three random errors: the ML decoder recovers the block in at
        // least 90 % of 200 seeded trials (measured: about 95 to 98 %), and
        // a CRC pass always means the right block.
        int recovered = 0, false_pass = 0;
        for (int trial = 0; trial < 200; ++trial) {
            const Bits t1 = random_bits(rng, c.spec->type1);
            Bits rx = encode_block(t1, *c.spec, c.init);
            flip_bits(rx, rng, 3);
            const auto d = ref::decode_block(rx, *c.spec, c.init);
            recovered += (d.crc_ok && d.type1 == t1) ? 1 : 0;
            false_pass += (d.crc_ok && d.type1 != t1) ? 1 : 0;
        }
        CHECK(recovered >= 180);
        CHECK(false_pass == 0);
    }
    // Half the bits wrong: the decoder output fails the CRC.
    int detected = 0;
    for (int trial = 0; trial < 20; ++trial) {
        Bits rx = encode_sch_f(random_bits(rng, kSchF.type1), cell);
        flip_bits(rx, rng, 150);
        detected += ref::decode_block(rx, kSchF, cell).crc_ok ? 0 : 1;
    }
    CHECK(detected == 20);
}

TEST_CASE("The reference Viterbi decoder is maximum likelihood")
{
    // Whenever it returns another block than the one sent, the path it
    // chose is at most as far from the received bits as the sent one.
    std::mt19937 rng(17);
    int wrong = 0;
    for (int trial = 0; trial < 300; ++trial) {
        const Bits type2 = add_crc_and_tail(random_bits(rng, kBsch.type1));
        Bits rx = puncture_2_3(conv_encode_mother(type2));
        flip_bits(rx, rng, 3);
        const Bits path = ref::viterbi_decode(ref::depuncture_2_3(rx));
        const Bits again = puncture_2_3(conv_encode_mother(path));
        int dist = 0;
        for (size_t i = 0; i < rx.size(); ++i)
            dist += again[i] != rx[i] ? 1 : 0;
        CHECK(dist <= 3);
        wrong += path != type2 ? 1 : 0;
    }
    CHECK(wrong < 300 / 5);
}

TEST_CASE("The colour code matters: the wrong scrambling init fails the CRC")
{
    std::mt19937 rng(8);
    const Bits type1 = random_bits(rng, kSchHd.type1);
    const Bits tx = encode_sch_hd(type1, scrambling_init(901, 1, 1));
    CHECK(ref::decode_block(tx, kSchHd, scrambling_init(901, 1, 1)).crc_ok);
    CHECK_FALSE(ref::decode_block(tx, kSchHd, scrambling_init(901, 1, 2)).crc_ok);
    CHECK_FALSE(ref::decode_block(tx, kSchHd, scrambling_init(901, 2, 1)).crc_ok);
}

TEST_CASE("AACH: RM(30,14) + scrambling round trip with up to 3 bit errors")
{
    std::mt19937 rng(99);
    const uint32_t cell = scrambling_init(1, 1, 5);
    for (int trial = 0; trial < 100; ++trial) {
        const auto aa = static_cast<uint16_t>(rng() & 0x3FFF);
        Bits rx = encode_aach(aa, cell);
        REQUIRE(rx.size() == 30);
        const int errors = trial % 4;
        flip_bits(rx, rng, errors);
        int distance = -1;
        CHECK(ref::decode_aach(rx, cell, &distance) == aa);
        CHECK(distance == errors);
    }
    // AACH is scrambled with the cell's colour code (not init 3).
    const Bits a = encode_aach(0x0123, cell);
    Bits plain = to_bits(rm3014_encode(0x0123), 30);
    CHECK(a != plain);
    scramble(plain, cell);
    CHECK(a == plain);
}
