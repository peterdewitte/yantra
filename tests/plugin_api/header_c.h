/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Functions of header_c.c (compiled as C11) for the C++ tests: struct
 * sizes as C sees them, the version rule compiled as C, and the
 * descriptor of a complete test plugin written in C (all three kinds and
 * a UI). The plugin_model tests validate the same descriptor.
 */
#ifndef YANTRA_TEST_PLUGIN_HEADER_C_H
#define YANTRA_TEST_PLUGIN_HEADER_C_H

#include <yantra/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

/** sizeof as seen by C: 0 descriptor, 1 host table, 2 value, 3 record,
 *  4 technology table, 5 UI drawing table, 6 reselection result. */
size_t yantra_test_c_sizeof(int which);
/** yantra_api_compatible() compiled as C. */
int yantra_test_c_compatible(uint32_t host_major, uint32_t host_minor, uint32_t plugin_major,
                             uint32_t plugin_minor);
/** Number of draw calls of the C plugin's panel, and the last button result. */
int yantra_test_c_draw_count(void);
/** Number of yantra_plugin_entry calls (it must have no other side effect). */
int yantra_test_c_entry_count(void);

#ifdef __cplusplus
}
#endif

#endif
