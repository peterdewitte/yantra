// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio API header of the pinned yantra-radios submodule (PLG-26,
// OA-01) is available to Yantra's build: it compiles as C++20 (here) and
// C11 (radio_h_c.c), its version is 1.x (the major version Yantra's radio
// host is written for, RAD-11), and both languages agree on it.

#include <doctest/doctest.h>

#include <yantra/radio.h>

#include <string_view>

#include "radio_h_c.h"

static_assert(YR_API_VERSION_MAJOR == 1u, "Yantra is written for radio API 1.x (PLG-26)");

TEST_CASE("the pinned radio API is version 1.x")
{
    CHECK(YR_API_VERSION_MAJOR == 1u);
    CHECK(yr_api_version_compiled().major == 1u);
    // CMake read the same numbers from the header (build summary).
    CHECK(YR_API_VERSION_MAJOR == YANTRA_TEST_RADIO_API_MAJOR);
    CHECK(YR_API_VERSION_MINOR == YANTRA_TEST_RADIO_API_MINOR);
    CHECK(std::string_view{YANTRA_RADIO_ENTRY_SYMBOL} == "yantra_radio_entry");
}

TEST_CASE("radio.h agrees between C and C++")
{
    CHECK(yantra_test_radio_c_major() == YR_API_VERSION_MAJOR);
    CHECK(yantra_test_radio_c_minor() == YR_API_VERSION_MINOR);
    const yr_api_version host = yr_api_version_compiled();
    CHECK(yr_api_compatible(host, host));
    CHECK(yantra_test_radio_c_compatible(host.major, host.minor, host.major, host.minor) == 1);
    CHECK(yantra_test_radio_c_compatible(host.major, host.minor, host.major, host.minor + 1) == 0);
    CHECK(yantra_test_radio_c_compatible(host.major, host.minor, host.major + 1, 0) == 0);
}
