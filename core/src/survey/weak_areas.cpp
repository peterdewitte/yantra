// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Weak areas of a floor (weak_areas.h; EXP-05, EXP-06, DEMO-16).

#include "yantra/core/survey/weak_areas.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <tuple>

namespace yantra::survey {

namespace {

struct Weak {
    Point2 m;
    double value = 0.0;
    const std::string* label = nullptr;
};

std::size_t find(std::vector<std::size_t>& parent, std::size_t i) {
    while (parent[i] != i) i = parent[i] = parent[parent[i]];
    return i;
}

}  // namespace

std::vector<WeakArea> weak_areas(const std::vector<SurveyPoint>& points, const WeakAreaOptions& o) {
    std::vector<Weak> weak;
    for (const SurveyPoint& p : points)
        if (p.metres && p.value && std::isfinite(*p.value) && p.unit == o.unit && *p.value < o.threshold)
            weak.push_back({*p.metres, *p.value, &p.label});
    if (weak.empty()) return {};

    // Single linkage over a grid of link_m cells: only the 3 x 3
    // neighbouring cells can hold points within link_m.
    const double link = o.link_m > 0.0 ? o.link_m : 1e-9;
    std::vector<std::size_t> parent(weak.size());
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    std::map<std::pair<long long, long long>, std::vector<std::size_t>> grid;
    const auto cell = [&](const Point2& m) {
        return std::pair{static_cast<long long>(std::floor(m.x / link)), static_cast<long long>(std::floor(m.y / link))};
    };
    for (std::size_t i = 0; i < weak.size(); ++i) grid[cell(weak[i].m)].push_back(i);
    for (std::size_t i = 0; i < weak.size(); ++i) {
        const auto [cx, cy] = cell(weak[i].m);
        for (long long dx = -1; dx <= 1; ++dx)
            for (long long dy = -1; dy <= 1; ++dy) {
                const auto it = grid.find({cx + dx, cy + dy});
                if (it == grid.end()) continue;
                for (std::size_t j : it->second)
                    if (j > i && std::hypot(weak[i].m.x - weak[j].m.x, weak[i].m.y - weak[j].m.y) <= o.link_m)
                        parent[find(parent, i)] = find(parent, j);
            }
    }

    std::map<std::size_t, std::vector<std::size_t>> groups;  // root -> members in point order
    for (std::size_t i = 0; i < weak.size(); ++i) groups[find(parent, i)].push_back(i);
    std::vector<WeakArea> out;
    for (const auto& [root, members] : groups) {
        WeakArea a;
        a.points = members.size();
        a.lowest = weak[members.front()].value;
        a.min_m = a.max_m = weak[members.front()].m;
        std::vector<std::pair<std::string, std::size_t>> labels;  // in first-seen order
        for (std::size_t i : members) {
            const Weak& w = weak[i];
            a.lowest = std::min(a.lowest, w.value);
            a.min_m = {std::min(a.min_m.x, w.m.x), std::min(a.min_m.y, w.m.y)};
            a.max_m = {std::max(a.max_m.x, w.m.x), std::max(a.max_m.y, w.m.y)};
            if (w.label->empty()) continue;
            auto it = std::find_if(labels.begin(), labels.end(), [&](const auto& l) { return l.first == *w.label; });
            if (it == labels.end()) labels.emplace_back(*w.label, 1);
            else ++it->second;
        }
        std::size_t best = 0;
        for (const auto& [text, n] : labels)
            if (n > best) {
                best = n;
                a.label = text;
            }
        out.push_back(std::move(a));
    }
    std::sort(out.begin(), out.end(), [](const WeakArea& x, const WeakArea& y) {
        return std::tuple(-static_cast<long long>(x.points), x.lowest, x.min_m.x, x.min_m.y) <
               std::tuple(-static_cast<long long>(y.points), y.lowest, y.min_m.x, y.min_m.y);
    });
    return out;
}

std::string plan_part(Point2 min_m, Point2 max_m, Point2 size) {
    const auto third = [](double v, double extent) {
        if (!(extent > 0.0)) return 1;
        const double f = v / extent;
        return f < 1.0 / 3.0 ? 0 : (f < 2.0 / 3.0 ? 1 : 2);
    };
    const int col = third((min_m.x + max_m.x) / 2.0, size.x);
    const int row = third((min_m.y + max_m.y) / 2.0, size.y);
    static const char* const kParts[3][3] = {{"top-left", "top", "top-right"},
                                             {"left", "centre", "right"},
                                             {"bottom-left", "bottom", "bottom-right"}};
    return kParts[row][col];
}

}  // namespace yantra::survey
