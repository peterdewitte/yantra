/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The TETRA terminal radio plugin "pei" (RAD-80, TRM-01, TRM-04), written
 * in plain C11 against include/yantra/radio.h only: a TETRA terminal in
 * TMO, read over its Peripheral Equipment Interface (AT commands,
 * EN 300 392-5) on a serial port. It delivers core records in dBm
 * (SELF_CALIBRATED) for the serving cell and the neighbour cells the
 * terminal reports, with C1 when the terminal gives it, and the radio
 * info (model, serial/TEI, firmware) as sensors. Only reading commands
 * are sent (RAD-17): no call set-up, SDS or addresses. The AT command log
 * goes to the device log at debug level (RAD-14).
 *
 * This file: descriptor, schema, enumerate/open/close, plans, settings
 * and sensors, the port watcher (RAD-35). The terminal session (stream
 * thread, polling, records, self-test) is pei_session.c.
 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "pei_device.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef YANTRA_RADIOS_SOURCE_URL
#define YANTRA_RADIOS_SOURCE_URL "https://example.invalid/yantra-radios"
#endif

#define PEI_KEY_PREFIX "pei:"
#define PEI_DEFAULT_BAUD 38400
#define PEI_PORTS_ENV "YANTRA_PEI_PORTS"

/* Defaults of the vendor cell-list commands: the documented example
 * format that the simulated terminal (tools/yantra-pei-sim) speaks. A real
 * terminal needs the commands of its PEI manual (docs/RADIO_PLUGINS.md). */
#define PEI_DEFAULT_SERVING_COMMAND "AT+YCELL?"
#define PEI_DEFAULT_SERVING_FIELDS "carrier,band,offset,mcc,mnc,la,cc,rssi,c1,state"
#define PEI_DEFAULT_NEIGHBOUR_COMMAND "AT+YNCELL?"
#define PEI_DEFAULT_NEIGHBOUR_FIELDS "carrier,band,offset,mcc,mnc,la,cc,rssi,c1"

/* ------------------------------------------------------------ helpers */

void pei_format(char* out, size_t size, const char* fmt, ...)
{
    if (out == NULL || size == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out, size, fmt, ap);
    va_end(ap);
}

static void copy_text(char* out, size_t size, const char* text)
{
    pei_format(out, size, "%s", text != NULL ? text : "");
}

static void set_plugin_error(yr_plugin* p, const char* text)
{
    pthread_mutex_lock(&p->mutex);
    copy_text(p->last_error, sizeof p->last_error, text);
    pthread_mutex_unlock(&p->mutex);
}

/* device->mutex held */
static void set_error_locked(yr_device* d, const char* text)
{
    copy_text(d->last_error, sizeof d->last_error, text);
}

void pei_log(yr_device* d, uint32_t level, const char* fmt, ...)
{
    char line[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    const yr_host_api* h = d->plugin->host;
    if (h->log != NULL)
        h->log(h->host, d, level, line);
}

int64_t pei_monotonic_ns(yr_device* d)
{
    return d->plugin->host->monotonic_ns(d->plugin->host->host);
}

static void init_cond_monotonic(pthread_cond_t* c)
{
    pthread_condattr_t a;
    pthread_condattr_init(&a);
#if !defined(_WIN32)
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
#endif
    pthread_cond_init(c, &a);
    pthread_condattr_destroy(&a);
}

static void timed_wait(pthread_cond_t* c, pthread_mutex_t* m, int ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(c, m, &ts);
}

void pei_wait_locked(yr_device* d, int ms)
{
    if (atomic_load(&d->stop))
        return;
    timed_wait(&d->wake, &d->mutex, ms);
}

/* ------------------------------------------------------------ schema */

static const yr_enum_item kRoleItems[] = {
    {YR_ROLE_SERVING, "serving", "Serving"},
    {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
    {YR_ROLE_OTHER, "other", "Other"},
};

static const yr_enum_item kRegistrationItems[] = {
    {0, "not_registered", "Not registered"},
    {1, "registered", "Registered"},
    {2, "searching", "Searching"},
    {3, "denied", "Registration denied"},
    {4, "unknown", "Unknown"},
    {5, "roaming", "Registered (roaming)"},
};

#define KEYDEF (YR_FIELD_KEY | YR_FIELD_DEFAULT)
#define FIELD(name, type, group, flags, label, unit, help, items, count) \
    {sizeof(yr_field_def), type, group, flags, name, label, unit, help, items, count, 0}

static const yr_field_def kFields[PEI_SCHEMA_FIELD_COUNT] = {
    FIELD(YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE, KEYDEF, "Frequency", "Hz",
          "Downlink frequency of the cell, from the carrier number (and band and offset) the terminal "
          "reports.",
          NULL, 0),
    FIELD(YR_FIELD_CHANNEL, YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_DEFAULT, "Carrier number", "",
          "Carrier number of the cell in its band (EN 300 392-2 21.4.4.1) as the terminal reports it.", NULL, 0),
    FIELD(YR_FIELD_ROLE, YR_TYPE_ENUM, YR_GROUP_CORE, YR_FIELD_DEFAULT, "Role", "",
          "Serving: the cell the terminal is camped on; neighbour: a neighbour cell the terminal monitors.",
          kRoleItems, 3),
    FIELD(YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE, YR_FIELD_DEFAULT, "Level", "dBm",
          "Received level as the terminal reports it (its RSSI; for the serving cell AT+CSQ when the "
          "cell command gives none). Terminals report in 1 or 2 dB steps.",
          NULL, 0),
    FIELD(YR_FIELD_MCC, YR_TYPE_UINT, YR_GROUP_CORE, KEYDEF, "MCC", "",
          "Mobile country code as the terminal reports it.", NULL, 0),
    FIELD(YR_FIELD_MNC, YR_TYPE_UINT, YR_GROUP_CORE, KEYDEF, "MNC", "",
          "Mobile network code as the terminal reports it.", NULL, 0),
    FIELD(YR_FIELD_LA, YR_TYPE_UINT, YR_GROUP_CORE, KEYDEF, "Location area", "",
          "Location area as the terminal reports it (cell command, else AT+CREG).", NULL, 0),
    FIELD(YR_FIELD_COLOUR_CODE, YR_TYPE_UINT, YR_GROUP_CORE, KEYDEF, "Colour code", "",
          "Colour code as the terminal reports it.", NULL, 0),
    FIELD(YR_FIELD_C1_DB, YR_TYPE_DOUBLE, YR_GROUP_C1, YR_FIELD_DEFAULT, "C1", "dB",
          "Path loss criterion C1 (EN 300 392-2 clause 23.3) as calculated by the terminal.", NULL, 0),
    FIELD("registration", YR_TYPE_ENUM, YR_GROUP_OTHER, 0, "Registration", "",
          "Registration state of the terminal (AT+CREG?, EN 300 392-5); serving cell only.",
          kRegistrationItems, 6),
    FIELD("csq_code", YR_TYPE_UINT, YR_GROUP_OTHER, 0, "AT+CSQ level code", "",
          "Signal quality code of AT+CSQ: 0..31 = -113..-51 dBm in 2 dB steps; serving cell only.", NULL, 0),
    FIELD("cell_state", YR_TYPE_STRING, YR_GROUP_OTHER, 0, "Cell state", "",
          "Serving-cell state text as the terminal reports it (vendor command).", NULL, 0),
};

static const yr_field_def* const kFieldPtrs[PEI_SCHEMA_FIELD_COUNT] = {
    &kFields[0], &kFields[1], &kFields[2], &kFields[3], &kFields[4],  &kFields[5],
    &kFields[6], &kFields[7], &kFields[8], &kFields[9], &kFields[10], &kFields[11],
};

static const yr_schema kCellSchema = {
    sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "tetra_terminal_cell", "TETRA cell seen by a terminal", 1,
    PEI_SCHEMA_FIELD_COUNT, kFieldPtrs, YR_GROUP_C1 | YR_GROUP_OTHER, 0};

static const yr_schema* const kSchemas[] = {&kCellSchema};

/* ------------------------------------------------------------ plugin */

static uint64_t ports_signature(void);

static void* watcher_main(void* arg);

static yr_status pei_init(const yr_host_api* host, yr_plugin** out)
{
    if (host == NULL || out == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    if (host->api_version.major != YR_API_VERSION_MAJOR)
        return YR_ERR_VERSION;
    yr_plugin* p = (yr_plugin*)calloc(1, sizeof *p);
    if (p == NULL)
        return YR_ERR_NO_MEMORY;
    p->host = host;
    pthread_mutex_init(&p->mutex, NULL);
    init_cond_monotonic(&p->wake);
    p->ports_signature = ports_signature();
    if (pthread_create(&p->watcher, NULL, watcher_main, p) != 0) {
        pthread_cond_destroy(&p->wake);
        pthread_mutex_destroy(&p->mutex);
        free(p);
        return YR_ERR_FAILED;
    }
    p->watcher_running = 1;
    *out = p;
    return YR_OK;
}

static void pei_shutdown(yr_plugin* p)
{
    if (p == NULL)
        return;
    pthread_mutex_lock(&p->mutex);
    p->stopping = 1;
    pthread_cond_broadcast(&p->wake);
    pthread_mutex_unlock(&p->mutex);
    if (p->watcher_running)
        pthread_join(p->watcher, NULL);
    pthread_cond_destroy(&p->wake);
    pthread_mutex_destroy(&p->mutex);
    free(p);
}

/* FNV-1a over the listed ports: a change means devices appeared or went. */
static void hash_port(void* user, const char* path)
{
    uint64_t* h = (uint64_t*)user;
    for (const char* c = path; *c != '\0'; ++c) {
        *h ^= (unsigned char)*c;
        *h *= 1099511628211ull;
    }
    *h ^= 0xff;
    *h *= 1099511628211ull;
}

static uint64_t ports_signature(void)
{
    uint64_t h = 1469598103934665603ull;
    pei_port_list(getenv(PEI_PORTS_ENV), hash_port, &h);
    return h;
}

/* The watch thread (RAD-35): devices_changed when the port list changes;
 * an open device whose port disappeared becomes LOST (DEVICE_LOST to its
 * stream), and DEVICE_BACK goes to the kept sink when the port returns. */
static void* watcher_main(void* arg)
{
    yr_plugin* p = (yr_plugin*)arg;
    pthread_mutex_lock(&p->mutex);
    while (!p->stopping) {
        timed_wait(&p->wake, &p->mutex, 250);
        if (p->stopping)
            break;
        pthread_mutex_unlock(&p->mutex);
        const uint64_t sig = ports_signature();
        pthread_mutex_lock(&p->mutex);
        const int changed = sig != p->ports_signature;
        p->ports_signature = sig;
        for (yr_device* d = p->devices; d != NULL; d = d->next) {
            pthread_mutex_lock(&d->mutex);
            char why[8];
            const int present = pei_port_check(d->watch_port, why, sizeof why) != 0;
            if (!present && d->state != YR_DEVICE_LOST && d->state != YR_DEVICE_CLOSED) {
                const int streaming = d->state == YR_DEVICE_STREAMING;
                d->state = YR_DEVICE_LOST;
                d->back_sent = 0;
                d->status.terminal = PEI_TERMINAL_LOST;
                atomic_store(&d->stop, 1);
                pthread_cond_broadcast(&d->wake);
                set_error_locked(d, "the terminal's port disappeared (unplugged); close and open the device "
                                    "again when it is back");
                if (streaming && d->sink != NULL)
                    p->host->stream_event(d->sink, YR_EVENT_DEVICE_LOST, 0);
                pei_log(d, YR_LOG_WARNING, "port %s disappeared: device lost", d->watch_port);
            } else if (present && d->state == YR_DEVICE_LOST && !d->back_sent) {
                d->back_sent = 1;
                if (d->sink != NULL)
                    p->host->stream_event(d->sink, YR_EVENT_DEVICE_BACK, 0);
                pei_log(d, YR_LOG_INFO, "port %s is back", d->watch_port);
            }
            pthread_mutex_unlock(&d->mutex);
        }
        if (changed) {
            pthread_mutex_unlock(&p->mutex);
            p->host->devices_changed(p->host->host);
            pthread_mutex_lock(&p->mutex);
        }
    }
    pthread_mutex_unlock(&p->mutex);
    return NULL;
}

/* ------------------------------------------------------------ devices */

typedef struct device_entry {
    char key[PEI_PATH_BYTES + 8];
    char label[PEI_PATH_BYTES + 40];
    char hardware_info[PEI_PATH_BYTES * 2 + 32];
    char reason[PEI_TEXT_BYTES];
    uint32_t flags;
} device_entry;

static void make_entry(const char* path, device_entry* e)
{
    memset(e, 0, sizeof *e);
    pei_format(e->key, sizeof e->key, PEI_KEY_PREFIX "%s", path);
    pei_format(e->label, sizeof e->label, "TETRA terminal (PEI) on %s", path);
    char real[PEI_PATH_BYTES] = "";
#if !defined(_WIN32)
    char buf[4096];
    if (realpath(path, buf) != NULL)
        copy_text(real, sizeof real, buf);
#endif
    pei_format(e->hardware_info, sizeof e->hardware_info, "port=%s\ndevice=%s", path, real[0] ? real : path);
    if (pei_port_check(path, e->reason, sizeof e->reason) < 0)
        e->flags |= YR_DEVICE_NO_PERMISSION;
    else
        e->reason[0] = '\0';
}

static void report_entry(const device_entry* e, yr_device_cb cb, void* user)
{
    yr_device_info info;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.flags = e->flags;
    info.key = e->key;
    info.label = e->label;
    info.driver = "pei";
    info.serial = "";
    info.module_version = "";
    info.hardware_info = e->hardware_info;
    info.flags_reason = e->reason;
    cb(user, &info);
}

typedef struct enum_ctx {
    yr_device_cb cb;
    void* user;
} enum_ctx;

static void enum_port(void* user, const char* path)
{
    enum_ctx* c = (enum_ctx*)user;
    device_entry e;
    make_entry(path, &e);
    report_entry(&e, c->cb, c->user);
}

static yr_status pei_enumerate(yr_plugin* p, yr_device_cb cb, void* user)
{
    if (p == NULL || cb == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    enum_ctx c = {cb, user};
    if (pei_port_list(getenv(PEI_PORTS_ENV), enum_port, &c) == 0) {
        set_plugin_error(p, "no serial port found (/dev/serial/by-id, /dev/ttyUSB*, /dev/ttyACM*): connect "
                            "the terminal's PEI cable (a USB serial adapter or the terminal's USB port)");
        return YR_ERR_NO_DEVICE;
    }
    set_plugin_error(p, "");
    return YR_OK;
}

static void default_settings(pei_settings* s, const char* path)
{
    memset(s, 0, sizeof *s);
    copy_text(s->port, sizeof s->port, path);
    s->baud = PEI_DEFAULT_BAUD;
    s->profile = PEI_PROFILE_CELL_LIST;
    copy_text(s->serving_command, sizeof s->serving_command, PEI_DEFAULT_SERVING_COMMAND);
    copy_text(s->serving_fields, sizeof s->serving_fields, PEI_DEFAULT_SERVING_FIELDS);
    copy_text(s->neighbour_command, sizeof s->neighbour_command, PEI_DEFAULT_NEIGHBOUR_COMMAND);
    copy_text(s->neighbour_fields, sizeof s->neighbour_fields, PEI_DEFAULT_NEIGHBOUR_FIELDS);
    s->creg_with_mode = 0;
    s->reply_timeout_ms = 1000;
    s->band = 3;
    s->offset = 0;
    s->log_commands = 1;
}

static void reset_status(pei_status* st)
{
    memset(st, 0, sizeof *st);
    st->terminal = PEI_TERMINAL_CLOSED;
    st->csq_code = 99;
    st->registration = 4;
}

static yr_status pei_open(yr_plugin* p, const char* key, yr_device** out)
{
    if (p == NULL || key == NULL || out == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    const size_t plen = strlen(PEI_KEY_PREFIX);
    if (strncmp(key, PEI_KEY_PREFIX, plen) != 0 || key[plen] == '\0' || strlen(key + plen) >= PEI_PATH_BYTES) {
        set_plugin_error(p, "not a terminal device key (expected pei:<serial port>)");
        return YR_ERR_NOT_FOUND;
    }
    const char* path = key + plen;
    char why[PEI_TEXT_BYTES];
    const int check = pei_port_check(path, why, sizeof why);
    if (check == 0) {
        char t[PEI_PATH_BYTES + 64];
        pei_format(t, sizeof t, "the serial port %s is not there (unplugged?)", path);
        set_plugin_error(p, t);
        return YR_ERR_NO_DEVICE;
    }
    if (check < 0) {
        set_plugin_error(p, why);
        return YR_ERR_IO;
    }
    yr_device* d = (yr_device*)calloc(1, sizeof *d);
    if (d == NULL)
        return YR_ERR_NO_MEMORY;
    d->plugin = p;
    device_entry e;
    make_entry(path, &e);
    copy_text(d->key, sizeof d->key, e.key);
    copy_text(d->path, sizeof d->path, path);
    copy_text(d->watch_port, sizeof d->watch_port, path);
    copy_text(d->label, sizeof d->label, e.label);
    copy_text(d->hardware_info, sizeof d->hardware_info, e.hardware_info);
    pthread_mutex_init(&d->mutex, NULL);
    init_cond_monotonic(&d->wake);
    d->state = YR_DEVICE_OPEN;
    default_settings(&d->settings, path);
    reset_status(&d->status);
    d->plan.struct_size = sizeof d->plan;
    d->plan.type = YR_PLAN_SERVING;
    d->plan.flags = YR_PLAN_MEASURE_NEIGHBOURS;
    d->stats.struct_size = sizeof d->stats;
    d->stats.receiver_load = NAN;
    d->stats.sample_rate_hz = NAN;
    atomic_init(&d->stop, 0);
    pthread_mutex_lock(&p->mutex);
    d->next = p->devices;
    p->devices = d;
    copy_text(p->last_error, sizeof p->last_error, "");
    pthread_mutex_unlock(&p->mutex);
    *out = d;
    return YR_OK;
}

static void pei_close(yr_device* d)
{
    if (d == NULL)
        return;
    yr_plugin* p = d->plugin;
    pthread_mutex_lock(&d->mutex);
    atomic_store(&d->stop, 1);
    pthread_cond_broadcast(&d->wake);
    if (d->state == YR_DEVICE_STREAMING)
        d->state = YR_DEVICE_OPEN;
    pthread_mutex_unlock(&d->mutex);
    pei_session_join(d);
    pthread_mutex_lock(&p->mutex);
    for (yr_device** it = &p->devices; *it != NULL; it = &(*it)->next)
        if (*it == d) {
            *it = d->next;
            break;
        }
    pthread_mutex_unlock(&p->mutex);
    pthread_cond_destroy(&d->wake);
    pthread_mutex_destroy(&d->mutex);
    free(d);
}

static yr_status pei_device_info(yr_device* d, yr_device_cb cb, void* user)
{
    if (d == NULL || cb == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    device_entry e;
    make_entry(d->path, &e);
    report_entry(&e, cb, user);
    return YR_OK;
}

static uint32_t pei_state(yr_device* d)
{
    if (d == NULL)
        return YR_DEVICE_CLOSED;
    pthread_mutex_lock(&d->mutex);
    const uint32_t s = d->state;
    pthread_mutex_unlock(&d->mutex);
    return s;
}

/* TETRA bands the PEI reports carriers in (the terminal picks the cell). */
static const yr_freq_range kRanges[] = {{350000000u, 470000000u}, {806000000u, 921000000u}};

static yr_status pei_plan_ranges(yr_device* d, yr_plan_ranges_cb cb, void* user)
{
    if (d == NULL || cb == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    if (pei_state(d) == YR_DEVICE_LOST)
        return YR_ERR_DEVICE_LOST;
    yr_plan_ranges r;
    memset(&r, 0, sizeof r);
    r.struct_size = sizeof r;
    r.plan_types = 1u << YR_PLAN_SERVING;
    r.range_count = 2;
    r.max_carriers = 1 + 32; /* serving + the neighbours a terminal monitors */
    r.ranges = kRanges;
    r.max_span_hz = 0.0;     /* no span: the terminal measures its own cells */
    cb(user, &r);
    return YR_OK;
}

/* SERVING with the terminal's own choice only (no Force Channel, spec 9). */
static yr_status check_plan(yr_device* d, const yr_plan* plan)
{
    if (plan == NULL || plan->struct_size < sizeof(uint32_t) * 2) {
        set_error_locked(d, "no plan given");
        return YR_ERR_INVALID_ARGUMENT;
    }
    if (plan->type != YR_PLAN_SERVING) {
        set_error_locked(d, "a terminal offers only the serving plan: it measures the cell it is camped on and "
                            "its neighbours");
        return YR_ERR_UNSUPPORTED;
    }
    if (plan->serving_hz != 0) {
        set_error_locked(d, "the terminal chooses its serving cell itself (no Force Channel); use serving "
                            "carrier 0");
        return YR_ERR_UNSUPPORTED;
    }
    return YR_OK;
}

static void keep_plan(yr_device* d, const yr_plan* plan)
{
    memset(&d->plan, 0, sizeof d->plan);
    d->plan.struct_size = sizeof d->plan;
    d->plan.type = plan->type;
    d->plan.flags = YR_HAS_FIELD(plan, yr_plan, flags) ? plan->flags : 0;
}

static yr_status pei_set_plan(yr_device* d, const yr_plan* plan)
{
    if (d == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&d->mutex);
    yr_status st = YR_OK;
    if (d->state == YR_DEVICE_LOST)
        st = YR_ERR_DEVICE_LOST;
    else if (d->state == YR_DEVICE_STREAMING) {
        set_error_locked(d, "stop the stream to change the plan (no live plan changes)");
        st = YR_ERR_BUSY;
    } else if ((st = check_plan(d, plan)) == YR_OK)
        keep_plan(d, plan);
    pthread_mutex_unlock(&d->mutex);
    return st;
}

static yr_status pei_start_stream(yr_device* d, const yr_plan* plan, yr_stream* sink)
{
    if (d == NULL || sink == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&d->mutex);
    yr_status st = YR_OK;
    if (d->state == YR_DEVICE_LOST)
        st = YR_ERR_DEVICE_LOST;
    else if (d->state != YR_DEVICE_OPEN) {
        set_error_locked(d, "the device is already streaming");
        st = YR_ERR_BUSY;
    } else if ((st = check_plan(d, plan)) == YR_OK)
        keep_plan(d, plan);
    pthread_mutex_unlock(&d->mutex);
    if (st != YR_OK)
        return st;
    pei_session_join(d); /* a stream thread left by a loss or an error */
    return pei_session_start(d, sink);
}

static yr_status pei_stop_stream(yr_device* d)
{
    if (d == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&d->mutex);
    const uint32_t state = d->state;
    atomic_store(&d->stop, 1);
    pthread_cond_broadcast(&d->wake);
    if (state == YR_DEVICE_STREAMING)
        d->state = YR_DEVICE_OPEN;
    pthread_mutex_unlock(&d->mutex);
    pei_session_join(d);
    pthread_mutex_lock(&d->mutex);
    d->sink = NULL;
    if (d->state != YR_DEVICE_LOST) {
        d->status.terminal = PEI_TERMINAL_CLOSED;
        copy_text(d->watch_port, sizeof d->watch_port, d->path);
    }
    pthread_mutex_unlock(&d->mutex);
    return state == YR_DEVICE_LOST ? YR_ERR_DEVICE_LOST : YR_OK;
}

static yr_status pei_stream_stats(yr_device* d, yr_stream_stats* out)
{
    if (d == NULL || out == NULL || out->struct_size < sizeof(uint32_t) * 2)
        return YR_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&d->mutex);
    yr_stream_stats s = d->stats;
    pthread_mutex_unlock(&d->mutex);
    const uint32_t size = out->struct_size < sizeof s ? out->struct_size : (uint32_t)sizeof s;
    s.struct_size = size;
    memcpy(out, &s, size);
    return YR_OK;
}

static yr_status pei_last_error(yr_plugin* p, yr_device* d, char* buffer, uint32_t size)
{
    if (buffer == NULL || size == 0)
        return YR_ERR_INVALID_ARGUMENT;
    if (d != NULL) {
        pthread_mutex_lock(&d->mutex);
        copy_text(buffer, size, d->last_error);
        pthread_mutex_unlock(&d->mutex);
    } else if (p != NULL) {
        pthread_mutex_lock(&p->mutex);
        copy_text(buffer, size, p->last_error);
        pthread_mutex_unlock(&p->mutex);
    } else {
        buffer[0] = '\0';
    }
    return YR_OK;
}

/* ------------------------------------------------------------ settings */

static const yr_enum_item kBaudItems[] = {
    {9600, "b9600", "9600 baud"},       {19200, "b19200", "19200 baud"},   {38400, "b38400", "38400 baud"},
    {57600, "b57600", "57600 baud"},    {115200, "b115200", "115200 baud"}, {230400, "b230400", "230400 baud"},
    {460800, "b460800", "460800 baud"},
};
static const yr_enum_item kProfileItems[] = {
    {PEI_PROFILE_CELL_LIST, "cell_list", "EN 300 392-5 + vendor cell lists"},
    {PEI_PROFILE_ETSI, "etsi", "EN 300 392-5 only (no records)"},
};
static const yr_enum_item kOffsetItems[] = {
    {0, "none", "0 kHz"}, {1, "plus_6k25", "+6.25 kHz"}, {2, "minus_6k25", "-6.25 kHz"}, {3, "plus_12k5", "+12.5 kHz"},
};

enum {
    S_PORT,
    S_BAUD,
    S_PROFILE,
    S_SERVING_COMMAND,
    S_SERVING_FIELDS,
    S_NEIGHBOUR_COMMAND,
    S_NEIGHBOUR_FIELDS,
    S_CREG_MODE,
    S_TIMEOUT,
    S_BAND,
    S_OFFSET,
    S_LOG,
    S_COUNT
};

static const char* const kSettingKeys[S_COUNT] = {
    "port",           "baud",           "command_profile", "serving_command", "serving_fields",
    "neighbour_command", "neighbour_fields", "creg_with_mode", "reply_timeout_ms", "band",
    "offset",         "log_commands"};

static const char kFieldsHelp[] =
    "Meaning of each comma-separated value of the reply lines, in order: carrier, band, offset, freq_hz, "
    "freq_mhz, mcc, mnc, la, cc, rssi (dBm), csq (AT+CSQ code), c1 (dB), state, or - to skip a value.";
static const char kCommandHelp[] =
    "Vendor reading command (AT+NAME or AT+NAME?) whose reply lines \"+NAME: v1,v2,...\" list cells; from the "
    "terminal's PEI manual. The default is the example format of the simulated terminal. Only reading "
    "commands are sent; anything else is not used.";

static int find_key(const char* key, const char* const* keys, int count)
{
    if (key == NULL)
        return -1;
    for (int i = 0; i < count; ++i)
        if (strcmp(key, keys[i]) == 0)
            return i;
    return -1;
}

static void setting_def(yr_device* d, int i, yr_setting_def* s)
{
    const pei_settings* v = &d->settings;
    memset(s, 0, sizeof *s);
    s->struct_size = sizeof *s;
    s->key = kSettingKeys[i];
    s->unit = "";
    s->minimum = NAN;
    s->maximum = NAN;
    s->sort_order = i;
    s->group = "Serial port";
    const int etsi = v->profile == PEI_PROFILE_ETSI;
    switch (i) {
    case S_PORT:
        s->type = YR_TYPE_STRING;
        s->flags = YR_SETTING_RESTART;
        s->label = "Port";
        s->help = "Serial port of the terminal's PEI (e.g. /dev/ttyUSB0 or a /dev/serial/by-id name).";
        s->default_value = yr_value_string(d->path);
        break;
    case S_BAUD:
        s->type = YR_TYPE_ENUM;
        s->flags = YR_SETTING_RESTART;
        s->label = "Baud rate";
        s->help = "Bit rate of the PEI (8 data bits, no parity, 1 stop bit, no flow control); terminals use "
                  "38400 or 115200 baud, see their PEI manual.";
        s->items = kBaudItems;
        s->item_count = sizeof kBaudItems / sizeof kBaudItems[0];
        s->default_value = yr_value_enum(PEI_DEFAULT_BAUD);
        break;
    case S_PROFILE:
        s->type = YR_TYPE_ENUM;
        s->flags = YR_SETTING_RESTART;
        s->group = "Commands";
        s->label = "Command profile";
        s->help = "Which commands are read. EN 300 392-5 only gives the registration and the AT+CSQ level, "
                  "without carrier or cell identity, so it shows sensors but delivers no records; the "
                  "vendor cell lists add the serving and neighbour cells.";
        s->items = kProfileItems;
        s->item_count = 2;
        s->default_value = yr_value_enum(PEI_PROFILE_CELL_LIST);
        break;
    case S_SERVING_COMMAND:
    case S_NEIGHBOUR_COMMAND:
        s->type = YR_TYPE_STRING;
        s->flags = YR_SETTING_RESTART | YR_SETTING_ADVANCED | (etsi ? YR_SETTING_UNAVAILABLE : 0u);
        s->unavailable_reason = etsi ? "only with the vendor cell-list profile" : NULL;
        s->group = "Commands";
        s->label = i == S_SERVING_COMMAND ? "Serving cell command" : "Neighbour cells command";
        s->help = kCommandHelp;
        s->default_value = yr_value_string(i == S_SERVING_COMMAND ? PEI_DEFAULT_SERVING_COMMAND
                                                                  : PEI_DEFAULT_NEIGHBOUR_COMMAND);
        break;
    case S_SERVING_FIELDS:
    case S_NEIGHBOUR_FIELDS:
        s->type = YR_TYPE_STRING;
        s->flags = YR_SETTING_RESTART | YR_SETTING_ADVANCED | (etsi ? YR_SETTING_UNAVAILABLE : 0u);
        s->unavailable_reason = etsi ? "only with the vendor cell-list profile" : NULL;
        s->group = "Commands";
        s->label = i == S_SERVING_FIELDS ? "Serving cell values" : "Neighbour cell values";
        s->help = kFieldsHelp;
        s->default_value = yr_value_string(i == S_SERVING_FIELDS ? PEI_DEFAULT_SERVING_FIELDS
                                                                 : PEI_DEFAULT_NEIGHBOUR_FIELDS);
        break;
    case S_CREG_MODE:
        s->type = YR_TYPE_BOOL;
        s->flags = YR_SETTING_RESTART | YR_SETTING_ADVANCED;
        s->group = "Commands";
        s->label = "AT+CREG with mode";
        s->help = "The terminal answers AT+CREG? in the 3GPP TS 27.007 form \"+CREG: <n>,<stat>,...\" instead "
                  "of the TETRA form \"+CREG: <stat>,...\".";
        s->default_value = yr_value_bool(0);
        break;
    case S_TIMEOUT:
        s->type = YR_TYPE_UINT;
        s->flags = YR_SETTING_RESTART | YR_SETTING_ADVANCED;
        s->label = "Reply timeout";
        s->unit = "ms";
        s->help = "How long to wait for the reply to one command; without a reply the terminal is asked "
                  "again (ATE0) on the next poll.";
        s->minimum = 100;
        s->maximum = 10000;
        s->step = 50;
        s->default_value = yr_value_uint(1000);
        break;
    case S_BAND:
        s->type = YR_TYPE_UINT;
        s->flags = YR_SETTING_RESTART;
        s->group = "Commands";
        s->label = "Frequency band";
        s->help = "Band code for carrier numbers without a band in the reply: base frequency = code x 100 MHz "
                  "(3: 300 MHz, so carrier 3600 is 390 MHz; EN 300 392-2 21.4.4.1).";
        s->minimum = 1;
        s->maximum = 9;
        s->step = 1;
        s->default_value = yr_value_uint(3);
        break;
    case S_OFFSET:
        s->type = YR_TYPE_ENUM;
        s->flags = YR_SETTING_RESTART;
        s->group = "Commands";
        s->label = "Carrier offset";
        s->help = "Carrier offset for carrier numbers without an offset in the reply.";
        s->items = kOffsetItems;
        s->item_count = 4;
        s->default_value = yr_value_enum(0);
        break;
    case S_LOG:
        s->type = YR_TYPE_BOOL;
        s->flags = YR_SETTING_ADVANCED;
        s->group = "Commands";
        s->label = "Command log";
        s->help = "Writes every AT command and reply line to the device log (debug level), shown in the "
                  "debug window.";
        s->default_value = yr_value_bool(1);
        break;
    default:
        break;
    }
}

static yr_status pei_list_settings(yr_device* d, yr_setting_cb cb, void* user)
{
    if (d == NULL || cb == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&d->mutex);
    if (d->state == YR_DEVICE_LOST) {
        pthread_mutex_unlock(&d->mutex);
        return YR_ERR_DEVICE_LOST;
    }
    yr_setting_def defs[S_COUNT];
    for (int i = 0; i < S_COUNT; ++i)
        setting_def(d, i, &defs[i]);
    pthread_mutex_unlock(&d->mutex);
    /* The device's own strings stay valid: settings change on the control
     * thread only, which is this one. */
    for (int i = 0; i < S_COUNT; ++i)
        cb(user, &defs[i]);
    return YR_OK;
}

static yr_value string_out(const char* s, char* text, uint32_t text_size)
{
    copy_text(text, text_size, s);
    return yr_value_string(text);
}

static yr_status pei_get_setting(yr_device* d, const char* key, yr_value* out, char* text, uint32_t text_size)
{
    if (d == NULL || out == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    const int i = find_key(key, kSettingKeys, S_COUNT);
    if (i < 0)
        return YR_ERR_NOT_FOUND;
    pthread_mutex_lock(&d->mutex);
    yr_status st = YR_OK;
    const pei_settings* v = &d->settings;
    if (d->state == YR_DEVICE_LOST) {
        st = YR_ERR_DEVICE_LOST;
    } else if ((i == S_PORT || i == S_SERVING_COMMAND || i == S_SERVING_FIELDS || i == S_NEIGHBOUR_COMMAND ||
                i == S_NEIGHBOUR_FIELDS) &&
               (text == NULL || text_size == 0)) {
        st = YR_ERR_INVALID_ARGUMENT;
    } else {
        switch (i) {
        case S_PORT: *out = string_out(v->port, text, text_size); break;
        case S_BAUD: *out = yr_value_enum(v->baud); break;
        case S_PROFILE: *out = yr_value_enum(v->profile); break;
        case S_SERVING_COMMAND: *out = string_out(v->serving_command, text, text_size); break;
        case S_SERVING_FIELDS: *out = string_out(v->serving_fields, text, text_size); break;
        case S_NEIGHBOUR_COMMAND: *out = string_out(v->neighbour_command, text, text_size); break;
        case S_NEIGHBOUR_FIELDS: *out = string_out(v->neighbour_fields, text, text_size); break;
        case S_CREG_MODE: *out = yr_value_bool(v->creg_with_mode); break;
        case S_TIMEOUT: *out = yr_value_uint(v->reply_timeout_ms); break;
        case S_BAND: *out = yr_value_uint(v->band); break;
        case S_OFFSET: *out = yr_value_enum(v->offset); break;
        case S_LOG: *out = yr_value_bool(v->log_commands); break;
        default: st = YR_ERR_NOT_FOUND; break;
        }
    }
    pthread_mutex_unlock(&d->mutex);
    return st;
}

/* Text settings: printable, without line ends (no command injection). */
static int valid_text(const char* s, size_t max)
{
    if (s == NULL || strlen(s) >= max)
        return 0;
    for (const unsigned char* c = (const unsigned char*)s; *c != '\0'; ++c)
        if (*c < 0x20 || *c == 0x7f)
            return 0;
    return 1;
}

static int number_in(const yr_value* v, uint32_t type, double lo, double hi, double* out)
{
    double x;
    if (v->type != type)
        return 0;
    if (type == YR_TYPE_UINT)
        x = (double)v->u.u64;
    else
        return 0;
    if (x < lo || x > hi)
        return 0;
    *out = x;
    return 1;
}

static yr_status pei_set_setting(yr_device* d, const char* key, const yr_value* value)
{
    if (d == NULL || value == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    const int i = find_key(key, kSettingKeys, S_COUNT);
    if (i < 0) {
        pthread_mutex_lock(&d->mutex);
        set_error_locked(d, "unknown setting");
        pthread_mutex_unlock(&d->mutex);
        return YR_ERR_NOT_FOUND;
    }
    pthread_mutex_lock(&d->mutex);
    pei_settings* v = &d->settings;
    yr_status st = YR_OK;
    int notify = 0;
    double x = 0.0;
    if (d->state == YR_DEVICE_LOST) {
        st = YR_ERR_DEVICE_LOST;
        goto done;
    }
    if (d->state == YR_DEVICE_STREAMING && i != S_LOG) {
        set_error_locked(d, "stop the stream to change this setting");
        st = YR_ERR_BUSY;
        goto done;
    }
    if (i == S_SERVING_COMMAND || i == S_SERVING_FIELDS || i == S_NEIGHBOUR_COMMAND || i == S_NEIGHBOUR_FIELDS) {
        if (v->profile == PEI_PROFILE_ETSI) {
            set_error_locked(d, "only with the vendor cell-list profile");
            st = YR_ERR_UNSUPPORTED;
            goto done;
        }
    }
    switch (i) {
    case S_PORT:
    case S_SERVING_COMMAND:
    case S_SERVING_FIELDS:
    case S_NEIGHBOUR_COMMAND:
    case S_NEIGHBOUR_FIELDS: {
        if (value->type != YR_TYPE_STRING || !valid_text(value->u.str, PEI_TEXT_BYTES)) {
            set_error_locked(d, "expected a text of at most 255 characters without line ends");
            st = YR_ERR_INVALID_ARGUMENT;
            break;
        }
        char* dst = i == S_PORT                ? v->port
                    : i == S_SERVING_COMMAND   ? v->serving_command
                    : i == S_SERVING_FIELDS    ? v->serving_fields
                    : i == S_NEIGHBOUR_COMMAND ? v->neighbour_command
                                               : v->neighbour_fields;
        copy_text(dst, PEI_TEXT_BYTES, value->u.str);
        break;
    }
    case S_BAUD: {
        int ok = 0;
        if (value->type == YR_TYPE_ENUM)
            for (size_t k = 0; k < sizeof kBaudItems / sizeof kBaudItems[0]; ++k)
                ok |= kBaudItems[k].value == value->u.i64;
        if (!ok) {
            set_error_locked(d, "not one of the offered baud rates");
            st = YR_ERR_INVALID_ARGUMENT;
            break;
        }
        v->baud = (int)value->u.i64;
        break;
    }
    case S_PROFILE:
        if (value->type != YR_TYPE_ENUM || (value->u.i64 != PEI_PROFILE_CELL_LIST && value->u.i64 != PEI_PROFILE_ETSI)) {
            set_error_locked(d, "not one of the command profiles");
            st = YR_ERR_INVALID_ARGUMENT;
            break;
        }
        notify = v->profile != (int)value->u.i64; /* availability of the command settings (RAD-44) */
        v->profile = (int)value->u.i64;
        break;
    case S_CREG_MODE:
    case S_LOG:
        if (value->type != YR_TYPE_BOOL) {
            set_error_locked(d, "expected true or false");
            st = YR_ERR_INVALID_ARGUMENT;
            break;
        }
        if (i == S_CREG_MODE)
            v->creg_with_mode = value->u.i64 != 0;
        else
            v->log_commands = value->u.i64 != 0;
        break;
    case S_TIMEOUT:
        if (!number_in(value, YR_TYPE_UINT, 100, 10000, &x)) {
            set_error_locked(d, "expected 100..10000 ms");
            st = YR_ERR_INVALID_ARGUMENT;
            break;
        }
        v->reply_timeout_ms = (uint32_t)(llround(x / 50.0) * 50);
        break;
    case S_BAND:
        if (!number_in(value, YR_TYPE_UINT, 1, 9, &x)) {
            set_error_locked(d, "expected a band code 1..9");
            st = YR_ERR_INVALID_ARGUMENT;
            break;
        }
        v->band = (uint32_t)x;
        break;
    case S_OFFSET:
        if (value->type != YR_TYPE_ENUM || value->u.i64 < 0 || value->u.i64 > 3) {
            set_error_locked(d, "not one of the offsets");
            st = YR_ERR_INVALID_ARGUMENT;
            break;
        }
        v->offset = (uint32_t)value->u.i64;
        break;
    default:
        st = YR_ERR_NOT_FOUND;
        break;
    }
done:
    pthread_mutex_unlock(&d->mutex);
    if (notify)
        d->plugin->host->settings_changed(d->plugin->host->host, d);
    return st;
}

/* ------------------------------------------------------------ sensors */

static const yr_enum_item kTerminalItems[] = {
    {PEI_TERMINAL_CLOSED, "closed", "Not connected"},
    {PEI_TERMINAL_CONNECTING, "connecting", "Opening the port"},
    {PEI_TERMINAL_NO_REPLY, "no_reply", "No reply to AT commands"},
    {PEI_TERMINAL_READY, "ready", "Ready"},
    {PEI_TERMINAL_LOST, "lost", "Port unplugged"},
};

enum { N_TERMINAL, N_MANUFACTURER, N_MODEL, N_FIRMWARE, N_TEI, N_REGISTRATION, N_LA, N_CSQ, N_SERVING, N_POLLS,
       N_TIMEOUTS, N_NEIGHBOURS, N_UNSUPPORTED, N_PROBLEM, N_COUNT };

static const char* const kSensorKeys[N_COUNT] = {
    "terminal_state", "manufacturer", "model",      "firmware",  "tei",         "registration", "registered_la",
    "csq_code",       "serving_cell", "polls",      "timeouts",  "neighbour_count", "unsupported_commands",
    "problem"};

static void sensor_def(int i, yr_setting_def* s)
{
    memset(s, 0, sizeof *s);
    s->struct_size = sizeof *s;
    s->key = kSensorKeys[i];
    s->flags = YR_SETTING_READ_ONLY;
    s->unit = "";
    s->minimum = NAN;
    s->maximum = NAN;
    s->sort_order = i;
    s->default_value = yr_value_none();
    s->type = YR_TYPE_STRING;
    s->group = "Terminal";
    switch (i) {
    case N_TERMINAL:
        s->type = YR_TYPE_ENUM;
        s->items = kTerminalItems;
        s->item_count = 5;
        s->label = "Terminal";
        s->help = "Connection state: port open, the terminal answering AT commands.";
        break;
    case N_MANUFACTURER:
        s->group = "Radio info";
        s->label = "Manufacturer";
        s->help = "AT+GMI (TRM-04), read when the connection starts.";
        break;
    case N_MODEL:
        s->group = "Radio info";
        s->label = "Model";
        s->help = "AT+GMM (TRM-04).";
        break;
    case N_FIRMWARE:
        s->group = "Radio info";
        s->label = "Firmware";
        s->help = "AT+GMR, the revision of the terminal's software (TRM-04).";
        break;
    case N_TEI:
        s->group = "Radio info";
        s->label = "Serial / TEI";
        s->help = "AT+GSN: the terminal equipment identity of a TETRA terminal (TRM-04).";
        break;
    case N_REGISTRATION:
        s->type = YR_TYPE_ENUM;
        s->items = kRegistrationItems;
        s->item_count = 6;
        s->label = "Registration";
        s->help = "Registration state from AT+CREG?.";
        break;
    case N_LA:
        s->label = "Registered location area";
        s->help = "Location area from AT+CREG?, empty when not reported.";
        break;
    case N_CSQ:
        s->type = YR_TYPE_UINT;
        s->label = "AT+CSQ level code";
        s->help = "0..31 = -113..-51 dBm in 2 dB steps, 99 = not known.";
        s->minimum = 0;
        s->maximum = 99;
        break;
    case N_SERVING:
        s->label = "Serving cell";
        s->help = "The last serving cell reading: carrier, identity and level.";
        break;
    case N_POLLS:
        s->type = YR_TYPE_UINT;
        s->group = "Diagnostics";
        s->label = "Polls";
        s->help = "Reading cycles completed in this stream (one per second).";
        break;
    case N_TIMEOUTS:
        s->type = YR_TYPE_UINT;
        s->group = "Diagnostics";
        s->label = "Timeouts";
        s->help = "Commands the terminal did not answer within the reply timeout.";
        break;
    case N_NEIGHBOURS:
        s->type = YR_TYPE_UINT;
        s->label = "Neighbour cells";
        s->help = "Neighbour cells in the last reading.";
        break;
    case N_UNSUPPORTED:
        s->group = "Diagnostics";
        s->label = "Unsupported commands";
        s->help = "Commands the terminal refused with ERROR in this connection (not sent again).";
        break;
    case N_PROBLEM:
        s->group = "Diagnostics";
        s->label = "Problem";
        s->help = "The last connection or command problem, empty when none.";
        break;
    default:
        break;
    }
}

static yr_status pei_list_sensors(yr_device* d, yr_setting_cb cb, void* user)
{
    if (d == NULL || cb == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    if (pei_state(d) == YR_DEVICE_LOST)
        return YR_ERR_DEVICE_LOST;
    for (int i = 0; i < N_COUNT; ++i) {
        yr_setting_def s;
        sensor_def(i, &s);
        cb(user, &s);
    }
    return YR_OK;
}

static yr_status pei_read_sensor(yr_device* d, const char* key, yr_value* out, char* text, uint32_t text_size)
{
    if (d == NULL || out == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    const int i = find_key(key, kSensorKeys, N_COUNT);
    if (i < 0)
        return YR_ERR_NOT_FOUND;
    pthread_mutex_lock(&d->mutex);
    const pei_status* s = &d->status;
    yr_status st = YR_OK;
    const int is_text = i == N_MANUFACTURER || i == N_MODEL || i == N_FIRMWARE || i == N_TEI || i == N_LA ||
                        i == N_SERVING || i == N_UNSUPPORTED || i == N_PROBLEM;
    if (d->state == YR_DEVICE_LOST)
        st = YR_ERR_DEVICE_LOST;
    else if (is_text && (text == NULL || text_size == 0))
        st = YR_ERR_INVALID_ARGUMENT;
    else {
        switch (i) {
        case N_TERMINAL: *out = yr_value_enum(s->terminal); break;
        case N_MANUFACTURER: *out = string_out(s->manufacturer, text, text_size); break;
        case N_MODEL: *out = string_out(s->model, text, text_size); break;
        case N_FIRMWARE: *out = string_out(s->firmware, text, text_size); break;
        case N_TEI: *out = string_out(s->tei, text, text_size); break;
        case N_REGISTRATION: *out = yr_value_enum(s->registration); break;
        case N_LA: *out = string_out(s->la, text, text_size); break;
        case N_CSQ: *out = yr_value_uint((uint64_t)s->csq_code); break;
        case N_SERVING: *out = string_out(s->serving, text, text_size); break;
        case N_POLLS: *out = yr_value_uint(s->polls); break;
        case N_TIMEOUTS: *out = yr_value_uint(s->timeouts); break;
        case N_NEIGHBOURS: *out = yr_value_uint(s->neighbours); break;
        case N_UNSUPPORTED: *out = string_out(s->unsupported, text, text_size); break;
        case N_PROBLEM: *out = string_out(s->problem, text, text_size); break;
        default: st = YR_ERR_NOT_FOUND; break;
        }
    }
    pthread_mutex_unlock(&d->mutex);
    return st;
}

static yr_status pei_self_test(yr_device* d, yr_selftest_cb cb, void* user)
{
    if (d == NULL || cb == NULL)
        return YR_ERR_INVALID_ARGUMENT;
    const uint32_t s = pei_state(d);
    if (s == YR_DEVICE_LOST)
        return YR_ERR_DEVICE_LOST;
    if (s == YR_DEVICE_STREAMING)
        return YR_ERR_BUSY;
    return pei_session_self_test(d, cb, user);
}

/* ------------------------------------------------------------ descriptor */

static const yr_stream_control kStreamControl = {
    sizeof(yr_stream_control), 0,          pei_enumerate,    pei_open,         pei_close,
    pei_device_info,           pei_state,  pei_plan_ranges,  pei_set_plan,     pei_start_stream,
    pei_stop_stream,           pei_stream_stats, pei_last_error};

static const yr_device_control kDeviceControl = {
    sizeof(yr_device_control), 0,    pei_list_settings, pei_get_setting, pei_set_setting, NULL, NULL, NULL,
    pei_list_sensors,          pei_read_sensor, pei_self_test};

YR_EXPORT const yr_descriptor* yantra_radio_entry(void)
{
    static const yr_descriptor descriptor = {
        sizeof(yr_descriptor),
        {YR_API_VERSION_MAJOR, YR_API_VERSION_MINOR},
        YR_CAP_SELF_CALIBRATED | YR_CAP_HOTPLUG,
        "pei",
        "TETRA terminal (PEI)",
        "0.1.0",
        "The Yantra contributors",
        "GPL-3.0-or-later",
        "A TETRA terminal read over its PEI (AT commands, EN 300 392-5): serving and neighbour cells, C1 and "
        "radio info (RAD-80)",
        YANTRA_RADIOS_SOURCE_URL,
        "tetra",
        1,
        0,
        kSchemas,
        NULL,
        pei_init,
        pei_shutdown,
        &kStreamControl,
        &kDeviceControl,
        NULL,
    };
    return &descriptor;
}
