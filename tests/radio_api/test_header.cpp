// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke test of the radio API header (RAD-02, RAD-10): it compiles as C
// and C++ and names the single plugin entry point of RAD-133.
#include <doctest/doctest.h>
#include <yantra/radio.h>

#include <string_view>

extern "C" const char *yantra_test_radio_entry_symbol_c(void);

TEST_CASE("radio.h names the plugin entry point in C and C++")
{
    CHECK(std::string_view(YANTRA_RADIO_ENTRY_SYMBOL) == "yantra_radio_entry");
    CHECK(std::string_view(yantra_test_radio_entry_symbol_c()) == YANTRA_RADIO_ENTRY_SYMBOL);
}

TEST_CASE("the SIMD switch is defined as 0 or 1 (RAD-90)")
{
    CHECK((YANTRA_SIMD == 0 || YANTRA_SIMD == 1));
}
