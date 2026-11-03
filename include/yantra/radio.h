/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * yantra/radio.h - the radio API, the only interface between Yantra and a
 * radio plugin (RAD-02, RAD-10).
 *
 * PLACEHOLDER: the real API (ABI rules, descriptor, records and schemas,
 * stream control, settings, host callbacks; RAD-10..49) follows in a
 * later step. Until then this header only fixes its name, its C linkage
 * and the name of the single exported plugin entry point (RAD-133).
 */
#ifndef YANTRA_RADIO_H
#define YANTRA_RADIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The only symbol a radio plugin exports (RAD-133). */
#define YANTRA_RADIO_ENTRY_SYMBOL "yantra_radio_entry"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YANTRA_RADIO_H */
