// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thread names (OA-03: every thread is named): the helper names the
// calling thread and cuts long names to the 15 characters Linux keeps;
// a background job's worker carries its name while it runs.

#include "yantra/core/platform/thread_name.h"
#include "yantra/core/util/job.h"

#include <doctest/doctest.h>

#include <string>
#include <thread>

using yantra::platform::set_thread_name;
using yantra::platform::thread_name;

TEST_CASE("thread name: set, read back and cut to 15 characters") {
#if defined(__linux__)
    std::string seen_short, seen_long;
    std::thread t([&] {
        set_thread_name("yt-test");
        seen_short = thread_name();
        set_thread_name("yt-a-name-longer-than-fifteen");
        seen_long = thread_name();
    });
    t.join();
    CHECK(seen_short == "yt-test");
    CHECK(seen_long == "yt-a-name-longe");
    CHECK(seen_long.size() == 15);
#else
    set_thread_name("yt-test");  // no-op elsewhere; must not fail
#endif
}

TEST_CASE("thread name: a job's worker is named yt-job") {
    std::string name;
    auto job = yantra::util::Job::start([&](yantra::util::JobContext&) { name = thread_name(); });
    job->wait();
#if defined(__linux__)
    CHECK(name == "yt-job");
#endif
}
