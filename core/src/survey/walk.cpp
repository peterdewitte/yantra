// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/survey/walk.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace yantra::survey {

const char* walk_mode_name(WalkMode mode) noexcept {
    return mode == WalkMode::Continuous ? "continuous" : "point_and_click";
}

std::optional<WalkMode> walk_mode_from_name(const std::string& name) {
    if (name == "point_and_click") return WalkMode::PointAndClick;
    if (name == "continuous") return WalkMode::Continuous;
    return std::nullopt;
}

double segment_fraction(const WalkSegment& s, std::int64_t t_ns) noexcept {
    const std::int64_t span = s.end.time_ns - s.start.time_ns;
    if (span <= 0) return 0.0;
    const double f = static_cast<double>(t_ns - s.start.time_ns) / static_cast<double>(span);
    return std::clamp(f, 0.0, 1.0);
}

Point2 place_on_segment(const WalkSegment& s, std::int64_t t_ns) noexcept {
    const double f = segment_fraction(s, t_ns);
    if (f >= 1.0) return s.end.plan;  // exactly the clicked point
    return {s.start.plan.x + f * (s.end.plan.x - s.start.plan.x), s.start.plan.y + f * (s.end.plan.y - s.start.plan.y)};
}

void WalkRoute::set_mode(WalkMode mode) {
    if (mode != mode_) open_.reset();
    mode_ = mode;
}

std::optional<WalkSegment> WalkRoute::click(const WalkClick& c) {
    if (!std::isfinite(c.plan.x) || !std::isfinite(c.plan.y)) throw std::invalid_argument("walk click: not a point");
    if (last_time_ && c.time_ns < *last_time_) throw std::invalid_argument("walk click: earlier than the last click");
    last_time_ = c.time_ns;
    if (!open_) {
        open_ = c;
        return std::nullopt;
    }
    WalkSegment s{next_number_++, *open_, c};
    segments_.push_back(s);
    if (mode_ == WalkMode::Continuous) {
        open_ = c;
    } else {
        open_.reset();
    }
    return s;
}

void WalkRoute::pause() { open_.reset(); }

std::optional<WalkSegment> WalkRoute::delete_last() {
    open_.reset();
    if (segments_.empty()) return std::nullopt;
    WalkSegment s = segments_.back();
    segments_.pop_back();
    return s;
}

const WalkSegment* WalkRoute::segment_at(std::int64_t t_ns) const noexcept {
    // Segments are in time order and do not overlap (they may touch).
    auto it = std::upper_bound(segments_.begin(), segments_.end(), t_ns,
                               [](std::int64_t t, const WalkSegment& s) { return t < s.start.time_ns; });
    if (it == segments_.begin()) return nullptr;
    --it;
    return t_ns <= it->end.time_ns ? &*it : nullptr;
}

std::optional<Point2> WalkRoute::place(std::int64_t t_ns) const noexcept {
    const WalkSegment* s = segment_at(t_ns);
    if (s == nullptr) return std::nullopt;
    return place_on_segment(*s, t_ns);
}

}  // namespace yantra::survey
