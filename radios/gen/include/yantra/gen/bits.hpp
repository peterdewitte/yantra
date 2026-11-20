// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bit vectors of the TETRA test-signal generator (RAD-70): one bit per
// byte (0 or 1), first transmitted bit first, as EN 300 392-2 numbers the
// bits b(1), b(2), ... of a block. Fields are written MSB first.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::gen {

/// One bit per element (0 or 1), first transmitted bit first.
using Bits = std::vector<uint8_t>;

/// Appends fields MSB first. put() refuses values that do not fit the
/// field width (std::out_of_range naming the field), so a wrong value in a
/// PDU struct is an error instead of a silently truncated field.
class BitWriter {
public:
    /// Appends `value` as a `width`-bit field (0 <= width <= 64).
    void put(uint64_t value, int width, std::string_view field = {});
    /// Appends one bit.
    void bit(bool b) { bits_.push_back(b ? 1 : 0); }
    /// Appends a bit vector as it is.
    void append(const Bits& b) { bits_.insert(bits_.end(), b.begin(), b.end()); }
    size_t size() const { return bits_.size(); }
    const Bits& bits() const { return bits_; }
    Bits take() { return std::move(bits_); }

private:
    Bits bits_;
};

/// Reads `width` bits (<= 64) at `pos` MSB first; std::out_of_range when
/// the vector is too short.
uint64_t get_bits(const Bits& b, size_t pos, int width);

/// Bits as a '0'/'1' string (golden files, diagnostics).
std::string to_string(const Bits& b);
/// Parses a '0'/'1' string; std::invalid_argument on any other character.
Bits from_string(std::string_view s);

/// The low `width` bits of `value`, MSB first.
Bits to_bits(uint64_t value, int width);

}  // namespace yantra::gen
