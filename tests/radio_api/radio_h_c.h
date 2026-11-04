/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * C side of the radio API header test (radio_h_c.c, compiled as C11).
 */
#ifndef YANTRA_TEST_RADIO_H_C_H
#define YANTRA_TEST_RADIO_H_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* YR_API_VERSION_MAJOR and _MINOR as the C compiler sees them. */
uint32_t yantra_test_radio_c_major(void);
uint32_t yantra_test_radio_c_minor(void);
/* yr_api_compatible() compiled as C. */
int yantra_test_radio_c_compatible(uint32_t host_major, uint32_t host_minor,
                                   uint32_t plugin_major, uint32_t plugin_minor);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YANTRA_TEST_RADIO_H_C_H */
