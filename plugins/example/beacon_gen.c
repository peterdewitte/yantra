/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sample generator of the beacon example radio plugin (see beacon_gen.h).
 */
#include "beacon_gen.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

struct beacon_gen {
    beacon_gen_config cfg;
    double sample_rate_hz;
    uint64_t total;      /* samples to produce */
    uint64_t n;          /* next sample index */
    uint64_t offset;     /* first frame start */
    uint64_t period;     /* frame period in samples */
    double amplitude;    /* signal amplitude (full scale 1.0) */
    double noise_sigma;  /* per I/Q component */
    double fsk_phase;    /* phase of the FSK modulation */
    double cfo_phase;    /* phase of the carrier offset */
    uint64_t rng;        /* xorshift64* state */
    uint64_t bits_frame; /* frame index held in bits, UINT64_MAX none */
    uint8_t bits[BEACON_FRAME_BITS];
};

static void set_error(char* error, size_t size, const char* text, const char* item)
{
    if (error != NULL && size > 0) {
        snprintf(error, size, "%s%s%s", text, item ? ": " : "", item ? item : "");
    }
}

static int parse_number(const char* value, double minimum, double maximum, double* out)
{
    char* end = NULL;
    const double v = strtod(value, &end);
    if (end == value || *end != '\0' || !isfinite(v) || v < minimum || v > maximum) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_item(const char* key, const char* value, beacon_gen_config* cfg)
{
    double v = 0.0;
    if (strcmp(key, "class") == 0) {
        if (strcmp(value, "indoor") == 0) {
            cfg->payload.beacon_class = BEACON_CLASS_INDOOR;
        } else if (strcmp(value, "outdoor") == 0) {
            cfg->payload.beacon_class = BEACON_CLASS_OUTDOOR;
        } else if (strcmp(value, "test") == 0) {
            cfg->payload.beacon_class = BEACON_CLASS_TEST;
        } else {
            return -1;
        }
        return 0;
    }
    if (strcmp(key, "neighbours") == 0) {
        cfg->payload.neighbour_count = 0;
        const char* p = value;
        while (*p != '\0') {
            char* end = NULL;
            const unsigned long id = strtoul(p, &end, 10);
            if (end == p || id > 0xFFFFul || cfg->payload.neighbour_count >= BEACON_MAX_NEIGHBOURS) {
                return -1;
            }
            cfg->payload.neighbours[cfg->payload.neighbour_count++] = (uint16_t)id;
            p = end;
            if (*p == ',') {
                ++p;
            } else if (*p != '\0') {
                return -1;
            }
        }
        return 0;
    }
    static const struct {
        const char* key;
        double minimum, maximum;
    } ranges[] = {
        {"network", 0, 255},          {"beacon", 0, 65535},       {"power", -128, 127},
        {"first_sequence", 0, 255},   {"level_dbfs", -200, 0},    {"noise_dbfs", -200, 0},
        {"cfo_hz", -1000, 1000},      {"seconds", 0, 3600},       {"period_ms", 1, 60000},
        {"offset_ms", 0, 60000},      {"seed", 0, 4294967295.0},  {"corrupt_every", 0, 1000000},
    };
    for (size_t i = 0; i < sizeof ranges / sizeof ranges[0]; ++i) {
        if (strcmp(key, ranges[i].key) != 0) {
            continue;
        }
        if (parse_number(value, ranges[i].minimum, ranges[i].maximum, &v) != 0) {
            return -1;
        }
        switch (i) {
        case 0: cfg->payload.network_code = (uint8_t)v; break;
        case 1: cfg->payload.beacon_id = (uint16_t)v; break;
        case 2: cfg->payload.tx_power_dbm = (int8_t)v; break;
        case 3: cfg->payload.sequence = (uint8_t)v; break;
        case 4: cfg->level_dbfs = v; break;
        case 5: cfg->noise_dbfs = v; break;
        case 6: cfg->cfo_hz = v; break;
        case 7: cfg->seconds = v; break;
        case 8: cfg->period_ms = v; break;
        case 9: cfg->offset_ms = v; break;
        case 10: cfg->seed = (uint64_t)v; break;
        default: cfg->corrupt_every = (unsigned)v; break;
        }
        return 0;
    }
    return -1;
}

int beacon_gen_parse(const char* text, beacon_gen_config* cfg, char* error, size_t error_size)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->payload.version = BEACON_VERSION;
    cfg->payload.beacon_class = BEACON_CLASS_INDOOR;
    cfg->payload.network_code = 1;
    cfg->payload.beacon_id = 1;
    cfg->payload.tx_power_dbm = 30;
    cfg->level_dbfs = -20.0;
    cfg->noise_dbfs = -60.0;
    cfg->seconds = 1.0;
    cfg->period_ms = 250.0;
    cfg->offset_ms = 20.0;
    cfg->seed = 1;
    if (text == NULL) {
        return 0;
    }
    const char* p = text;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ';') {
            ++p;
        }
        if (*p == '#') {
            while (*p != '\0' && *p != '\n') {
                ++p;
            }
            continue;
        }
        if (*p == '\0') {
            break;
        }
        char item[128];
        size_t len = 0;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != ';' &&
               *p != '#') {
            if (len + 1 >= sizeof item) {
                set_error(error, error_size, "configuration item too long", NULL);
                return -1;
            }
            item[len++] = *p++;
        }
        item[len] = '\0';
        char* eq = strchr(item, '=');
        if (eq == NULL) {
            set_error(error, error_size, "expected key=value", item);
            return -1;
        }
        *eq = '\0';
        if (parse_item(item, eq + 1, cfg) != 0) {
            *eq = '=';
            set_error(error, error_size, "unknown key or invalid value", item);
            return -1;
        }
    }
    return 0;
}

beacon_gen* beacon_gen_create(const beacon_gen_config* cfg, double sample_rate_hz, char* error,
                              size_t error_size)
{
    if (sample_rate_hz != BEACON_SAMPLE_RATE_HZ) {
        set_error(error, error_size, "the beacon generator runs at 24000 Hz only", NULL);
        return NULL;
    }
    const double period = floor(cfg->period_ms * sample_rate_hz / 1000.0 + 0.5);
    if (period < BEACON_FRAME_SAMPLES) {
        set_error(error, error_size, "period_ms is shorter than a frame (147 ms)", NULL);
        return NULL;
    }
    beacon_gen* g = calloc(1, sizeof *g);
    if (g == NULL) {
        set_error(error, error_size, "out of memory", NULL);
        return NULL;
    }
    g->cfg = *cfg;
    g->sample_rate_hz = sample_rate_hz;
    g->total = (uint64_t)floor(cfg->seconds * sample_rate_hz + 0.5);
    g->offset = (uint64_t)floor(cfg->offset_ms * sample_rate_hz / 1000.0 + 0.5);
    g->period = (uint64_t)period;
    g->amplitude = pow(10.0, cfg->level_dbfs / 20.0);
    g->noise_sigma = cfg->noise_dbfs <= -200.0 ? 0.0 : sqrt(pow(10.0, cfg->noise_dbfs / 10.0) / 2.0);
    g->rng = cfg->seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull;
    g->bits_frame = UINT64_MAX;
    return g;
}

/* Uniform in (0, 1]. */
static double uniform(beacon_gen* g)
{
    g->rng ^= g->rng >> 12;
    g->rng ^= g->rng << 25;
    g->rng ^= g->rng >> 27;
    const uint64_t r = g->rng * 0x2545F4914F6CDD1Dull;
    return ((double)(r >> 11) + 1.0) / 9007199254740992.0;
}

static void prepare_frame(beacon_gen* g, uint64_t frame)
{
    beacon_payload p = g->cfg.payload;
    p.sequence = (uint8_t)((g->cfg.payload.sequence + frame) & 0xFFu);
    beacon_encode_frame(&p, g->bits);
    if (g->cfg.corrupt_every > 0 && (frame + 1) % g->cfg.corrupt_every == 0) {
        /* one bit error in the payload (the CRC then fails) */
        const unsigned k = BEACON_PREAMBLE_BITS + BEACON_SYNC_BITS + 21;
        g->bits[k] ^= 1u;
    }
    g->bits_frame = frame;
}

uint64_t beacon_gen_read(beacon_gen* g, float* out_iq, uint64_t max_count)
{
    const double fsk_step = 2.0 * PI * BEACON_DEVIATION_HZ / g->sample_rate_hz;
    const double cfo_step = 2.0 * PI * g->cfg.cfo_hz / g->sample_rate_hz;
    uint64_t written = 0;
    while (written < max_count && g->n < g->total) {
        double i = 0.0;
        double q = 0.0;
        if (g->n >= g->offset) {
            const uint64_t frame = (g->n - g->offset) / g->period;
            const uint64_t pos = (g->n - g->offset) % g->period;
            if (pos < BEACON_FRAME_SAMPLES) {
                if (frame != g->bits_frame) {
                    prepare_frame(g, frame);
                }
                g->fsk_phase += g->bits[pos / BEACON_SPS] ? fsk_step : -fsk_step;
                g->fsk_phase = fmod(g->fsk_phase, 2.0 * PI);
                const double phase = g->fsk_phase + g->cfo_phase;
                i = g->amplitude * cos(phase);
                q = g->amplitude * sin(phase);
            }
        }
        g->cfo_phase = fmod(g->cfo_phase + cfo_step, 2.0 * PI);
        if (g->noise_sigma > 0.0) {
            const double r = g->noise_sigma * sqrt(-2.0 * log(uniform(g)));
            const double a = 2.0 * PI * uniform(g);
            i += r * cos(a);
            q += r * sin(a);
        }
        out_iq[2 * written] = (float)i;
        out_iq[2 * written + 1] = (float)q;
        ++written;
        ++g->n;
    }
    return written;
}

void beacon_gen_destroy(beacon_gen* gen)
{
    free(gen);
}
