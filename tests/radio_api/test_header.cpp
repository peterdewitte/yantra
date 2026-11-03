// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the radio API header (RAD-10, RAD-11, RAD-16): it compiles as C
// and C++, names the single plugin entry point of RAD-133, and its inline
// helpers work from both languages.
#include <doctest/doctest.h>
#include <yantra/radio.h>

#include <string_view>

extern "C" const char* yantra_test_radio_entry_symbol_c(void);
extern "C" int yantra_test_c_helpers(void);

TEST_CASE("radio.h names the plugin entry point in C and C++")
{
    CHECK(std::string_view(YANTRA_RADIO_ENTRY_SYMBOL) == "yantra_radio_entry");
    CHECK(std::string_view(yantra_test_radio_entry_symbol_c()) == YANTRA_RADIO_ENTRY_SYMBOL);
}

TEST_CASE("the SIMD switch is defined as 0 or 1 (RAD-90)")
{
    CHECK((YANTRA_SIMD == 0 || YANTRA_SIMD == 1));
}

TEST_CASE("the C helpers work from C (RAD-10)")
{
    CHECK(yantra_test_c_helpers() == 1);
}
