/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The terminal session of the "pei" plugin (RAD-80, TRM-01, TRM-04): the
 * stream thread opens the port, wakes the terminal (ATE0), reads the
 * radio info once per connection (AT+GMI/GMM/GMR/GSN) and then polls once
 * per second (AT+CSQ, AT+CREG? and, with the cell-list profile, the vendor
 * serving and neighbour cell commands), turning each poll into core
 * records (RAD-21) in dBm. A command the terminal refuses with ERROR is
 * not sent again in that connection; a timeout sends ATE0 again; an I/O
 * error reopens the port. Copied from yantra-v1 c24e8f5:
 * core/terminal/terminal_session.cpp, rewritten in C for the radio API.
 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "pei_device.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POLL_INTERVAL_MS 1000
#define RETRY_MS 1000

enum { CMD_CSQ, CMD_CREG, CMD_SERVING, CMD_NEIGHBOURS, CMD_COUNT };
enum { EX_DONE = 0, EX_TIMEOUT = 1, EX_IO = 2, EX_STOP = 3 };

typedef struct session {
    yr_device* d;
    atomic_int* stop;
    pei_port* port;
    pei_settings s;
    pei_parser parser;
    pei_field serving_fields[PEI_MAX_FIELDS];
    int serving_count; /* < 0: not used */
    pei_field neighbour_fields[PEI_MAX_FIELDS];
    int neighbour_count;
    char serving_prefix[PEI_COMMAND_BYTES];
    char neighbour_prefix[PEI_COMMAND_BYTES];
    int refused[CMD_COUNT];
    int have_serving;
    uint64_t serving_hz;
    char config_problem[PEI_TEXT_BYTES]; /* a command setting not used, shown while connected */
} session;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ------------------------------------------------------------ commands */

/* Vendor commands must be reading commands AT+NAME or AT+NAME? (also $,
 * % or ^ prefixes); the PEI's call, SDS and number commands are refused
 * (RAD-17): +CMG*, +CMS*, +CTS*, +CTX*, +CTC*, +CTG*, +CTD*, +CUTX*, +CNUM. */
static int reading_command(const char* cmd, char* why, size_t why_size)
{
    size_t n = strlen(cmd);
    if (n < 4 || n >= PEI_COMMAND_BYTES || toupper((unsigned char)cmd[0]) != 'A' ||
        toupper((unsigned char)cmd[1]) != 'T' || strchr("+$%^", cmd[2]) == NULL) {
        pei_format(why, why_size, "\"%s\" is not a command of the form AT+NAME or AT+NAME?", cmd);
        return 0;
    }
    size_t end = n;
    if (cmd[end - 1] == '?')
        --end;
    if (end <= 3) {
        pei_format(why, why_size, "\"%s\" has no command name", cmd);
        return 0;
    }
    for (size_t i = 3; i < end; ++i)
        if (!(isalnum((unsigned char)cmd[i]) || cmd[i] == '_')) {
            pei_format(why, why_size, "\"%s\" is not a reading command (AT+NAME or AT+NAME?)", cmd);
            return 0;
        }
    static const char* const refused[] = {"CMG", "CMS", "CTS", "CTX", "CTC", "CTG", "CTD", "CUTX", "CNUM"};
    for (size_t k = 0; k < sizeof refused / sizeof refused[0]; ++k) {
        const size_t len = strlen(refused[k]);
        int match = end - 3 >= len;
        for (size_t i = 0; match && i < len; ++i)
            match = toupper((unsigned char)cmd[3 + i]) == refused[k][i];
        if (match && cmd[2] == '+') {
            pei_format(why, why_size, "\"%s\" is a call, SDS or number command, which the plugin never sends", cmd);
            return 0;
        }
    }
    return 1;
}

static void set_problem(session* ss, const char* text)
{
    pthread_mutex_lock(&ss->d->mutex);
    pei_format(ss->d->status.problem, sizeof ss->d->status.problem, "%s", text);
    pthread_mutex_unlock(&ss->d->mutex);
}

/* A command setting that cannot be used: shown as the problem for the
 * whole stream (the first one). */
static void config_problem(session* ss, const char* text)
{
    if (ss->config_problem[0] == '\0')
        pei_format(ss->config_problem, sizeof ss->config_problem, "%s", text);
    set_problem(ss, ss->config_problem);
}

static void set_terminal(session* ss, int terminal, const char* problem)
{
    pthread_mutex_lock(&ss->d->mutex);
    if (ss->d->state != YR_DEVICE_LOST)
        ss->d->status.terminal = terminal;
    if (problem != NULL)
        pei_format(ss->d->status.problem, sizeof ss->d->status.problem, "%s", problem);
    pthread_mutex_unlock(&ss->d->mutex);
}

/* The cell-list commands of the settings: checked and parsed once. */
static void setup_commands(session* ss)
{
    char why[PEI_TEXT_BYTES];
    char err[64];
    ss->serving_count = -1;
    ss->neighbour_count = -1;
    if (ss->s.profile != PEI_PROFILE_CELL_LIST)
        return;
    if (ss->s.serving_command[0] != '\0') {
        if (!reading_command(ss->s.serving_command, why, sizeof why)) {
            pei_log(ss->d, YR_LOG_WARNING, "serving cell command not used: %s", why);
            config_problem(ss, why);
        } else if ((ss->serving_count = pei_parse_fields(ss->s.serving_fields, ss->serving_fields,
                                                         PEI_MAX_FIELDS, err, sizeof err)) < 0) {
            pei_format(why, sizeof why, "serving cell values: unknown value name \"%s\"", err);
            pei_log(ss->d, YR_LOG_WARNING, "%s", why);
            config_problem(ss, why);
        }
        pei_reply_prefix(ss->s.serving_command, ss->serving_prefix, sizeof ss->serving_prefix);
    }
    if (ss->s.neighbour_command[0] != '\0') {
        if (!reading_command(ss->s.neighbour_command, why, sizeof why)) {
            pei_log(ss->d, YR_LOG_WARNING, "neighbour cells command not used: %s", why);
            config_problem(ss, why);
        } else if ((ss->neighbour_count = pei_parse_fields(ss->s.neighbour_fields, ss->neighbour_fields,
                                                           PEI_MAX_FIELDS, err, sizeof err)) < 0) {
            pei_format(why, sizeof why, "neighbour cell values: unknown value name \"%s\"", err);
            pei_log(ss->d, YR_LOG_WARNING, "%s", why);
            config_problem(ss, why);
        }
        pei_reply_prefix(ss->s.neighbour_command, ss->neighbour_prefix, sizeof ss->neighbour_prefix);
    }
}

/* Sends one command and collects its reply (AT command log at debug). */
static int exchange(session* ss, const char* cmd, const char* prefix)
{
    pei_parser* p = &ss->parser;
    pei_parser_begin(p, cmd, prefix);
    pei_port_flush_input(ss->port);
    if (ss->s.log_commands)
        pei_log(ss->d, YR_LOG_DEBUG, "> %s", cmd);
    char line[PEI_COMMAND_BYTES + 2];
    const int len = snprintf(line, sizeof line, "%s\r", cmd);
    if (len <= 0 || !pei_port_write(ss->port, line, (size_t)len))
        return EX_IO;
    const int64_t deadline = now_ms() + ss->s.reply_timeout_ms;
    char buf[512];
    while (!atomic_load(ss->stop)) {
        const long n = pei_port_read(ss->port, buf, sizeof buf, 50);
        if (n < 0)
            return EX_IO;
        if (n > 0 && pei_parser_feed(p, buf, (size_t)n)) {
            if (ss->s.log_commands) {
                for (unsigned k = 0; k < p->reply.line_count; ++k)
                    pei_log(ss->d, YR_LOG_DEBUG, "< %s", p->reply.lines[k]);
                pei_log(ss->d, YR_LOG_DEBUG, "< %s", p->reply.ok ? "OK" : p->reply.error);
            }
            return EX_DONE;
        }
        if (now_ms() > deadline) {
            if (ss->s.log_commands)
                pei_log(ss->d, YR_LOG_DEBUG, "< (no reply in %u ms)", ss->s.reply_timeout_ms);
            pthread_mutex_lock(&ss->d->mutex);
            ++ss->d->status.timeouts;
            pthread_mutex_unlock(&ss->d->mutex);
            return EX_TIMEOUT;
        }
    }
    /* Stopped with the command out: its reply may still come and must not
     * answer the next session's first command (settle). */
    pthread_mutex_lock(&ss->d->mutex);
    ss->d->reply_due_ms = deadline;
    pthread_mutex_unlock(&ss->d->mutex);
    return EX_STOP;
}

/* Before the first command of a session (stream or self-test): when the
 * last session was stopped while a command was out, its reply is read
 * away, until it is complete or its reply time is over. The flush in
 * exchange() does not catch a reply the terminal sends after it: a late
 * "OK" was taken as the answer to the self-test's ATE0 (RAD-46, TRM-01). */
static void settle(session* ss)
{
    pthread_mutex_lock(&ss->d->mutex);
    const int64_t due = ss->d->reply_due_ms;
    ss->d->reply_due_ms = 0;
    pthread_mutex_unlock(&ss->d->mutex);
    if (due == 0)
        return;
    pei_parser* p = &ss->parser;
    pei_parser_begin(p, "", "");
    char buf[512];
    while (!atomic_load(ss->stop) && now_ms() < due) {
        const long n = pei_port_read(ss->port, buf, sizeof buf, 50);
        if (n < 0 || (n > 0 && pei_parser_feed(p, buf, (size_t)n)))
            break;
    }
    if (ss->s.log_commands)
        pei_log(ss->d, YR_LOG_DEBUG, "< %s (the reply of the last session's command)",
                p->reply.done ? "read away" : "not seen");
}

static void note_refused(session* ss, int which, const char* cmd)
{
    ss->refused[which] = 1;
    pei_log(ss->d, YR_LOG_INFO, "%s not supported by the terminal: %s", cmd, ss->parser.reply.error);
    pthread_mutex_lock(&ss->d->mutex);
    char* u = ss->d->status.unsupported;
    const size_t used = strlen(u);
    pei_format(u + used, sizeof ss->d->status.unsupported - used, "%s%s (%s)", used > 0 ? ", " : "", cmd,
             ss->parser.reply.error);
    pthread_mutex_unlock(&ss->d->mutex);
}

/* Runs a polled command: 1 with an OK reply in ss->parser, 0 refused or
 * not used; *res gets EX_TIMEOUT/EX_IO/EX_STOP on those. */
static int run_cmd(session* ss, int which, const char* cmd, const char* prefix, int* res)
{
    if (ss->refused[which])
        return 0;
    *res = exchange(ss, cmd, prefix);
    if (*res != EX_DONE)
        return 0;
    if (!ss->parser.reply.ok) {
        note_refused(ss, which, cmd);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------ handshake */

/* ATE0 until OK: EX_DONE when the terminal answers. */
static int wake(session* ss)
{
    const int r = exchange(ss, "ATE0", "");
    if (r != EX_DONE)
        return r;
    return ss->parser.reply.ok ? EX_DONE : EX_TIMEOUT;
}

/* Radio info (TRM-04) into the sensors. */
static int identify(session* ss, int* missing)
{
    static const char* const cmds[4] = {"AT+GMI", "AT+GMM", "AT+GMR", "AT+GSN"};
    char text[4][PEI_TEXT_BYTES];
    *missing = 0;
    for (int i = 0; i < 4; ++i) {
        text[i][0] = '\0';
        const int r = exchange(ss, cmds[i], "");
        if (r == EX_IO || r == EX_STOP)
            return r;
        char prefix[PEI_COMMAND_BYTES];
        pei_reply_prefix(cmds[i], prefix, sizeof prefix);
        if (r == EX_DONE && ss->parser.reply.ok)
            pei_info_text(&ss->parser.reply, prefix, text[i], sizeof text[i]);
        if (text[i][0] == '\0')
            ++*missing;
    }
    pthread_mutex_lock(&ss->d->mutex);
    pei_status* st = &ss->d->status;
    pei_format(st->manufacturer, sizeof st->manufacturer, "%s", text[0]);
    pei_format(st->model, sizeof st->model, "%s", text[1]);
    pei_format(st->firmware, sizeof st->firmware, "%s", text[2]);
    pei_format(st->tei, sizeof st->tei, "%s", text[3]);
    pthread_mutex_unlock(&ss->d->mutex);
    return EX_DONE;
}

/* ------------------------------------------------------------ records */

typedef struct reading {
    int has_csq;
    pei_csq csq;
    int has_reg;
    pei_registration reg;
    int has_serving;
    pei_cell serving;
    unsigned neighbour_count;
    pei_cell neighbours[PEI_MAX_LINES];
} reading;

typedef struct cell_values {
    int ok;
    yr_value v[PEI_SCHEMA_FIELD_COUNT];
    uint32_t flags;
    uint32_t present;
    uint64_t hz;
} cell_values;

/* One record from a cell reading; ok = 0 without frequency or level. */
static void make_values(const session* ss, const pei_cell* c, int serving, const reading* r,
                        const uint32_t* inherit_mcc, const uint32_t* inherit_mnc, cell_values* out)
{
    memset(out, 0, sizeof *out);
    for (unsigned i = 0; i < PEI_SCHEMA_FIELD_COUNT; ++i)
        out->v[i] = yr_value_none();
    const uint32_t band = c->has_band ? c->band : ss->s.band;
    const uint32_t offset = c->has_offset ? c->offset : ss->s.offset;
    uint64_t hz = 0;
    if (c->has_freq)
        hz = c->freq_hz;
    else if (c->has_carrier)
        hz = pei_carrier_hz(band, c->carrier, offset);
    uint32_t carrier = 0;
    int has_carrier = 0;
    if (c->has_carrier) {
        carrier = c->carrier;
        has_carrier = 1;
    } else if (hz != 0) {
        has_carrier = pei_carrier_of(hz, band, offset, &carrier);
    }
    double level = 0.0;
    int has_level = 0;
    if (c->has_rssi) {
        level = c->rssi_dbm;
        has_level = 1;
    } else if (serving && r->has_csq) {
        has_level = pei_csq_to_dbm(r->csq.rssi_code, &level);
    }
    if (hz == 0 || !has_level)
        return;
    int has_mcc = c->has_mcc, has_mnc = c->has_mnc, has_la = c->has_la;
    uint32_t mcc = c->mcc, mnc = c->mnc, la = c->la;
    if (serving && r->has_reg && r->reg.has_network) {
        if (!has_mcc) {
            mcc = r->reg.mcc;
            has_mcc = 1;
        }
        if (!has_mnc) {
            mnc = r->reg.mnc;
            has_mnc = 1;
        }
    }
    if (serving && !has_la && r->has_reg && r->reg.has_la) {
        la = r->reg.la;
        has_la = 1;
    }
    /* A neighbour the terminal lists is of its own network (EN 300 392-2
     * 18.5: neighbours are announced by the serving cell). */
    if (!serving && !has_mcc && inherit_mcc != NULL) {
        mcc = *inherit_mcc;
        has_mcc = 1;
    }
    if (!serving && !has_mnc && inherit_mnc != NULL) {
        mnc = *inherit_mnc;
        has_mnc = 1;
    }
    out->hz = hz;
    out->v[YR_CORE_FREQUENCY_HZ] = yr_value_uint(hz);
    out->v[YR_CORE_ROLE] = yr_value_enum(serving ? YR_ROLE_SERVING : YR_ROLE_NEIGHBOUR);
    out->v[YR_CORE_LEVEL] = yr_value_double(level);
    if (has_carrier && has_mcc && has_mnc && has_la && c->has_cc) {
        out->v[YR_CORE_CHANNEL] = yr_value_uint(carrier);
        out->v[YR_CORE_MCC] = yr_value_uint(mcc);
        out->v[YR_CORE_MNC] = yr_value_uint(mnc);
        out->v[YR_CORE_LA] = yr_value_uint(la);
        out->v[YR_CORE_COLOUR_CODE] = yr_value_uint(c->cc);
    } else {
        out->flags |= YR_RECORD_NO_SYNC;
    }
    if (c->has_c1) {
        out->present |= YR_GROUP_C1;
        out->v[PEI_FIELD_INDEX_C1] = yr_value_double(c->c1_db);
    }
    if (serving) {
        out->present |= YR_GROUP_OTHER;
        if (r->has_reg)
            out->v[PEI_FIELD_INDEX_REGISTRATION] = yr_value_enum(r->reg.stat <= 5 ? r->reg.stat : 4);
        if (r->has_csq)
            out->v[PEI_FIELD_INDEX_CSQ] = yr_value_uint((uint64_t)r->csq.rssi_code);
    }
    if (c->state[0] != '\0') {
        out->present |= YR_GROUP_OTHER;
        out->v[PEI_FIELD_INDEX_STATE] = yr_value_string(c->state);
    }
    out->ok = 1;
}

/* device->mutex held */
static void push_locked(session* ss, const cell_values* cv, const yr_time* t)
{
    yr_device* d = ss->d;
    yr_record rec;
    memset(&rec, 0, sizeof rec);
    rec.struct_size = sizeof rec;
    rec.schema_index = 0;
    rec.time = *t;
    rec.flags = cv->flags | YR_RECORD_LEVEL_DBM; /* SELF_CALIBRATED: dBm (RAD-21, API 1.1) */
    rec.present = cv->present;
    rec.value_count = PEI_SCHEMA_FIELD_COUNT;
    rec.values = cv->v;
    const yr_status st = d->plugin->host->push_record(d->sink, &rec);
    if (st == YR_OK)
        ++d->stats.records_delivered;
    else
        ++d->stats.records_dropped;
}

static void deliver(session* ss, const reading* r)
{
    yr_device* d = ss->d;
    cell_values serving;
    memset(&serving, 0, sizeof serving);
    if (r->has_serving)
        make_values(ss, &r->serving, 1, r, NULL, NULL, &serving);
    uint32_t smcc = 0, smnc = 0;
    int inherit = 0;
    if (serving.ok && serving.v[YR_CORE_MCC].type == YR_TYPE_UINT) {
        smcc = (uint32_t)serving.v[YR_CORE_MCC].u.u64;
        smnc = (uint32_t)serving.v[YR_CORE_MNC].u.u64;
        inherit = 1;
    }
    cell_values* nb = (cell_values*)calloc(r->neighbour_count > 0 ? r->neighbour_count : 1, sizeof *nb);
    if (nb == NULL)
        return;
    for (unsigned k = 0; k < r->neighbour_count; ++k) {
        make_values(ss, &r->neighbours[k], 0, r, inherit ? &smcc : NULL, inherit ? &smnc : NULL, &nb[k]);
        if (nb[k].ok && serving.ok && nb[k].hz == serving.hz)
            nb[k].ok = 0; /* the serving cell repeated in the list */
    }
    yr_time t;
    t.monotonic_ns = pei_monotonic_ns(d);
    t.utc_ns = d->plugin->host->utc_now_ns(d->plugin->host->host);
    t.utc_source = YR_TIME_SOURCE_SYSTEM;
    t.reserved = 0;

    pthread_mutex_lock(&d->mutex);
    pei_status* st = &d->status;
    st->csq_code = r->has_csq ? r->csq.rssi_code : 99;
    st->registration = r->has_reg ? (r->reg.stat <= 5 ? r->reg.stat : 4) : 4;
    if (r->has_reg && r->reg.has_la)
        pei_format(st->la, sizeof st->la, "%u", (unsigned)r->reg.la);
    else
        st->la[0] = '\0';
    if (serving.ok)
        pei_format(st->serving, sizeof st->serving, "%.4f MHz, %.0f dBm%s", (double)serving.hz / 1e6,
                 serving.v[YR_CORE_LEVEL].u.f64, (serving.flags & YR_RECORD_NO_SYNC) ? ", identity incomplete" : "");
    else
        st->serving[0] = '\0';
    st->neighbours = 0;
    for (unsigned k = 0; k < r->neighbour_count; ++k)
        st->neighbours += nb[k].ok ? 1u : 0u;
    ++st->polls;
    if (d->state == YR_DEVICE_STREAMING && !atomic_load(&d->stop) && d->sink != NULL) {
        const yr_host_api* h = d->plugin->host;
        if (serving.ok) {
            if (!ss->have_serving)
                h->stream_event(d->sink, YR_EVENT_SYNC_FOUND, (int64_t)serving.hz);
            else if (ss->serving_hz != serving.hz)
                h->stream_event(d->sink, YR_EVENT_SERVING_CHANGED, (int64_t)serving.hz);
            ss->have_serving = 1;
            ss->serving_hz = serving.hz;
            push_locked(ss, &serving, &t);
        } else if (ss->have_serving) {
            h->stream_event(d->sink, YR_EVENT_SYNC_LOST, (int64_t)ss->serving_hz);
            ss->have_serving = 0;
        }
        for (unsigned k = 0; k < r->neighbour_count; ++k)
            if (nb[k].ok)
                push_locked(ss, &nb[k], &t);
    }
    pthread_mutex_unlock(&d->mutex);
    free(nb);
}

/* One poll (TRM-01): EX_DONE, or EX_TIMEOUT/EX_IO/EX_STOP. */
static int poll_once(session* ss, int neighbours_wanted)
{
    reading* r = (reading*)calloc(1, sizeof *r);
    if (r == NULL)
        return EX_DONE;
    int res = EX_DONE;
    if (run_cmd(ss, CMD_CSQ, "AT+CSQ", "+CSQ:", &res))
        for (unsigned k = 0; k < ss->parser.reply.line_count; ++k)
            if (pei_parse_csq(ss->parser.reply.lines[k], &r->csq))
                r->has_csq = 1;
    if (res == EX_DONE && run_cmd(ss, CMD_CREG, "AT+CREG?", "+CREG:", &res))
        for (unsigned k = 0; k < ss->parser.reply.line_count; ++k)
            if (pei_parse_creg(ss->parser.reply.lines[k], ss->s.creg_with_mode, &r->reg))
                r->has_reg = 1;
    if (res == EX_DONE && ss->serving_count > 0 &&
        run_cmd(ss, CMD_SERVING, ss->s.serving_command, ss->serving_prefix, &res))
        for (unsigned k = 0; k < ss->parser.reply.line_count && !r->has_serving; ++k)
            r->has_serving = pei_parse_cell(ss->parser.reply.lines[k], ss->serving_prefix, ss->serving_fields,
                                            (unsigned)ss->serving_count, &r->serving);
    if (res == EX_DONE && neighbours_wanted && ss->neighbour_count > 0 &&
        run_cmd(ss, CMD_NEIGHBOURS, ss->s.neighbour_command, ss->neighbour_prefix, &res))
        for (unsigned k = 0; k < ss->parser.reply.line_count && r->neighbour_count < PEI_MAX_LINES; ++k)
            if (pei_parse_cell(ss->parser.reply.lines[k], ss->neighbour_prefix, ss->neighbour_fields,
                               (unsigned)ss->neighbour_count, &r->neighbours[r->neighbour_count]))
                ++r->neighbour_count;
    if (res == EX_DONE)
        deliver(ss, r);
    free(r);
    return res;
}

/* ------------------------------------------------------------ stream thread */

static void wait_ms(session* ss, int ms)
{
    pthread_mutex_lock(&ss->d->mutex);
    const int64_t end = now_ms() + ms;
    while (!atomic_load(ss->stop)) {
        const int64_t left = end - now_ms();
        if (left <= 0)
            break;
        pei_wait_locked(ss->d, (int)left);
    }
    pthread_mutex_unlock(&ss->d->mutex);
}

static void reset_connection(session* ss)
{
    memset(ss->refused, 0, sizeof ss->refused);
    pthread_mutex_lock(&ss->d->mutex);
    ss->d->status.unsupported[0] = '\0';
    pthread_mutex_unlock(&ss->d->mutex);
}

/* Opens the session's port; a port another program holds is marked for
 * enumerate's IN_USE, a port that opens loses its mark (RAD-80). Called
 * without the device mutex. */
static pei_port* open_port(session* ss, char* err, size_t err_size)
{
    int in_use = 0;
    pei_port* port = pei_port_open(ss->s.port, ss->s.baud, &in_use, err, err_size);
    if (port != NULL || in_use)
        pei_mark_port(ss->d->plugin, ss->s.port, in_use, err);
    return port;
}

static void* stream_main(void* arg)
{
    session* ss = (session*)arg;
    yr_device* d = ss->d;
    pthread_mutex_lock(&d->mutex);
    const int neighbours_wanted = (d->plan.flags & YR_PLAN_MEASURE_NEIGHBOURS) != 0;
    pthread_mutex_unlock(&d->mutex);
    int identified = 0;
    while (!atomic_load(ss->stop)) {
        if (ss->port == NULL) {
            char err[PEI_TEXT_BYTES];
            ss->port = open_port(ss, err, sizeof err);
            if (ss->port == NULL) {
                set_terminal(ss, PEI_TERMINAL_CONNECTING, err);
                wait_ms(ss, RETRY_MS);
                continue;
            }
            pei_log(d, YR_LOG_INFO, "port %s open again", ss->s.port);
            identified = 0;
            reset_connection(ss);
        }
        settle(ss);
        int r = wake(ss);
        if (r == EX_TIMEOUT) {
            set_terminal(ss, PEI_TERMINAL_NO_REPLY,
                         "the terminal does not answer AT commands: check the PEI cable, the port and the baud rate");
            wait_ms(ss, RETRY_MS);
            continue;
        }
        if (r == EX_DONE && !identified) {
            int missing = 0;
            r = identify(ss, &missing);
            identified = r == EX_DONE;
        }
        if (r == EX_DONE) {
            set_terminal(ss, PEI_TERMINAL_READY, NULL);
            set_problem(ss, ss->config_problem);
        }
        while (r == EX_DONE && !atomic_load(ss->stop)) {
            const int64_t t0 = now_ms();
            r = poll_once(ss, neighbours_wanted);
            if (r == EX_DONE) {
                const int64_t left = t0 + POLL_INTERVAL_MS - now_ms();
                if (left > 0)
                    wait_ms(ss, (int)left);
            }
        }
        if (r == EX_TIMEOUT) {
            set_terminal(ss, PEI_TERMINAL_NO_REPLY, "the terminal stopped answering; waking it again (ATE0)");
        } else if (r == EX_IO) {
            char why[PEI_TEXT_BYTES];
            pei_format(why, sizeof why, "port I/O error: %s", pei_port_error(ss->port));
            pei_log(d, YR_LOG_WARNING, "%s; reopening %s", why, ss->s.port);
            set_terminal(ss, PEI_TERMINAL_CONNECTING, why);
            pei_port_close(ss->port);
            ss->port = NULL;
            wait_ms(ss, RETRY_MS / 4);
        }
    }
    pei_port_close(ss->port);
    free(ss);
    return NULL;
}

yr_status pei_session_start(yr_device* d, yr_stream* sink)
{
    session* ss = (session*)calloc(1, sizeof *ss);
    if (ss == NULL)
        return YR_ERR_NO_MEMORY;
    ss->d = d;
    ss->stop = &d->stop;
    pei_parser_init(&ss->parser);
    pthread_mutex_lock(&d->mutex);
    ss->s = d->settings;
    pthread_mutex_unlock(&d->mutex);
    char err[PEI_TEXT_BYTES];
    ss->port = open_port(ss, err, sizeof err);
    if (ss->port == NULL) {
        pthread_mutex_lock(&d->mutex);
        pei_format(d->last_error, sizeof d->last_error, "%s", err);
        pthread_mutex_unlock(&d->mutex);
        free(ss);
        return YR_ERR_IO;
    }
    pthread_mutex_lock(&d->mutex);
    atomic_store(&d->stop, 0);
    d->sink = sink;
    d->state = YR_DEVICE_STREAMING;
    memset(&d->stats, 0, sizeof d->stats);
    d->stats.struct_size = sizeof d->stats;
    d->stats.receiver_load = NAN;
    d->stats.sample_rate_hz = NAN;
    d->status.terminal = PEI_TERMINAL_CONNECTING;
    d->status.polls = 0;
    d->status.timeouts = 0;
    d->status.unsupported[0] = '\0';
    d->status.problem[0] = '\0';
    pei_format(d->watch_port, sizeof d->watch_port, "%s", ss->s.port);
    d->last_error[0] = '\0';
    pthread_mutex_unlock(&d->mutex);
    setup_commands(ss);
    pei_log(d, YR_LOG_INFO, "port %s open at %d baud", ss->s.port, ss->s.baud);
    if (pthread_create(&d->thread, NULL, stream_main, ss) != 0) {
        pei_port_close(ss->port);
        free(ss);
        pthread_mutex_lock(&d->mutex);
        d->state = YR_DEVICE_OPEN;
        d->sink = NULL;
        pei_format(d->last_error, sizeof d->last_error, "cannot start the stream thread");
        pthread_mutex_unlock(&d->mutex);
        return YR_ERR_FAILED;
    }
    pthread_mutex_lock(&d->mutex);
    d->thread_running = 1;
    pthread_mutex_unlock(&d->mutex);
    return YR_OK;
}

void pei_session_join(yr_device* d)
{
    pthread_mutex_lock(&d->mutex);
    const int running = d->thread_running;
    d->thread_running = 0;
    atomic_store(&d->stop, 1);
    pthread_cond_broadcast(&d->wake);
    pthread_mutex_unlock(&d->mutex);
    if (running) {
        pthread_join(d->thread, NULL); /* stream_main frees its session */
    }
}

/* ------------------------------------------------------------ self-test */

static void item(yr_selftest_cb cb, void* user, uint32_t result, const char* name, const char* label,
                 const char* detail, const char* remedy)
{
    yr_selftest_item it;
    memset(&it, 0, sizeof it);
    it.struct_size = sizeof it;
    it.result = result;
    it.name = name;
    it.label = label;
    it.detail = detail;
    it.remedy = remedy;
    cb(user, &it);
}

yr_status pei_session_self_test(yr_device* d, yr_selftest_cb cb, void* user)
{
    session* ss = (session*)calloc(1, sizeof *ss);
    if (ss == NULL)
        return YR_ERR_NO_MEMORY;
    static atomic_int never; /* the self-test is not stopped from outside */
    ss->d = d;
    ss->stop = &never;
    pei_parser_init(&ss->parser);
    pthread_mutex_lock(&d->mutex);
    ss->s = d->settings;
    pthread_mutex_unlock(&d->mutex);
    if (ss->s.reply_timeout_ms > 2000)
        ss->s.reply_timeout_ms = 2000; /* within the 10 s of RAD-46 */
    char detail[PEI_TEXT_BYTES * 2];
    char err[PEI_TEXT_BYTES];
    const char* rest_skipped = "skipped: no connection to the terminal";
    static const char* const names[] = {"identity", "level", "registration", "serving_cell", "neighbour_cells"};
    static const char* const labels[] = {"Radio info (AT+GMI, AT+GMM, AT+GMR, AT+GSN)", "Level (AT+CSQ)",
                                         "Registration (AT+CREG?)", "Serving cell command",
                                         "Neighbour cells command"};
    ss->port = open_port(ss, err, sizeof err);
    if (ss->port == NULL) {
        item(cb, user, YR_TEST_FAIL, "port", "Serial port opens", err,
             "check the port setting, that the cable is plugged in and that you may use the port");
        item(cb, user, YR_TEST_SKIPPED, "at_reply", "The terminal answers AT commands", rest_skipped, "");
        for (int i = 0; i < 5; ++i)
            item(cb, user, YR_TEST_SKIPPED, names[i], labels[i], rest_skipped, "");
        free(ss);
        return YR_OK;
    }
    pei_format(detail, sizeof detail, "%s at %d baud", ss->s.port, ss->s.baud);
    item(cb, user, YR_TEST_PASS, "port", "Serial port opens", detail, "");
    settle(ss);
    const int awake = wake(ss) == EX_DONE;
    item(cb, user, awake ? YR_TEST_PASS : YR_TEST_FAIL, "at_reply", "The terminal answers AT commands",
         awake ? "ATE0: OK" : "no OK to ATE0",
         awake ? "" : "switch the terminal on, check the PEI cable and the baud rate (often 38400 or 115200)");
    if (!awake) {
        for (int i = 0; i < 5; ++i)
            item(cb, user, YR_TEST_SKIPPED, names[i], labels[i], rest_skipped, "");
        pei_port_close(ss->port);
        free(ss);
        return YR_OK;
    }
    int missing = 0;
    identify(ss, &missing);
    pthread_mutex_lock(&d->mutex);
    pei_format(detail, sizeof detail, "%s %s, firmware %s, serial %s", d->status.manufacturer, d->status.model,
             d->status.firmware, d->status.tei);
    pthread_mutex_unlock(&d->mutex);
    item(cb, user, missing == 0 ? YR_TEST_PASS : YR_TEST_WARN, names[0], labels[0], detail,
         missing == 0 ? "" : "the terminal does not report every value; the measurements still work");

    int res = EX_DONE;
    pei_csq q;
    int has_q = 0;
    if (run_cmd(ss, CMD_CSQ, "AT+CSQ", "+CSQ:", &res))
        for (unsigned k = 0; k < ss->parser.reply.line_count; ++k)
            has_q |= pei_parse_csq(ss->parser.reply.lines[k], &q);
    double dbm = 0.0;
    if (has_q && pei_csq_to_dbm(q.rssi_code, &dbm)) {
        pei_format(detail, sizeof detail, "%.0f dBm (code %d)", dbm, q.rssi_code);
        item(cb, user, YR_TEST_PASS, names[1], labels[1], detail, "");
    } else {
        item(cb, user, YR_TEST_WARN, names[1], labels[1], has_q ? "level not known (99)" : "no AT+CSQ reading",
             "the terminal may have no service here; the cell commands may still give levels");
    }
    pei_registration reg;
    int has_reg = 0;
    if (res == EX_DONE && run_cmd(ss, CMD_CREG, "AT+CREG?", "+CREG:", &res))
        for (unsigned k = 0; k < ss->parser.reply.line_count; ++k)
            has_reg |= pei_parse_creg(ss->parser.reply.lines[k], ss->s.creg_with_mode, &reg);
    if (has_reg) {
        pei_format(detail, sizeof detail, "%s", pei_registration_name(reg.stat));
        item(cb, user, reg.stat == 1 || reg.stat == 5 ? YR_TEST_PASS : YR_TEST_WARN, names[2], labels[2], detail,
             reg.stat == 1 || reg.stat == 5 ? "" : "the terminal is not registered: check its subscription and coverage");
    } else {
        item(cb, user, YR_TEST_WARN, names[2], labels[2], "no AT+CREG? reading",
             "turn \"AT+CREG with mode\" on if the terminal answers in the TS 27.007 form");
    }
    setup_commands(ss);
    for (int which = 0; which < 2; ++which) {
        const int count = which == 0 ? ss->serving_count : ss->neighbour_count;
        const char* cmd = which == 0 ? ss->s.serving_command : ss->s.neighbour_command;
        const char* prefix = which == 0 ? ss->serving_prefix : ss->neighbour_prefix;
        const pei_field* fields = which == 0 ? ss->serving_fields : ss->neighbour_fields;
        const char* name = names[3 + which];
        const char* label = labels[3 + which];
        if (ss->s.profile != PEI_PROFILE_CELL_LIST) {
            item(cb, user, YR_TEST_SKIPPED, name, label, "the EN 300 392-5 profile has no cell commands", "");
            continue;
        }
        if (count <= 0) {
            item(cb, user, which == 0 ? YR_TEST_FAIL : YR_TEST_WARN, name, label, "the command or its values are not usable",
                 "set the command and its values from the terminal's PEI manual");
            continue;
        }
        int cells = 0;
        pei_cell cell;
        if (res == EX_DONE && run_cmd(ss, which == 0 ? CMD_SERVING : CMD_NEIGHBOURS, cmd, prefix, &res))
            for (unsigned k = 0; k < ss->parser.reply.line_count; ++k)
                cells += pei_parse_cell(ss->parser.reply.lines[k], prefix, fields, (unsigned)count, &cell);
        if (cells > 0) {
            pei_format(detail, sizeof detail, "%s: %d cell(s)", cmd, cells);
            item(cb, user, YR_TEST_PASS, name, label, detail, "");
        } else {
            pei_format(detail, sizeof detail, "%s: %s", cmd,
                     ss->refused[which == 0 ? CMD_SERVING : CMD_NEIGHBOURS] ? ss->parser.reply.error
                     : res != EX_DONE                                      ? "no reply"
                                                                           : "no cell in the reply");
            item(cb, user, which == 0 ? YR_TEST_FAIL : YR_TEST_WARN, name, label, detail,
                 "set the command and its values from the terminal's PEI manual (it may also have no service)");
        }
    }
    pei_port_close(ss->port);
    free(ss);
    return YR_OK;
}
