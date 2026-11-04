// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke test of the public plugin header: it compiles as C++20 (here) and as
// C11 (plugin_h_c.c), and both languages agree on the ABI version and the
// compatibility rule.

#include <doctest/doctest.h>

#include <string_view>
#include <type_traits>

#include "plugin_h_c.h"
#include "yantra/plugin.h"

static_assert(std::is_standard_layout_v<yantra_plugin_abi_version>);
static_assert(sizeof(yantra_plugin_abi_version) == 2 * sizeof(uint32_t));
static_assert(std::is_same_v<yantra_plugin_entry_fn, yantra_plugin_abi_version (*)()>);

TEST_CASE("plugin.h version macros agree between C and C++") {
    const yantra_plugin_entry_fn entry = &yantra_test_c_entry;
    const yantra_plugin_abi_version v = entry();
    CHECK(v.major == YANTRA_PLUGIN_ABI_VERSION_MAJOR);
    CHECK(v.minor == YANTRA_PLUGIN_ABI_VERSION_MINOR);
    CHECK(std::string_view{YANTRA_PLUGIN_ENTRY_SYMBOL} == "yantra_plugin_entry");
}

TEST_CASE("plugin ABI compatibility: same major, plugin minor not newer") {
    const yantra_plugin_abi_version host{1, 3};
    CHECK(yantra_plugin_abi_compatible(host, {1, 0}));
    CHECK(yantra_plugin_abi_compatible(host, {1, 3}));
    CHECK_FALSE(yantra_plugin_abi_compatible(host, {1, 4}));
    CHECK_FALSE(yantra_plugin_abi_compatible(host, {0, 3}));
    CHECK_FALSE(yantra_plugin_abi_compatible(host, {2, 0}));
    // The C build of the inline function gives the same answers.
    CHECK(yantra_test_c_compatible(1, 3, 1, 0) == 1);
    CHECK(yantra_test_c_compatible(1, 3, 1, 4) == 0);
    CHECK(yantra_test_c_compatible(1, 3, 2, 0) == 0);
}
