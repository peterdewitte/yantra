/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sink for osmo-tetra's printf/fprintf output (see osmo_quiet.h). Set the
 * environment variable YANTRA_OSMO_VERBOSE=1 to see it on stderr.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "osmo_quiet.h"

static int verbose(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("YANTRA_OSMO_VERBOSE");
        v = e && *e == '1';
    }
    return v;
}

int yantra_osmo_printf(const char* fmt, ...)
{
    if (!verbose())
        return 0;
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return r;
}

int yantra_osmo_fprintf(FILE* f, const char* fmt, ...)
{
    (void)f;
    if (!verbose())
        return 0;
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return r;
}
