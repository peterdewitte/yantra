/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Functions of plugin_h_c.c, compiled as C, used by the C++ test.
 */
#ifndef YANTRA_TEST_PLUGIN_H_C_H
#define YANTRA_TEST_PLUGIN_H_C_H

#include "yantra/plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

yantra_plugin_abi_version yantra_test_c_entry(void);
int yantra_test_c_compatible(uint32_t host_major, uint32_t host_minor,
                             uint32_t plugin_major, uint32_t plugin_minor);

#ifdef __cplusplus
}
#endif

#endif
