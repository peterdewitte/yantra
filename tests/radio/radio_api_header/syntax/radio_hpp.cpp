// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Syntax check of yantra/radio.hpp with extra warnings (-Wconversion),
// compiled by tests/radio_api/CMakeLists.txt.
#include <yantra/radio.hpp>

int yantra_syntax_check();
int yantra_syntax_check()
{
    return static_cast<int>(yantra::radio::validate_descriptor(nullptr).size());
}
