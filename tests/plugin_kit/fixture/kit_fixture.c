/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Broken variants of the example technology plugin for the negative cases
 * of the plugin test kit (PLG-13). The example is compiled in unchanged;
 * this file replaces its entry point by one that hands out a copy of its
 * descriptor with exactly one rule broken, chosen by compile definition:
 *   KIT_BROKEN_LICENCE   a licence that is not GPL-3.0-compatible (PLG-10)
 *   KIT_BROKEN_UI        a button without a tooltip (DOC-07)
 *   KIT_BROKEN_IMPURE    a C1 rule that keeps state (PLG-07 purity)
 *   KIT_BROKEN_SLOW      a C1 rule that blocks for 300 ms once (PLG-07)
 *   KIT_BROKEN_SETTINGS  a C1 rule that writes settings on the
 *                        measurement thread (PLG-07)
 */
#define _POSIX_C_SOURCE 200809L /* nanosleep */
#include <time.h>

#include <yantra/plugin.h>

/* The example's entry point, renamed. */
const yantra_plugin_descriptor* example_beacon_entry(const yantra_host_api* host);
#define yantra_plugin_entry example_beacon_entry
#include "beacon_technology.c"
#undef yantra_plugin_entry

static yantra_plugin_descriptor broken;
static yantra_technology_api broken_technology;
static yantra_plugin_ui_api broken_ui;

#if defined(KIT_BROKEN_IMPURE) || defined(KIT_BROKEN_SLOW) || defined(KIT_BROKEN_SETTINGS)
static yantra_status broken_c1(yantra_plugin_instance* instance, const yantra_c1_input* input,
                               double* out_c1_db)
{
    const yantra_status status = compute_c1(instance, input, out_c1_db);
#if defined(KIT_BROKEN_IMPURE)
    static double drift; /* state: every call answers a little more */
    drift += 0.5;
    if (status == YANTRA_OK)
        *out_c1_db += drift;
#elif defined(KIT_BROKEN_SLOW)
    static int slept;
    if (!slept) {
        struct timespec t = {0, 300000000L};
        slept = 1;
        nanosleep(&t, NULL);
    }
#else
    instance->host->settings_set(instance->host->host, "last_c1", "x");
#endif
    return status;
}
#endif

#if defined(KIT_BROKEN_UI)
static void broken_draw(yantra_plugin_instance* instance, const char* window_id,
                        const yantra_ui_api* ui)
{
    static const yantra_ui_control no_tooltip = {
        sizeof(yantra_ui_control), 0, "refresh", "Refresh", NULL, NULL, NULL, NULL};
    draw(instance, window_id, ui);
    ui->button(ui->ui, &no_tooltip);
}
#endif

YANTRA_PLUGIN_EXPORT const yantra_plugin_descriptor* yantra_plugin_entry(
    const yantra_host_api* host)
{
    const yantra_plugin_descriptor* d = example_beacon_entry(host);
    if (d == NULL)
        return NULL;
    broken = *d;
    broken_technology = *d->technology;
    broken_ui = *d->ui;
    broken.technology = &broken_technology;
    broken.ui = &broken_ui;
#if defined(KIT_BROKEN_LICENCE)
    broken.licence = "LicenseRef-Proprietary";
#elif defined(KIT_BROKEN_UI)
    broken_ui.draw = broken_draw;
#else
    broken_technology.compute_c1 = broken_c1;
#endif
    return &broken;
}
