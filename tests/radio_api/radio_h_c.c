/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compiles the radio API header in radios/
 * (yantra/radio.h, PLG-26) as C11, with Yantra's warning flags.
 */
#include <yantra/radio.h>

#include "radio_h_c.h"

uint32_t yantra_test_radio_c_major(void)
{
    return yr_api_version_compiled().major;
}

uint32_t yantra_test_radio_c_minor(void)
{
    return yr_api_version_compiled().minor;
}

int yantra_test_radio_c_compatible(uint32_t host_major, uint32_t host_minor,
                                   uint32_t plugin_major, uint32_t plugin_minor)
{
    yr_api_version host;
    yr_api_version plugin;
    host.major = host_major;
    host.minor = host_minor;
    plugin.major = plugin_major;
    plugin.minor = plugin_minor;
    return yr_api_compatible(host, plugin);
}

#ifdef __cplusplus
#error "radio_h_c.c must be compiled as C"
#endif
