// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Walk-test route of the indoor survey (GEO-02): "point and click" - click
// the start point on the plan, walk, click the end point - with the
// measurement samples spread evenly along the segment, plus "continuous"
// mode (every click ends one segment and starts the next), "delete last
// segment" and pause. Technology-free; docs/SURVEY.md "Walk test".
// Copied from yantra-v2 68ea20a: core/include/yantra/core/survey/walk.h,
// adapted for IND-20, GEO-02.
//
// Placement rule (TST-32): a sample taken at time t during the segment
// from click A (time tA, point pA) to click B (tB, pB) lies at
//     p(t) = pA + (t - tA) / (tB - tA) * (pB - pA)
// so samples at a steady rate are evenly spaced along the segment, the
// walker's speed being taken as constant between two clicks. A sample at
// a click time lies on the clicked point; when two segments touch (the
// joint of continuous mode) both give that point. Samples outside every
// closed segment (before the first click, during a pause, after an end
// click in point-and-click mode before the next start, in a deleted
// segment) have no position: they stay in the session but are not points
// on the plan (IND-30).
//
// Times are nanoseconds of the pipeline clock (a cycle's
// time.monotonic_ns, docs/MEASUREMENT_PIPELINE.md); clicks must not go
// back in time. Points are plan units of the floor.
//
// Not thread-safe: WalkTest (walk_test.h) holds it under its lock.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/building/model.h"

namespace yantra::survey {

using building::Point2;

enum class WalkMode {
    PointAndClick,  ///< click start, walk, click end; the next click starts a new segment
    Continuous,     ///< every click ends the open segment and starts the next
};
const char* walk_mode_name(WalkMode mode) noexcept;  ///< "point_and_click", "continuous"
std::optional<WalkMode> walk_mode_from_name(const std::string& name);

struct WalkClick {
    std::int64_t time_ns = 0;
    Point2 plan;  ///< floor units
    bool operator==(const WalkClick&) const = default;
};

struct WalkSegment {
    std::uint32_t number = 0;  ///< 1, 2, ... in click order, never reused
    WalkClick start, end;
    bool operator==(const WalkSegment&) const = default;
};

/// (t - tA) / (tB - tA) clamped to [0, 1]; 0 for a segment of zero
/// duration.
double segment_fraction(const WalkSegment& segment, std::int64_t t_ns) noexcept;
/// The placement rule above.
Point2 place_on_segment(const WalkSegment& segment, std::int64_t t_ns) noexcept;

class WalkRoute {
public:
    explicit WalkRoute(WalkMode mode = WalkMode::PointAndClick) : mode_(mode) {}

    WalkMode mode() const noexcept { return mode_; }
    /// Changing the mode drops an open start (the next click starts anew).
    void set_mode(WalkMode mode);

    /// A click on the plan. Returns the segment it closed (nullopt when it
    /// only started one). std::invalid_argument for a time before the
    /// previous click or a non-finite point (nothing changes then).
    std::optional<WalkSegment> click(const WalkClick& click);
    /// Pause: the open segment is dropped (its samples get no position,
    /// as its end is not known); after the pause the next click starts a
    /// new segment.
    void pause();
    /// Removes the last closed segment (and an open start); the next click
    /// starts a new segment. nullopt when there is none.
    std::optional<WalkSegment> delete_last();

    /// The start of the open segment, if any.
    const std::optional<WalkClick>& open_start() const noexcept { return open_; }
    /// The closed segments still in the route, in time order.
    const std::vector<WalkSegment>& segments() const noexcept { return segments_; }
    /// The closed segment whose time span holds t (the later one at a
    /// joint), or nullptr.
    const WalkSegment* segment_at(std::int64_t t_ns) const noexcept;
    /// The position of a sample at t, or nullopt (see the rule above).
    std::optional<Point2> place(std::int64_t t_ns) const noexcept;

private:
    WalkMode mode_;
    std::optional<WalkClick> open_;
    std::vector<WalkSegment> segments_;
    std::optional<std::int64_t> last_time_;
    std::uint32_t next_number_ = 1;
};

}  // namespace yantra::survey
