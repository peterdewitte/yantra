// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-256 (FIPS 180-4) for the generator's golden vectors (RAD-93): a
// seeded run is hashed into a digest that is checked in, so any change of
// the generated bits or samples is seen. Own code, no library.

#pragma once

#include "bits.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace yantra::gen {

/// Incremental SHA-256.
class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t n);
    /// Bits as one byte per bit (0 or 1), as stored in Bits.
    void update(const Bits& bits) { update(bits.data(), bits.size()); }
    /// The digest; the object is spent afterwards.
    std::array<uint8_t, 32> finish();
    /// finish() as 64 lower-case hex digits.
    std::string hex();

private:
    void block(const uint8_t* p);
    std::array<uint32_t, 8> h_;
    std::array<uint8_t, 64> buf_{};
    std::size_t used_ = 0;
    uint64_t total_ = 0;
};

/// SHA-256 of `n` bytes as hex.
std::string sha256_hex(const void* data, std::size_t n);

}  // namespace yantra::gen
