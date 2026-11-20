// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers of the fuzz targets (RAD-96). Every target defines
// LLVMFuzzerTestOneInput: with clang's libFuzzer in the fuzz preset
// (YANTRA_FUZZ=ON), or with replay_main.cpp in every other build, where
// ctest replays the seed corpus plus seeded mutations of it (label fuzz).
// A target fails by crashing: a sanitizer report, an uncaught exception
// (only each parser's documented error is caught) or fail() when a
// property of the output does not hold.
// Copied from yantra 68ea20a: tests/fuzz/fuzz.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace yantra::fuzz {

/// Stops the run on a broken property (libFuzzer saves the input).
[[noreturn]] inline void fail(const char* what)
{
    std::fprintf(stderr, "fuzz property failed: %s\n", what);
    std::fflush(stderr);
    std::abort();
}

/// Bits, one per byte, of `n` bytes packed MSB first.
inline std::vector<uint8_t> unpack(const uint8_t* d, size_t n)
{
    std::vector<uint8_t> bits(n * 8);
    for (size_t k = 0; k < n * 8; ++k)
        bits[k] = static_cast<uint8_t>((d[k / 8] >> (7 - k % 8)) & 1);
    return bits;
}

/// Packs bits (one per byte) MSB first; the last byte padded with 0.
inline std::vector<uint8_t> pack(const std::vector<uint8_t>& bits)
{
    std::vector<uint8_t> d((bits.size() + 7) / 8, 0);
    for (size_t k = 0; k < bits.size(); ++k)
        if (bits[k] & 1)
            d[k / 8] |= static_cast<uint8_t>(0x80u >> (k % 8));
    return d;
}

/// Reads an input front to back; zeros once it is used up.
class Reader {
public:
    Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    bool done() const { return at_ >= n_; }
    size_t left() const { return at_ < n_ ? n_ - at_ : 0; }
    uint8_t u8() { return at_ < n_ ? d_[at_++] : 0; }
    uint64_t u64()
    {
        uint64_t v = 0;
        for (int k = 0; k < 8; ++k)
            v = (v << 8) | u8();
        return v;
    }
    std::string bytes(size_t n)
    {
        std::string s;
        for (size_t k = 0; k < n && !done(); ++k)
            s.push_back(static_cast<char>(u8()));
        return s;
    }

private:
    const uint8_t* d_;
    size_t n_;
    size_t at_ = 0;
};

inline std::string text_of(const uint8_t* d, size_t n) { return std::string(reinterpret_cast<const char*>(d), n); }

}  // namespace yantra::fuzz
