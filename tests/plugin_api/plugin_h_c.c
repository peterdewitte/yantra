/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compiles include/yantra/plugin.h as C11 (plugins may be written in C) and
 * implements a plugin-style entry point in C for the C++ test to call.
 */
#include "yantra/plugin.h"

#include "plugin_h_c.h"

yantra_plugin_abi_version yantra_test_c_entry(void)
{
    yantra_plugin_abi_version v;
    v.major = YANTRA_PLUGIN_ABI_VERSION_MAJOR;
    v.minor = YANTRA_PLUGIN_ABI_VERSION_MINOR;
    return v;
}

int yantra_test_c_compatible(uint32_t host_major, uint32_t host_minor,
                             uint32_t plugin_major, uint32_t plugin_minor)
{
    yantra_plugin_abi_version host;
    yantra_plugin_abi_version plugin;
    host.major = host_major;
    host.minor = host_minor;
    plugin.major = plugin_major;
    plugin.minor = plugin_minor;
    return yantra_plugin_abi_compatible(host, plugin);
}

#ifdef __cplusplus
#error "plugin_h_c.c must be compiled as C"
#endif
