// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Calendar time on Linux and Windows (HW-06): the thread-safe broken-down
// time of a std::time_t in UTC or local time, its inverse for UTC, and the
// local offset from UTC. POSIX: gmtime_r, localtime_r, timegm, tm_gmtoff;
// Windows (UCRT): gmtime_s, localtime_s, _mkgmtime and the difference of
// the two broken-down times.
#pragma once

#include <ctime>

namespace yantra::platform {

// `t` in UTC.
inline std::tm utc_tm(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    return tm;
}

// `t` in the local time zone.
inline std::tm local_tm(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// The time_t of a broken-down UTC time (timegm); tm_isdst is ignored.
inline std::time_t utc_time(std::tm tm) {
#if defined(_WIN32)
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

// Seconds the local time zone is ahead of UTC at `t` (tm_gmtoff of
// local_tm(t)).
inline long utc_offset_seconds(std::time_t t) {
#if defined(_WIN32)
    std::tm local = local_tm(t);
    local.tm_isdst = 0;
    return static_cast<long>(utc_time(local) - t);
#else
    return local_tm(t).tm_gmtoff;
#endif
}

}  // namespace yantra::platform
