// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bit vector helpers of the TETRA test-signal generator (RAD-70).

#include "yantra/gen/bits.hpp"

#include <stdexcept>

namespace yantra::gen {

void BitWriter::put(uint64_t value, int width, std::string_view field)
{
    if (width < 0 || width > 64)
        throw std::invalid_argument("BitWriter::put: width must be 0..64");
    if (width < 64 && (value >> width) != 0) {
        std::string msg = "value ";
        msg += std::to_string(value);
        msg += " does not fit ";
        msg += std::to_string(width);
        msg += " bits";
        if (!field.empty()) {
            msg += " (field ";
            msg += field;
            msg += ')';
        }
        throw std::out_of_range(msg);
    }
    for (int i = width - 1; i >= 0; --i)
        bits_.push_back(static_cast<uint8_t>((value >> i) & 1u));
}

uint64_t get_bits(const Bits& b, size_t pos, int width)
{
    if (width < 0 || width > 64 || pos + static_cast<size_t>(width) > b.size())
        throw std::out_of_range("get_bits: outside the bit vector");
    uint64_t v = 0;
    for (int i = 0; i < width; ++i)
        v = (v << 1) | (b[pos + static_cast<size_t>(i)] & 1u);
    return v;
}

std::string to_string(const Bits& b)
{
    std::string s;
    s.reserve(b.size());
    for (uint8_t v : b)
        s.push_back(v ? '1' : '0');
    return s;
}

Bits from_string(std::string_view s)
{
    Bits b;
    b.reserve(s.size());
    for (char c : s) {
        if (c != '0' && c != '1')
            throw std::invalid_argument("from_string: only '0' and '1' allowed");
        b.push_back(c == '1' ? 1 : 0);
    }
    return b;
}

Bits to_bits(uint64_t value, int width)
{
    BitWriter w;
    w.put(width < 64 ? value & ((uint64_t{1} << width) - 1) : value, width);
    return w.take();
}

}  // namespace yantra::gen
