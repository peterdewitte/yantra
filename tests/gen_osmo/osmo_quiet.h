/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Forced-include header for the osmo-tetra sources (RAD-101). osmo-tetra
 * writes progress text to stdout/stderr; in the tests that output goes to
 * a quiet sink instead. This avoids patching the pinned upstream sources.
 */
#ifndef YANTRA_OSMO_QUIET_H
#define YANTRA_OSMO_QUIET_H

#include <stdio.h>

int yantra_osmo_printf(const char* fmt, ...);
int yantra_osmo_fprintf(FILE* f, const char* fmt, ...);

#define printf(...) yantra_osmo_printf(__VA_ARGS__)
#define fprintf(...) yantra_osmo_fprintf(__VA_ARGS__)

#endif
