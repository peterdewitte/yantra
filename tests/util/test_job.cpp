// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Background job (IND-45): progress, cancel, commit point, errors. The
// tests wait on observable conditions (wait(), latches the work sets),
// never on time.
//
// Copied from yantra-v2 68ea20a: tests/core/test_job.cpp, adapted for IND-45.

#include "yantra/core/util/job.h"

#include <doctest/doctest.h>

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using yantra::util::Job;
using yantra::util::JobContext;
using yantra::util::JobState;
using yantra::util::JobStatus;

namespace {

/// A one-shot gate between threads.
class Latch {
public:
    void open() {
        const std::lock_guard l(m_);
        open_ = true;
        cv_.notify_all();
    }
    void wait() {
        std::unique_lock l(m_);
        cv_.wait(l, [&] { return open_; });
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    bool open_ = false;
};

}  // namespace

TEST_CASE("job: runs on a worker, reports progress and stage, ends Done") {
    std::mutex m;
    std::vector<JobStatus> seen;
    auto job = Job::start(
        [](JobContext& ctx) {
            ctx.stage("Fit");
            for (int i = 1; i <= 4; ++i) ctx.progress(i / 8.0);
            ctx.progress(0.1);  // never lower
            ctx.stage("Map");
            ctx.progress(0.75);
        },
        [&](const JobStatus& s) {
            const std::lock_guard l(m);
            seen.push_back(s);
        });
    job->wait();
    const auto st = job->status();
    CHECK(st.state == JobState::Done);
    CHECK(st.progress == 1.0);
    CHECK(st.stage == "Map");
    CHECK(job->finished());
    const std::lock_guard l(m);
    REQUIRE(seen.size() >= 4);
    for (std::size_t i = 1; i < seen.size(); ++i) CHECK(seen[i].progress >= seen[i - 1].progress);
    CHECK(seen.back().state == JobState::Done);
    CHECK(seen.front().stage == "Fit");
}

TEST_CASE("job: cancel while running ends Cancelled and nothing is committed") {
    Latch started;
    Latch release;
    bool committed = true;
    auto job = Job::start([&](JobContext& ctx) {
        started.open();
        release.wait();
        CHECK(ctx.cancelled());
        CHECK(ctx.cancel_flag()->load());
        committed = ctx.commit();
    });
    started.wait();
    CHECK(job->status().state == JobState::Running);
    job->cancel();
    release.open();
    job->wait();
    CHECK(job->status().state == JobState::Cancelled);
    CHECK_FALSE(committed);
}

TEST_CASE("job: a cancel after the commit point is ignored") {
    Latch committed;
    Latch release;
    auto job = Job::start([&](JobContext& ctx) {
        CHECK(ctx.commit());
        committed.open();
        release.wait();
        CHECK_FALSE(ctx.cancelled());
    });
    committed.wait();
    job->cancel();
    release.open();
    job->wait();
    CHECK(job->status().state == JobState::Done);
}

TEST_CASE("job: an exception ends Failed with its message; the listener may cancel") {
    auto failing = Job::start([](JobContext&) { throw std::runtime_error("no points"); });
    failing->wait();
    CHECK(failing->status().state == JobState::Failed);
    CHECK(failing->status().error == "no points");

    // The listener cancels at the first progress report; the work sees it.
    Job* self = nullptr;
    Latch ready;
    auto job = Job::start(
        [&](JobContext& ctx) {
            ready.wait();
            ctx.progress(0.2);
            CHECK(ctx.cancelled());
        },
        [&](const JobStatus& s) {
            if (s.state == JobState::Running && s.progress > 0.0) self->cancel();
        });
    self = job.get();
    ready.open();
    job->wait();
    CHECK(job->status().state == JobState::Cancelled);
}

TEST_CASE("job: the destructor cancels and joins a running job") {
    Latch started;
    bool saw_cancel = false;
    {
        auto job = Job::start([&](JobContext& ctx) {
            started.open();
            while (!ctx.cancelled()) std::this_thread::yield();
            saw_cancel = true;
        });
        started.wait();
    }
    CHECK(saw_cancel);
}
