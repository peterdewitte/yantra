// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Context snapshots (PLG-27): which events a change raises, and the
// publisher's thread-safe snapshots and subscriptions.

#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

#include <yantra/core/context/context.h>

namespace yc = yantra::context;

TEST_CASE("context: changes name the events of a snapshot change") {
    yc::Snapshot a;
    yc::Snapshot b = a;
    CHECK(yc::changes(a, b) == 0u);
    b.plan = yc::FloorPlan{"Plan", "", "f1", "Ground", true, 10.0, 8.0};
    b.scenes["f1"] = "{}";
    CHECK(yc::changes(a, b) == yc::kPlanChanged);  // a new plan carries its scene
    yc::Snapshot c = b;
    c.scenes["f1"] = R"({"walls":[]})";
    CHECK(yc::changes(b, c) == yc::kSceneChanged);
    c.cursor = yc::PlanPosition{"f1", "Ground", 1.0, 2.0, 5};
    c.walk = yc::PlanPosition{"f1", "Ground", 3.0, 4.0, 5};
    CHECK(yc::changes(b, c) == (yc::kSceneChanged | yc::kCursorMoved | yc::kWalkPosition));
}

TEST_CASE("context: the publisher stamps positions and tells subscribers") {
    yc::ContextPublisher pub;
    std::atomic<std::uint32_t> seen{0};
    const std::uint64_t id = pub.subscribe([&](std::uint32_t bits) { seen |= bits; });
    CHECK(pub.snapshot() != nullptr);
    CHECK(!pub.snapshot()->plan);
    yc::Snapshot s;
    s.plan = yc::FloorPlan{"Plan", "", "f1", "Ground", true, 10.0, 8.0};
    CHECK(pub.publish(s) == yc::kPlanChanged);
    CHECK(pub.set_cursor(yc::PlanPosition{"f1", "Ground", 1.0, 1.0, 0}) == yc::kCursorMoved);
    CHECK(pub.snapshot()->cursor->moved_monotonic_ns > 0);
    CHECK(pub.set_walk(std::nullopt) == 0u);
    CHECK(seen.load() == (yc::kPlanChanged | yc::kCursorMoved));
    pub.unsubscribe(id);
    seen = 0;
    pub.set_cursor(std::nullopt);
    CHECK(seen.load() == 0u);
}

TEST_CASE("context: snapshots stay valid while another thread publishes") {
    yc::ContextPublisher pub;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (int i = 0; i < 2000; ++i)
            pub.set_cursor(yc::PlanPosition{"f1", "Ground", static_cast<double>(i), 0.0, 0});
        stop = true;
    });
    std::size_t reads = 0;
    while (!stop.load()) {
        const auto snap = pub.snapshot();
        if (snap->cursor) CHECK(snap->cursor->floor_id == "f1");
        ++reads;
    }
    writer.join();
    CHECK(pub.snapshot()->cursor->x_m == doctest::Approx(1999.0));
    CHECK(reads > 0);
}
