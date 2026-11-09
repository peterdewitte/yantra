// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Background job (IND-45); see job.h.
// Copied from yantra-v2 68ea20a: core/src/util/job.cpp.

#include "yantra/core/util/job.h"
#include "yantra/core/platform/thread_name.h"

#include <algorithm>
#include <cmath>
#include <exception>

namespace yantra::util {

bool JobContext::cancelled() const { return job_.cancel_.load(); }

const std::atomic<bool>* JobContext::cancel_flag() const { return &job_.cancel_; }

void JobContext::progress(double fraction) {
    if (!std::isfinite(fraction)) return;
    job_.update([&](JobStatus& s) { s.progress = std::max(s.progress, std::clamp(fraction, 0.0, 1.0)); });
}

void JobContext::stage(const std::string& text) {
    job_.update([&](JobStatus& s) { s.stage = text; });
}

bool JobContext::commit() {
    const std::lock_guard lock(job_.mutex_);
    if (job_.cancel_.load()) return false;
    job_.committed_ = true;
    return true;
}

std::unique_ptr<Job> Job::start(Work work, Listener listener) {
    std::unique_ptr<Job> job(new Job());
    job->listener_ = std::move(listener);
    Job* j = job.get();
    j->worker_ = std::thread([j, work = std::move(work)] {
        platform::set_thread_name("yt-job");
        JobContext ctx(*j);
        JobState end = JobState::Done;
        std::string error;
        try {
            work(ctx);
            const std::lock_guard lock(j->mutex_);
            if (j->cancel_.load() && !j->committed_) end = JobState::Cancelled;
        } catch (const std::exception& e) {
            end = JobState::Failed;
            error = e.what();
        } catch (...) {
            end = JobState::Failed;
            error = "unknown error";
        }
        j->update([&](JobStatus& s) {
            s.state = end;
            s.error = error;
            if (end == JobState::Done) s.progress = 1.0;
        });
        // Wake waiters last: the job may be destroyed right after.
        const std::lock_guard lock(j->mutex_);
        j->done_.notify_all();
    });
    return job;
}

Job::~Job() {
    cancel();
    if (worker_.joinable()) worker_.join();
}

JobStatus Job::status() const {
    const std::lock_guard lock(mutex_);
    return status_;
}

bool Job::finished() const { return status().state != JobState::Running; }

void Job::cancel() {
    const std::lock_guard lock(mutex_);
    if (!committed_) cancel_.store(true);
}

void Job::wait() const {
    std::unique_lock lock(mutex_);
    done_.wait(lock, [&] { return status_.state != JobState::Running; });
}

void Job::update(const std::function<void(JobStatus&)>& change) {
    JobStatus now;
    {
        const std::lock_guard lock(mutex_);
        JobStatus before = status_;
        change(status_);
        if (status_ == before) return;
        now = status_;
    }
    if (listener_) listener_(now);
}

}  // namespace yantra::util
