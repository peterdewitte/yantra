// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Yantra's clocks (PLG-07, RAD-14, RAD-27): the monotonic clock of every
// record time (yantra_time::monotonic_ns, yr_time::monotonic_ns) and the
// system clock's UTC. One definition for the plugin loader and the radio
// host, so plugin and radio times compare. Clock is the same pair as an
// object, so the measurement pipeline (OA-13, TST-31) and its tests can
// inject a ManualClock instead of waiting on the wall clock.
#pragma once

#include <atomic>
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

// An injectable clock: monotonic ns (same epoch as monotonic_ns()) and
// UTC ns. Thread-safe.
class Clock {
public:
    virtual ~Clock() = default;
    virtual std::int64_t monotonic_ns() const noexcept = 0;
    virtual std::int64_t utc_ns() const noexcept = 0;
};

// The computer's clocks (monotonic_ns(), utc_now_ns()).
class SystemClock final : public Clock {
public:
    std::int64_t monotonic_ns() const noexcept override { return platform::monotonic_ns(); }
    std::int64_t utc_ns() const noexcept override { return utc_now_ns(); }
    // One shared instance (it has no state).
    static const SystemClock& instance() noexcept {
        static const SystemClock clock;
        return clock;
    }
};

// A clock the caller sets (tests, replay): stands still until set or
// advanced; UTC moves with the monotonic time.
class ManualClock final : public Clock {
public:
    explicit ManualClock(std::int64_t monotonic = 1'000'000'000,
                         std::int64_t utc = 1'800'000'000'000'000'000) noexcept
        : mono_(monotonic), utc_offset_(utc - monotonic) {}
    std::int64_t monotonic_ns() const noexcept override { return mono_.load(); }
    std::int64_t utc_ns() const noexcept override { return mono_.load() + utc_offset_.load(); }
    void set(std::int64_t monotonic) noexcept { mono_.store(monotonic); }
    // Both clocks (replay of a recorded tick: its monotonic time and UTC).
    void set(std::int64_t monotonic, std::int64_t utc) noexcept {
        utc_offset_.store(utc - monotonic);
        mono_.store(monotonic);
    }
    void advance(std::int64_t ns) noexcept { mono_.fetch_add(ns); }

private:
    std::atomic<std::int64_t> mono_;
    std::atomic<std::int64_t> utc_offset_;
};

}  // namespace yantra::platform
