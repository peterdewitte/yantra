/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Replacement for libosmocore's src/core/backtrace.c in the RAD-136
 * subset. The upstream file logs through libosmocore's logging framework,
 * which the subset leaves out; panic.c only needs osmo_generate_backtrace
 * before it aborts. Inside a radio plugin nothing is printed: the
 * upstream panic text still goes to stderr, then the process aborts as
 * upstream.
 */
#include <osmocom/core/backtrace.h>

void osmo_generate_backtrace(void)
{
}

void osmo_log_backtrace(int subsys, int level)
{
    (void)subsys;
    (void)level;
}
