// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A background job (IND-45, OA-03): one worker thread runs a piece of work
// that reports progress and checks a cancel flag, so the UI thread never
// waits on it. The UI polls status() each frame or gets every change
// through a listener; tests wait on observable conditions (wait(), the
// listener), never on time.
//
// Commit point: work that stores a result calls JobContext::commit() just
// before; after a successful commit a cancel() is ignored and the job ends
// Done, before it a cancel() makes commit() return false and the job ends
// Cancelled. So "Cancelled" always means nothing was stored.
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/util/job.h.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace yantra::util {

enum class JobState { Running, Done, Cancelled, Failed };

struct JobStatus {
    JobState state = JobState::Running;
    double progress = 0.0;  ///< 0 .. 1, non-decreasing
    std::string stage;      ///< what runs now, e.g. "Fit"
    std::string error;      ///< Failed: the exception's message
    bool operator==(const JobStatus&) const = default;
};

class Job;

/// What the work sees of its job.
class JobContext {
public:
    /// True once cancel() was called (and before a commit).
    bool cancelled() const;
    /// The flag itself, for loops that take an atomic (raytrace::RunControl).
    const std::atomic<bool>* cancel_flag() const;
    /// Sets the progress (clamped to 0..1, never lower than before).
    void progress(double fraction);
    /// Sets the stage text (and keeps the progress).
    void stage(const std::string& text);
    /// The commit point (see above): true when the work may store its
    /// result; false when the job was cancelled first.
    bool commit();

private:
    friend class Job;
    explicit JobContext(Job& job) : job_(job) {}
    Job& job_;
};

class Job {
public:
    using Work = std::function<void(JobContext&)>;
    /// Called on the worker thread after every status change (progress,
    /// stage, end), without any lock held.
    using Listener = std::function<void(const JobStatus&)>;

    /// Starts `work` on a new thread.
    static std::unique_ptr<Job> start(Work work, Listener listener = {});

    /// Cancels and waits for the worker.
    ~Job();
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    JobStatus status() const;
    bool finished() const;
    /// Asks the work to stop (from any thread, also the listener).
    void cancel();
    /// Blocks until the job has finished.
    void wait() const;

private:
    friend class JobContext;
    Job() = default;
    void update(const std::function<void(JobStatus&)>& change);

    mutable std::mutex mutex_;
    mutable std::condition_variable done_;
    JobStatus status_;
    bool committed_ = false;
    std::atomic<bool> cancel_{false};
    Listener listener_;
    std::thread worker_;  // last: started when everything above exists
};

}  // namespace yantra::util
