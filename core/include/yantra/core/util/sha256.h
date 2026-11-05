// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-256 (FIPS 180-4), own code: content hashes of the source files a
// project keeps (IND-03) and of downloaded data (DEMO-12). Not for
// security decisions. Copied from yantra-v2 68ea20a:
// core/include/yantra/core/util/sha256.h, unchanged.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace yantra::util {

class Sha256 {
public:
    using Digest = std::array<std::uint8_t, 32>;

    Sha256() { reset(); }
    void reset();
    void update(const void* data, std::size_t size);
    void update(std::string_view text) { update(text.data(), text.size()); }
    // The digest of everything given since the last reset; resets.
    Digest finish();
    // finish() as 64 lowercase hex digits.
    std::string finish_hex();

    static std::string hex(const Digest& digest);
    static std::string of(std::string_view data);  // hex digest of data

private:
    void block(const std::uint8_t* p);

    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buf_{};
    std::size_t used_ = 0;
    std::uint64_t bytes_ = 0;
};

}  // namespace yantra::util
