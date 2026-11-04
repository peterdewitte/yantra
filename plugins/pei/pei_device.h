/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Internal state of the TETRA terminal plugin (RAD-80): plugin, device,
 * settings and the terminal session (pei_session.c). Plain C11 with POSIX
 * threads.
 *
 * Threads (RAD-15): control calls come from Yantra's control thread; the
 * stream thread of a device talks to the terminal (pei_session.c); the
 * plugin's watch thread lists the ports every 250 ms for devices_changed
 * and marks an open device LOST when its port disappears (RAD-35). Lock
 * order: plugin->mutex before device->mutex. Records and events are
 * pushed while holding device->mutex after checking the state, so no
 * push happens after stop_stream or after DEVICE_LOST (RAD-33).
 */
#ifndef YANTRA_PEI_DEVICE_H
#define YANTRA_PEI_DEVICE_H

#include "pei_at.h"
#include "pei_port.h"

#include <yantra/radio.h>

#include <pthread.h>
#include <stdatomic.h>

#define PEI_TEXT_BYTES 256u
#define PEI_PATH_BYTES 256u

/* Command profiles (setting command_profile). */
#define PEI_PROFILE_CELL_LIST 0
#define PEI_PROFILE_ETSI 1

/* Terminal states (sensor terminal_state). */
#define PEI_TERMINAL_CLOSED 0
#define PEI_TERMINAL_CONNECTING 1
#define PEI_TERMINAL_NO_REPLY 2
#define PEI_TERMINAL_READY 3
#define PEI_TERMINAL_LOST 4

/* Record schema 0 (pei_plugin.c). */
#define PEI_FIELD_INDEX_C1 8u
#define PEI_FIELD_INDEX_REGISTRATION 9u
#define PEI_FIELD_INDEX_CSQ 10u
#define PEI_FIELD_INDEX_STATE 11u
#define PEI_SCHEMA_FIELD_COUNT 12u

typedef struct pei_settings {
    char port[PEI_PATH_BYTES];
    int baud;
    int profile;
    char serving_command[PEI_TEXT_BYTES];
    char serving_fields[PEI_TEXT_BYTES];
    char neighbour_command[PEI_TEXT_BYTES];
    char neighbour_fields[PEI_TEXT_BYTES];
    int creg_with_mode;
    uint32_t reply_timeout_ms;
    uint32_t band;
    uint32_t offset;
    int log_commands;
} pei_settings;

/* What the sensors show (TRM-04 radio info and the last readings). */
typedef struct pei_status {
    int terminal;
    char manufacturer[PEI_TEXT_BYTES];
    char model[PEI_TEXT_BYTES];
    char firmware[PEI_TEXT_BYTES];
    char tei[PEI_TEXT_BYTES];
    int csq_code;   /* 99 unknown */
    int registration; /* 0..5, 4 unknown */
    char la[16];    /* "" unknown */
    char serving[PEI_TEXT_BYTES];
    uint64_t polls;
    uint64_t timeouts;
    uint64_t neighbours;
    char unsupported[PEI_TEXT_BYTES];
    char problem[PEI_TEXT_BYTES];
} pei_status;

struct yr_device {
    struct yr_plugin* plugin;
    struct yr_device* next; /* plugin's list of open devices */
    char key[PEI_PATH_BYTES + 8];
    char path[PEI_PATH_BYTES]; /* the port of the key */
    char watch_port[PEI_PATH_BYTES]; /* the port in use: the stream's port, else the key's */
    char label[PEI_PATH_BYTES + 40];
    char hardware_info[PEI_PATH_BYTES * 2 + 32];

    pthread_mutex_t mutex;
    pthread_cond_t wake; /* stop requests for the stream thread */
    uint32_t state;      /* YR_DEVICE_* */
    pei_settings settings;
    pei_status status;
    yr_plan plan;
    yr_stream* sink;
    int back_sent; /* DEVICE_BACK sent for the current loss */
    char last_error[512];

    pthread_t thread;
    int thread_running;
    atomic_int stop;
    yr_stream_stats stats;
};

struct yr_plugin {
    const yr_host_api* host;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    struct yr_device* devices;
    char last_error[512];
    pthread_t watcher;
    int watcher_running;
    int stopping;
    uint64_t ports_signature;
};

/* pei_plugin.c */
void pei_log(struct yr_device* d, uint32_t level, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
int64_t pei_monotonic_ns(struct yr_device* d);
/** snprintf that cuts long texts silently (the sensors and messages have
 *  fixed buffers). */
void pei_format(char* out, size_t size, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/* pei_session.c */
/** Opens the port at start_stream (so a wrong port fails the start) and
 *  hands it to the stream thread. */
yr_status pei_session_start(struct yr_device* d, yr_stream* sink);
/** Asks the stream thread to stop and joins it (device->mutex not held). */
void pei_session_join(struct yr_device* d);
/** The self-test (RAD-46) on its own short session. */
yr_status pei_session_self_test(struct yr_device* d, yr_selftest_cb cb, void* user);

/** Waits on d->wake up to `ms` or until d->stop is set; device->mutex held. */
void pei_wait_locked(struct yr_device* d, int ms);

#endif /* YANTRA_PEI_DEVICE_H */
