// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The guards of a test stream from the fake SoapySDR module (RAD-92) in
// signal time instead of wall-clock time (RAD-90, TST-23).
//
// An unpaced fake stream ("paced": false) is CPU-bound: the stream thread
// generates the signal and runs the engine as fast as the processor
// allows, so the wall-clock time a test needs for its records grows with
// the machine's load (under ASan: 35 s alone, over 150 s in a full
// parallel ctest run, 0 records in 150 s with 24 copies on 12 threads).
// A wall-clock limit on such a stream is a test of the machine's speed.
// StreamGuard stops the stream instead after a given amount of signal,
// read from the record times (the engine stamps them in signal time), and
// a stall guard catches a stream that hangs: no record and no event for
// stall_s x YANTRA_TEST_TIME_SCALE wall seconds.
#pragma once

#include <yantra/radios/record_file.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace yantra::tests {

class StreamGuard {
public:
    /// Sets `stop` once the records span `signal_s` seconds of signal, or
    /// when nothing arrived for `stall_s` x YANTRA_TEST_TIME_SCALE seconds.
    StreamGuard(double signal_s, std::atomic<bool>& stop, double stall_s = 60.0)
        : limit_ns_(static_cast<int64_t>(signal_s * 1e9)),
          stall_(std::chrono::duration_cast<Clock::duration>(
              std::chrono::duration<double>(stall_s * YANTRA_TEST_TIME_SCALE))),
          stop_(stop), last_(Clock::now().time_since_epoch().count()), watchdog_([this] { watch(); })
    {
    }
    ~StreamGuard()
    {
        {
            std::lock_guard lock(mutex_);
            quit_ = true;
        }
        cv_.notify_all();
        watchdog_.join();
    }
    StreamGuard(const StreamGuard&) = delete;
    StreamGuard& operator=(const StreamGuard&) = delete;

    /// Call for every record (before the test's own handling).
    void record(const radios::Record& r)
    {
        progress();
        const int64_t t = r.time.monotonic_ns;
        if (last_record_ns_ >= 0 && t > last_record_ns_) {
            // A discontinuity (overflow, retune) restarts the record times
            // from the host clock: that step counts as one capped step,
            // not as the wall-clock time it jumped.
            signal_ns_ += std::min(t - last_record_ns_, kMaxStepNs);
        }
        last_record_ns_ = t;
        if (signal_ns_ >= limit_ns_ && !stop_.load()) {
            set_reason("signal");
            stop_ = true;
        }
    }
    /// Call for every event: progress for the stall guard.
    void event() { progress(); }

    /// Seconds of signal the records spanned.
    double signal_s() const { return static_cast<double>(signal_ns_) * 1e-9; }
    /// "signal" (the signal limit), "stalled" (the stall guard) or empty.
    std::string reason() const
    {
        std::lock_guard lock(mutex_);
        return reason_;
    }

private:
    using Clock = std::chrono::steady_clock;
    static constexpr int64_t kMaxStepNs = 4'000'000'000;

    void progress() { last_ = Clock::now().time_since_epoch().count(); }
    void set_reason(const char* why)
    {
        std::lock_guard lock(mutex_);
        if (reason_.empty())
            reason_ = why;
    }
    void watch()
    {
        std::unique_lock lock(mutex_);
        while (!cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return quit_; })) {
            const Clock::time_point last{Clock::duration(last_.load())};
            if (Clock::now() - last > stall_ && !stop_.load()) {
                if (reason_.empty())
                    reason_ = "stalled";
                stop_ = true;
            }
        }
    }

    const int64_t limit_ns_;
    const Clock::duration stall_;
    std::atomic<bool>& stop_;
    std::atomic<Clock::rep> last_;
    int64_t last_record_ns_ = -1;
    int64_t signal_ns_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool quit_ = false;
    std::string reason_;
    std::thread watchdog_; // last: starts after the members above
};

} // namespace yantra::tests
