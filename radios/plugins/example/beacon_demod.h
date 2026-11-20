/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Streaming demodulator of the made-up beacon signal (beacon_proto.h): an
 * FM discriminator, a frame search on the preamble end and sync word, bit
 * timing from the strongest symbol energy, and the level, SNR and frequency
 * offset of every frame. Plain C, no global state: one beacon_demod per
 * channel, used from one thread at a time.
 */
#ifndef YANTRA_BEACON_DEMOD_H
#define YANTRA_BEACON_DEMOD_H

#include <stddef.h>
#include <stdint.h>

#include "beacon_proto.h"

/* One frame found in the stream. */
typedef struct beacon_frame_result {
    uint64_t start_sample;  /* stream index of the first preamble sample */
    int crc_ok;             /* 1: payload is valid */
    beacon_payload payload; /* valid when crc_ok */
    double level_dbfs;      /* mean power over the frame, dBFS */
    int has_snr;            /* 1 when snr_db is valid (noise seen before the frame) */
    double snr_db;          /* frame power over the noise power before it */
    double freq_offset_hz;  /* carrier offset measured on the preamble */
} beacon_frame_result;

typedef struct beacon_demod {
    double sample_rate_hz;
    float* disc;   /* phase step per sample (rad) */
    float* power;  /* |x|^2 per sample */
    size_t len;    /* samples held */
    size_t cap;    /* allocated samples */
    uint64_t base; /* stream index of disc[0] */
    uint64_t scan; /* next stream index to test as a frame start */
    float prev_i, prev_q;
    int have_prev;
} beacon_demod;

void beacon_demod_init(beacon_demod* d, double sample_rate_hz);
void beacon_demod_free(beacon_demod* d);

/* Appends `count` complex samples (interleaved I,Q). 0 on success, -1 when
 * out of memory. */
int beacon_demod_push(beacon_demod* d, const float* iq, uint64_t count);

/* Finds the next frame in the samples pushed so far: 1 with `out` filled,
 * 0 when more samples are needed. Call until it returns 0. */
int beacon_demod_next(beacon_demod* d, beacon_frame_result* out);

#endif /* YANTRA_BEACON_DEMOD_H */
