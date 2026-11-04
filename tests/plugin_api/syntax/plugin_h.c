/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Syntax check of yantra/plugin.h in the oldest modes a plugin may use
 * (C99, C++11), and next to yantra/radio.h without name clashes,
 * compiled by tests/plugin_api/CMakeLists.txt.
 */
#include <yantra/plugin.h>
#include <yantra/radio.h>

int yantra_syntax_check(void);
int yantra_syntax_check(void)
{
    return (int)(sizeof(yantra_plugin_descriptor) + sizeof(yr_descriptor));
}
