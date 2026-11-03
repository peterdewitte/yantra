// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The compiled-in dependency manifest (RAD-136) matches
// tools/deps/manifest.tsv.

#include "yantra/receiver/dependencies.hpp"

#include <doctest/doctest.h>

#include <string_view>

TEST_CASE("The dependency manifest lists the bundled libraries (RAD-136)")
{
    const auto libs = yantra::receiver::bundled_libraries();
    REQUIRE(libs.size() >= 5);
    auto find = [&](std::string_view name) -> const yantra::receiver::BundledLibrary* {
        for (const auto& l : libs)
            if (name == l.name)
                return &l;
        return nullptr;
    };
    const auto* osmo = find("libosmocore");
    REQUIRE(osmo != nullptr);
    CHECK(std::string_view(osmo->version) == "1.11.0");
    CHECK(std::string_view(osmo->licence) == "GPL-2.0-or-later");
    REQUIRE(find("talloc") != nullptr);
    CHECK(std::string_view(find("talloc")->licence) == "LGPL-3.0-or-later");
    REQUIRE(find("osmo-tetra") != nullptr);
    CHECK(std::string_view(find("osmo-tetra")->version) == "59fb603");
    REQUIRE(find("pocketfft") != nullptr);
}
