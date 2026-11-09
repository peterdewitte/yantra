// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target acceptance (TST-12): an acceptance profile definition (JSON
// profile format v1, EXP-06, docs/SURVEY.md). Refused with
// std::invalid_argument, or a profile validate() accepts whose JSON reads
// back to the same JSON.

#include "fuzz.hpp"

#include "yantra/core/survey/acceptance.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace sv = yantra::survey;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 64 * 1024) return 0;
    const auto j = nlohmann::json::parse(yantra::fuzz::text_of(data, size), nullptr, false);
    if (j.is_discarded()) return 0;
    sv::AcceptanceProfile p;
    try {
        p = sv::profile_from_json("fuzz", j);
    } catch (const std::invalid_argument&) {
        return 0;
    }
    if (!sv::validate(p).empty()) fail("profile_from_json gave a profile validate() refuses");
    const nlohmann::json out = sv::profile_json(p);
    sv::AcceptanceProfile again;
    try {
        again = sv::profile_from_json("fuzz", out);
    } catch (const std::invalid_argument&) {
        fail("a profile's own JSON is refused");
    }
    if (sv::profile_json(again) != out) fail("profile JSON round trip changed the profile");
    return 0;
}
