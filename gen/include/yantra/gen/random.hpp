// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Deterministic random numbers of the channel model and scenario engine
// (RAD-72..74):
// xoshiro256** seeded through SplitMix64, with uniform and Gaussian draws
// written out here (not std::*_distribution, whose output differs between
// standard libraries), so a seed gives the same samples everywhere.
// Copied from yantra 68ea20a (core/include/yantra/core/sim/random.h).
#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numbers>

namespace yantra::gen {

/// SplitMix64 step: a well-mixed 64-bit value from a counter.
constexpr uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// A seed for one component (e.g. one cell's fading) derived from the
/// scenario seed and a list of tags, so components never share a stream
/// and adding one does not change the others.
constexpr uint64_t derive_seed(uint64_t seed, std::initializer_list<uint64_t> tags) {
    uint64_t s = seed;
    uint64_t out = splitmix64(s);
    for (uint64_t t : tags) {
        s ^= t + 0x632BE59BD9B4E019ull + (out << 6) + (out >> 2);
        out = splitmix64(s);
    }
    return out;
}

/// FNV-1a hash of a string, for use as a derive_seed tag.
constexpr uint64_t string_tag(const char* s) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (; *s; ++s) h = (h ^ static_cast<unsigned char>(*s)) * 0x100000001B3ull;
    return h;
}

class Rng {
public:
    explicit Rng(uint64_t seed) {
        uint64_t s = seed;
        for (auto& w : s_) w = splitmix64(s);
    }

    uint64_t next() {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }
    /// Uniform in [0, 1).
    double uniform() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    /// Uniform in [lo, hi).
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    /// Standard normal (Box-Muller, both values used).
    double gaussian() {
        if (have_spare_) {
            have_spare_ = false;
            return spare_;
        }
        double u1 = 1.0 - uniform();  // (0, 1]
        double u2 = uniform();
        double r = std::sqrt(-2.0 * std::log(u1));
        double a = 2.0 * std::numbers::pi * u2;
        spare_ = r * std::sin(a);
        have_spare_ = true;
        return r * std::cos(a);
    }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4];
    double spare_ = 0.0;
    bool have_spare_ = false;
};

}  // namespace yantra::gen
