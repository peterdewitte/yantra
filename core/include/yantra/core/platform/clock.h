// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Yantra's clocks (PLG-07, RAD-14, RAD-27): the monotonic clock of every
// record time (yantra_time::monotonic_ns, yr_time::monotonic_ns) and the
// system clock's UTC. One definition for the plugin loader and the radio
// host, so plugin and radio times compare.
#pragma once

#include <chrono>
#include <cstdint>

namespace yantra::platform {

// Monotonic clock in ns (> 0).
inline std::int64_t monotonic_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
               .count() +
           1;
}

// UTC from the system clock, ns since 1970-01-01.
inline std::int64_t utc_now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace yantra::platform
