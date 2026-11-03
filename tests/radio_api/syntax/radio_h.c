/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Syntax check of yantra/radio.h in the oldest modes a plugin may use
 * (C99, C++11), compiled by tests/radio_api/CMakeLists.txt.
 */
#include <yantra/radio.h>

int yantra_syntax_check(void);
int yantra_syntax_check(void)
{
    return (int)sizeof(yr_descriptor);
}
