/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The radio API header compiles as plain C (RAD-10): plugins may be
 * written in C.
 */
#include <yantra/radio.h>

const char *yantra_test_radio_entry_symbol_c(void);
const char *yantra_test_radio_entry_symbol_c(void)
{
    return YANTRA_RADIO_ENTRY_SYMBOL;
}
