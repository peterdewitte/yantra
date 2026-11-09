// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A parallel loop over jobs for the ray tracer, the fit and the residual
// interpolation (internal): RunControl threads, cancel flag and progress.
//
// Copied from yantra-v2 68ea20a: core/src/raytrace/parallel.h.
#pragma once

#include "yantra/core/raytrace/model.h"
#include "yantra/core/platform/thread_name.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace yantra::raytrace::detail {

inline unsigned thread_count(const RunControl& run, std::size_t jobs) {
    unsigned n = run.threads ? run.threads : std::max(1u, std::thread::hardware_concurrency());
    if (jobs < n) n = static_cast<unsigned>(std::max<std::size_t>(jobs, 1));
    return n;
}

/// Runs job(i) for i < count over the threads; the calling thread takes
/// part and reports progress. Returns false when cancelled.
template <class Job>
bool run_parallel(const RunControl& run, std::size_t count, Job&& job) {
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    std::atomic<bool> stop{false};
    std::exception_ptr error;
    std::mutex error_mutex;
    auto cancelled = [&] { return stop.load() || (run.cancel && run.cancel->load()); };
    auto worker = [&] {
        platform::set_thread_name("yt-raytrace");
        try {
            for (;;) {
                if (cancelled()) return;
                std::size_t i = next.fetch_add(1);
                if (i >= count) return;
                job(i);
                done.fetch_add(1);
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(error_mutex);
            if (!error) error = std::current_exception();
            stop = true;
        }
    };
    const unsigned n = thread_count(run, count);
    std::vector<std::thread> threads;
    threads.reserve(n - 1);
    for (unsigned k = 1; k < n; ++k) threads.emplace_back(worker);
    // The calling thread works too, and reports progress between jobs.
    std::size_t reported = 0;
    const std::size_t step = std::max<std::size_t>(1, count / 200);
    try {
        for (;;) {
            if (cancelled()) break;
            std::size_t i = next.fetch_add(1);
            if (i >= count) break;
            job(i);
            std::size_t d = done.fetch_add(1) + 1;
            if (run.progress && d >= reported + step) {
                reported = d;
                run.progress(static_cast<double>(d) / static_cast<double>(count));
            }
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (!error) error = std::current_exception();
        stop = true;
    }
    for (auto& t : threads) t.join();
    if (error) std::rethrow_exception(error);
    const bool was_cancelled = run.cancel && run.cancel->load() && done.load() < count;
    if (!was_cancelled && run.progress) run.progress(1.0);
    return !was_cancelled;
}

}  // namespace yantra::raytrace::detail
