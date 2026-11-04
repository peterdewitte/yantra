/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Streaming demodulator of the made-up beacon signal (see beacon_demod.h).
 */
#include "beacon_demod.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

/* Symbols checked to find a frame: the last 16 preamble bits and the sync
 * word (32 bits, so noise matches about once in 4e9 positions). */
#define CHECK_FIRST 16
#define CHECK_END (BEACON_PREAMBLE_BITS + BEACON_SYNC_BITS)
/* Noise is measured over NOISE_SAMPLES ending GUARD_SAMPLES before a frame. */
#define NOISE_SAMPLES 1200
#define GUARD_SAMPLES 120
#define LOOKBACK (NOISE_SAMPLES + GUARD_SAMPLES)
/* Samples needed from a frame start: the frame plus the timing search. */
#define NEEDED (BEACON_FRAME_SAMPLES + BEACON_SPS)
#define TRIM_THRESHOLD 16384

void beacon_demod_init(beacon_demod* d, double sample_rate_hz)
{
    memset(d, 0, sizeof *d);
    d->sample_rate_hz = sample_rate_hz;
}

void beacon_demod_free(beacon_demod* d)
{
    free(d->disc);
    free(d->power);
    memset(d, 0, sizeof *d);
}

static int reserve(beacon_demod* d, size_t needed)
{
    if (needed <= d->cap) {
        return 0;
    }
    size_t cap = d->cap ? d->cap : 8192;
    while (cap < needed) {
        cap *= 2;
    }
    float* disc = realloc(d->disc, cap * sizeof *disc);
    if (disc == NULL) {
        return -1;
    }
    d->disc = disc;
    float* power = realloc(d->power, cap * sizeof *power);
    if (power == NULL) {
        return -1;
    }
    d->power = power;
    d->cap = cap;
    return 0;
}

int beacon_demod_push(beacon_demod* d, const float* iq, uint64_t count)
{
    if (count == 0) {
        return 0;
    }
    if (reserve(d, d->len + (size_t)count) != 0) {
        return -1;
    }
    for (uint64_t n = 0; n < count; ++n) {
        const float i = iq[2 * n];
        const float q = iq[2 * n + 1];
        float step = 0.0f;
        if (d->have_prev) {
            /* angle of x[n] * conj(x[n-1]) */
            const float re = i * d->prev_i + q * d->prev_q;
            const float im = q * d->prev_i - i * d->prev_q;
            step = atan2f(im, re);
        }
        d->prev_i = i;
        d->prev_q = q;
        d->have_prev = 1;
        const size_t k = d->len++;
        d->disc[k] = step;
        d->power[k] = i * i + q * q;
    }
    return 0;
}

/* Sums are taken directly over each window (no running sums), so the
 * results do not depend on how the stream was cut into blocks. */
static double sum(const float* v, size_t from, size_t to)
{
    double total = 0.0;
    for (size_t k = from; k < to; ++k) {
        total += v[k];
    }
    return total;
}

/* Sum of the phase steps of symbol `k` of a frame starting at index s. */
static double symbol(const beacon_demod* d, size_t s, unsigned k)
{
    return sum(d->disc, s + k * BEACON_SPS, s + (k + 1) * BEACON_SPS);
}

static unsigned expected_bit(unsigned k)
{
    if (k < BEACON_PREAMBLE_BITS) {
        return (k % 2u) == 0u ? 1u : 0u; /* 0xAAAAAAAA, MSB first */
    }
    return (BEACON_SYNC_WORD >> (BEACON_SYNC_BITS - 1u - (k - BEACON_PREAMBLE_BITS))) & 1u;
}

static int matches(const beacon_demod* d, size_t s)
{
    for (unsigned k = CHECK_FIRST; k < CHECK_END; ++k) {
        const double v = symbol(d, s, k);
        if ((v > 0.0) != (expected_bit(k) == 1u)) {
            return 0;
        }
    }
    return 1;
}

/* Timing metric: symbol energy over preamble and sync. */
static double metric(const beacon_demod* d, size_t s)
{
    double m = 0.0;
    for (unsigned k = 0; k < CHECK_END; ++k) {
        m += fabs(symbol(d, s, k));
    }
    return m;
}

static double mean_power(const beacon_demod* d, size_t from, size_t to)
{
    return sum(d->power, from, to) / (double)(to - from);
}

static double to_db(double power)
{
    return power > 1e-20 ? 10.0 * log10(power) : -200.0;
}

static void measure(const beacon_demod* d, size_t s, beacon_frame_result* out)
{
    /* Mean phase step over the balanced preamble = carrier offset. The
     * first two symbols are left out: the step into the first sample comes
     * from the noise before the frame and has a random phase. */
    const size_t from = s + 2 * BEACON_SPS;
    const size_t to = s + BEACON_PREAMBLE_BITS * BEACON_SPS;
    const double mean_step = sum(d->disc, from, to) / (double)(to - from);
    uint8_t bits[BEACON_FRAME_BITS - CHECK_END];
    for (unsigned k = CHECK_END; k < BEACON_FRAME_BITS; ++k) {
        const double v = symbol(d, s, k) - mean_step * BEACON_SPS;
        bits[k - CHECK_END] = v > 0.0 ? 1u : 0u;
    }
    memset(out, 0, sizeof *out);
    out->start_sample = d->base + s;
    out->crc_ok = beacon_decode_bits(bits, &out->payload);
    const double frame_power = mean_power(d, s, s + BEACON_FRAME_SAMPLES);
    out->level_dbfs = to_db(frame_power);
    out->freq_offset_hz = mean_step * d->sample_rate_hz / (2.0 * PI);
    if (s >= LOOKBACK) {
        const double noise = mean_power(d, s - LOOKBACK, s - GUARD_SAMPLES);
        if (noise > 1e-20 && frame_power - noise > 1e-6 * noise) {
            out->has_snr = 1;
            out->snr_db = 10.0 * log10((frame_power - noise) / noise);
        }
    }
}

/* Drops samples no longer needed (keeps LOOKBACK before the scan point). */
static void trim(beacon_demod* d)
{
    const uint64_t keep_from = d->scan > d->base + LOOKBACK ? d->scan - LOOKBACK : d->base;
    const size_t drop = (size_t)(keep_from - d->base);
    if (drop < TRIM_THRESHOLD || drop > d->len) {
        return;
    }
    const size_t rest = d->len - drop;
    memmove(d->disc, d->disc + drop, rest * sizeof *d->disc);
    memmove(d->power, d->power + drop, rest * sizeof *d->power);
    d->len = rest;
    d->base += drop;
}

int beacon_demod_next(beacon_demod* d, beacon_frame_result* out)
{
    if (d->scan < d->base) {
        d->scan = d->base;
    }
    while (d->scan + NEEDED <= d->base + d->len) {
        const size_t s = (size_t)(d->scan - d->base);
        if (!matches(d, s)) {
            ++d->scan;
            continue;
        }
        /* Several neighbouring starts match: take the best timing. */
        size_t best = s;
        double best_metric = metric(d, s);
        for (size_t t = s + 1; t < s + BEACON_SPS; ++t) {
            if (matches(d, t)) {
                const double m = metric(d, t);
                if (m > best_metric) {
                    best_metric = m;
                    best = t;
                }
            }
        }
        measure(d, best, out);
        d->scan = d->base + best + BEACON_FRAME_SAMPLES;
        trim(d);
        return 1;
    }
    trim(d);
    return 0;
}
