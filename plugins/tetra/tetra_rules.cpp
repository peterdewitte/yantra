// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA technology rules (tetra_rules.h). The reselection follows
// yantra-v2 68ea20a core/src/celldb/reselection.cpp (see the header).

#include "tetra_rules.h"

#include <cstdio>

namespace yantra::tetra {

const char* serving_state_name(ServingState s) noexcept
{
    switch (s) {
    case ServingState::Exclusive: return "exclusive";
    case ServingState::Superior: return "superior";
    case ServingState::Usable: return "usable";
    case ServingState::Improvable: return "improvable";
    case ServingState::Relinquishable: return "relinquishable";
    case ServingState::RadioLinkFailure: return "radio_link_failure";
    }
    return "usable";
}

const char* reselection_name(Reselection r) noexcept
{
    switch (r) {
    case Reselection::None: return "none";
    case Reselection::Slow: return "slow";
    case Reselection::Fast: return "fast";
    case Reselection::LinkFailure: return "link_failure";
    }
    return "none";
}

std::optional<ReselectionResult> reselect(double c1,
                                          const std::optional<ReselectionThresholds>& thresholds,
                                          std::size_t neighbour_count, NeighbourC2 c2,
                                          const void* context) noexcept
{
    ReselectionResult r;
    r.serving_c1 = c1;
    // The best suitable neighbour: the highest C2 > 0 (the first on a tie).
    double best = 0.0;
    for (std::size_t i = 0; i < neighbour_count && c2 != nullptr; ++i) {
        const std::optional<double> v = c2(context, i);
        if (v && *v > 0.0 && (r.best < 0 || *v > best)) {
            best = *v;
            r.best = static_cast<int>(i);
        }
    }
    char* const out = r.reason;
    const std::size_t n = sizeof r.reason;
    if (c1 < 0.0) {
        r.state = ServingState::RadioLinkFailure;
        if (r.best >= 0) {
            r.reselection = Reselection::LinkFailure;
            r.target = r.best;
            std::snprintf(out, n, "C1 %.1f dB < 0 (radio link failure); reselect to neighbour %d, C2 %.1f dB",
                          c1, r.best, best);
        } else {
            std::snprintf(out, n, "C1 %.1f dB < 0 (radio link failure); no neighbour with C2 > 0", c1);
        }
        return r;
    }
    if (r.best < 0) {
        r.state = ServingState::Exclusive;
        std::snprintf(out, n, "C1 %.1f dB; no neighbour with C2 > 0", c1);
        return r;
    }
    if (c1 >= best) {
        r.state = ServingState::Superior;
        std::snprintf(out, n, "C1 %.1f dB >= best C2 %.1f dB (neighbour %d)", c1, best, r.best);
        return r;
    }
    if (!thresholds)
        return std::nullopt;
    const ReselectionThresholds& t = *thresholds;
    if (c1 < t.frt_db) {
        r.state = ServingState::Relinquishable;
        if (best > c1 + t.frh_db) {
            r.reselection = Reselection::Fast;
            r.target = r.best;
            std::snprintf(out, n,
                          "C1 %.1f dB < FRT %.1f dB; fast reselection to neighbour %d: C2 %.1f dB > C1 + FRH %.1f dB",
                          c1, t.frt_db, r.best, best, c1 + t.frh_db);
        } else {
            std::snprintf(out, n, "C1 %.1f dB < FRT %.1f dB; best C2 %.1f dB is not above C1 + FRH %.1f dB",
                          c1, t.frt_db, best, c1 + t.frh_db);
        }
        return r;
    }
    if (c1 < t.srt_db) {
        r.state = ServingState::Improvable;
        if (best > c1 + t.srh_db) {
            r.reselection = Reselection::Slow;
            r.target = r.best;
            std::snprintf(out, n,
                          "C1 %.1f dB < SRT %.1f dB; slow reselection to neighbour %d: C2 %.1f dB > C1 + SRH %.1f dB",
                          c1, t.srt_db, r.best, best, c1 + t.srh_db);
        } else {
            std::snprintf(out, n, "C1 %.1f dB < SRT %.1f dB; best C2 %.1f dB is not above C1 + SRH %.1f dB",
                          c1, t.srt_db, best, c1 + t.srh_db);
        }
        return r;
    }
    r.state = ServingState::Usable;
    std::snprintf(out, n, "C1 %.1f dB >= SRT %.1f dB; best C2 %.1f dB (neighbour %d) is kept as a candidate",
                  c1, t.srt_db, best, r.best);
    return r;
}

std::int64_t snap_to_grid(std::int64_t value, const CodeGrid& g) noexcept
{
    // Nearest code, ties to the higher one: floor((2 d + step) / (2 step)).
    const std::int64_t num = 2 * (value - g.base) + g.step;
    const std::int64_t den = 2 * g.step;
    std::int64_t code = num / den;
    if (num % den != 0 && num < 0)
        --code;
    if (code < g.first_code)
        code = g.first_code;
    if (code > g.last_code)
        code = g.last_code;
    return g.base + g.step * code;
}

}  // namespace yantra::tetra
