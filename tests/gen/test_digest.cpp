// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-256 of the golden digests (RAD-93): FIPS 180-4 / NIST known answers.

#include "yantra/gen/digest.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace yantra::gen;

TEST_CASE("SHA-256 known answers")
{
    CHECK(sha256_hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(sha256_hex(two_blocks.data(), two_blocks.size()) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // One million 'a', fed in uneven pieces.
    Sha256 h;
    const std::string chunk(997, 'a');
    size_t left = 1000000;
    while (left > 0) {
        const size_t n = left < chunk.size() ? left : chunk.size();
        h.update(chunk.data(), n);
        left -= n;
    }
    CHECK(h.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 of bit vectors hashes one byte per bit")
{
    const Bits b{1, 0, 1};
    const unsigned char raw[3] = {1, 0, 1};
    Sha256 h;
    h.update(b);
    CHECK(h.hex() == sha256_hex(raw, 3));
}
