/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sample generator of the beacon example radio plugin (RAD-64): one beacon
 * on one channel, with level, noise, carrier offset and optional corrupted
 * frames, fully deterministic for a given configuration (seeded noise), so
 * its output can serve as golden test vectors.
 *
 * Configuration text: `key=value` items separated by spaces, `;` or new
 * lines; `#` starts a comment. Keys and defaults:
 *   network=1  beacon=1  class=indoor|outdoor|test  power=30 (dBm, -128..127)
 *   neighbours=  (up to 3 ids, comma separated)  first_sequence=0
 *   level_dbfs=-20  noise_dbfs=-60 (-200 = no noise)  cfo_hz=0
 *   seconds=1  period_ms=250  offset_ms=20  seed=1
 *   corrupt_every=0 (N > 0: every Nth frame gets a bit error)
 */
#ifndef YANTRA_BEACON_GEN_H
#define YANTRA_BEACON_GEN_H

#include <stddef.h>
#include <stdint.h>

#include "beacon_proto.h"

typedef struct beacon_gen_config {
    beacon_payload payload; /* sequence = first sequence */
    double level_dbfs;
    double noise_dbfs;
    double cfo_hz;
    double seconds;
    double period_ms;
    double offset_ms;
    uint64_t seed;
    unsigned corrupt_every;
} beacon_gen_config;

/* Fills `cfg` from the text (NULL or "" = defaults). 0 on success, else -1
 * with a message in `error`. */
int beacon_gen_parse(const char* text, beacon_gen_config* cfg, char* error, size_t error_size);

typedef struct beacon_gen beacon_gen;

/* NULL when the configuration does not fit the sample rate (message in
 * `error`) or out of memory. */
beacon_gen* beacon_gen_create(const beacon_gen_config* cfg, double sample_rate_hz, char* error,
                              size_t error_size);
/* Writes up to max_count complex samples; returns the number written,
 * 0 at the end. */
uint64_t beacon_gen_read(beacon_gen* gen, float* out_iq, uint64_t max_count);
void beacon_gen_destroy(beacon_gen* gen);

#endif /* YANTRA_BEACON_GEN_H */
