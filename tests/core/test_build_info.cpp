// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke test of the core library: it links, and its build information
// matches the build configuration.

#include <doctest/doctest.h>

#include <string>

#include "yantra/core/build_info.h"
#include "yantra/plugin.h"

TEST_CASE("build_info reports a consistent version") {
    const auto& info = yantra::core::build_info();
    CHECK(info.version.major >= 0);
    CHECK(info.version.minor >= 0);
    CHECK(info.version.patch >= 0);
    const std::string expected = std::to_string(info.version.major) + "." +
                                 std::to_string(info.version.minor) + "." +
                                 std::to_string(info.version.patch);
    CHECK(info.version_string == expected);
}

TEST_CASE("build_info reports the SIMD option and plugin API of this build") {
    const auto& info = yantra::core::build_info();
    CHECK(info.simd == (YANTRA_TEST_EXPECT_SIMD != 0));
    CHECK(info.plugin_api_major == YANTRA_PLUGIN_API_VERSION_MAJOR);
    CHECK(info.plugin_api_minor == YANTRA_PLUGIN_API_VERSION_MINOR);
}

TEST_CASE("build_info is the same object on every call") {
    CHECK(&yantra::core::build_info() == &yantra::core::build_info());
}
