// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-256 of the project's source files (IND-03) against the FIPS 180-4
// example vectors. Copied from yantra-v2 68ea20a:
// tests/project/test_sha256.cpp, unchanged (moved to tests/util/).

#include "yantra/core/util/sha256.h"

#include <doctest/doctest.h>

#include <string>

using yantra::util::Sha256;

TEST_CASE("sha256: FIPS 180-4 examples") {
    CHECK(Sha256::of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256::of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 h;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) h.update(chunk);
    CHECK(h.finish_hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: pieces of any size give the same digest") {
    std::string data;
    for (int i = 0; i < 1000; ++i) data.push_back(static_cast<char>(i * 7 + 3));
    const std::string whole = Sha256::of(data);
    for (std::size_t piece : {1u, 3u, 63u, 64u, 65u, 127u, 999u}) {
        Sha256 h;
        for (std::size_t at = 0; at < data.size(); at += piece) h.update(data.substr(at, piece));
        CHECK(h.finish_hex() == whole);
    }
    // finish() resets.
    Sha256 h;
    h.update("x");
    (void)h.finish();
    CHECK(h.finish_hex() == Sha256::of(""));
}
