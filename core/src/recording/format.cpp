// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/recording/format.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace yantra::recording {

const char* marker_action_name(MarkerAction action) noexcept {
    switch (action) {
    case MarkerAction::Mark: return "mark";
    case MarkerAction::NewFile: return "new_file";
    case MarkerAction::NewFilePause: return "new_file_pause";
    }
    return "mark";
}

std::int64_t File::last_time_ns() const {
    std::int64_t t = header.started_mono_ns;
    for (const InputLine& i : inputs) t = std::max(t, i.arrival_ns);
    for (const CycleLine& c : cycles) t = std::max(t, c.built_ns);
    for (const StateLine& s : states) t = std::max(t, s.time_ns);
    for (const PositionLine& p : positions) t = std::max(t, p.time_ns);
    for (const FloorLine& f : floors) t = std::max(t, f.time_ns);
    for (const WalkLine& w : walks) t = std::max(t, w.time_ns);
    for (const MarkerLine& m : markers) t = std::max(t, m.time_ns);
    for (const VoiceLine& v : voices) t = std::max(t, v.time_ns);
    if (end) t = std::max(t, end->time_ns);
    return t;
}

std::string file_name(const std::string& session, std::uint32_t index, Compression compression) {
    char b[16];
    std::snprintf(b, sizeof b, "_%04u", index);
    return session + b + (compression == Compression::Zstd ? kCompressedExtension : kExtension);
}

std::string quick_name(std::int64_t utc_ns) {
    std::time_t secs = static_cast<std::time_t>(utc_ns / 1'000'000'000);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char b[32];
    std::strftime(b, sizeof b, "rec_%Y%m%d_%H%M%S", &tm);
    return b;
}

bool valid_session_name(const std::string& n) {
    if (n.empty() || n.size() > 120 || n == "." || n == "..") return false;
    for (const char c : n)
        if (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    return true;
}

std::string voice_file_name(const std::string& session, std::uint32_t index, std::uint32_t number) {
    char b[32];
    std::snprintf(b, sizeof b, "_%04u_voice_%03u.wav", index, number);
    return session + b;
}

bool valid_voice_file_name(const std::string& n) {
    if (n.size() < 5 || n.size() > 200 || n.compare(n.size() - 4, 4, ".wav") != 0) return false;
    if (n.find("..") != std::string::npos) return false;
    for (const char c : n)
        if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    return true;
}

}  // namespace yantra::recording
