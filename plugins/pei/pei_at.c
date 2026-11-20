/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PEI AT command parsing (RAD-80, TRM-01, TRM-04); see pei_at.h.
 * Copied from yantra-v1 c24e8f5: core/terminal/pei.cpp, rewritten in C.
 */
#include "pei_at.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ helpers */

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static char upper_char(char c)
{
    return (char)toupper((unsigned char)c);
}

/* Copies at most out_size - 1 bytes of s[0..n) into out. */
static void copy_n(char* out, size_t out_size, const char* s, size_t n)
{
    if (out_size == 0)
        return;
    if (n >= out_size)
        n = out_size - 1;
    memcpy(out, s, n);
    out[n] = '\0';
}

/* [*b, *e) trimmed of blanks. */
static void trim_range(const char** b, const char** e)
{
    while (*b < *e && is_space(**b))
        ++*b;
    while (*e > *b && is_space((*e)[-1]))
        --*e;
}

/* [*b, *e) trimmed and without surrounding quotes. */
static void unquote_range(const char** b, const char** e)
{
    trim_range(b, e);
    if (*e - *b >= 2 && **b == '"' && (*e)[-1] == '"') {
        ++*b;
        --*e;
    }
}

/* Case-insensitive: does s start with prefix? */
static int starts_with_ci(const char* s, const char* prefix)
{
    for (; *prefix != '\0'; ++s, ++prefix)
        if (*s == '\0' || upper_char(*s) != upper_char(*prefix))
            return 0;
    return 1;
}

static int equal_ci(const char* a, const char* b)
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b)
        if (upper_char(*a) != upper_char(*b))
            return 0;
    return *a == *b;
}

/* The text after `prefix` (blanks skipped), or NULL when the line does not
 * start with it. An empty prefix returns the line. */
static const char* after_prefix(const char* line, const char* prefix)
{
    if (prefix == NULL || prefix[0] == '\0')
        return line;
    if (!starts_with_ci(line, prefix))
        return NULL;
    line += strlen(prefix);
    while (is_space(*line))
        ++line;
    return line;
}

/* A field of a comma-separated list: [begin, end). Commas inside quotes
 * do not split. Returns the number of fields found (at most max). */
typedef struct span {
    const char* b;
    const char* e;
} span;

static unsigned split_fields(const char* s, span* out, unsigned max)
{
    unsigned n = 0;
    int quoted = 0;
    const char* start = s;
    for (const char* p = s;; ++p) {
        if (*p == '"')
            quoted = !quoted;
        if (*p == '\0' || (*p == ',' && !quoted)) {
            if (n < max) {
                out[n].b = start;
                out[n].e = p;
                unquote_range(&out[n].b, &out[n].e);
                ++n;
            }
            if (*p == '\0')
                break;
            start = p + 1;
        }
    }
    return n;
}

/* Integer of [b, e): decimal (optional sign) or 0x-hex; nonzero on success. */
static int to_long(const char* b, const char* e, long* out)
{
    char tmp[32];
    trim_range(&b, &e);
    if (b == e || (size_t)(e - b) >= sizeof tmp)
        return 0;
    memcpy(tmp, b, (size_t)(e - b));
    tmp[e - b] = '\0';
    const int hex = tmp[0] == '0' && (tmp[1] == 'x' || tmp[1] == 'X') && tmp[2] != '\0';
    if (!hex && !(isdigit((unsigned char)tmp[0]) || tmp[0] == '-' || tmp[0] == '+'))
        return 0;
    char* end = NULL;
    errno = 0;
    const long v = strtol(tmp, &end, hex ? 16 : 10);
    if (errno != 0 || end == tmp || *end != '\0')
        return 0;
    *out = v;
    return 1;
}

/* Finite double of [b, e) (no hex, inf or nan); nonzero on success. */
static int to_double(const char* b, const char* e, double* out)
{
    char tmp[32];
    trim_range(&b, &e);
    if (b == e || (size_t)(e - b) >= sizeof tmp)
        return 0;
    memcpy(tmp, b, (size_t)(e - b));
    tmp[e - b] = '\0';
    for (const char* p = tmp; *p != '\0'; ++p)
        if (!(isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.'))
            return 0;
    char* end = NULL;
    errno = 0;
    const double v = strtod(tmp, &end);
    if (errno != 0 || end == tmp || *end != '\0' || !isfinite(v))
        return 0;
    *out = v;
    return 1;
}

static int to_uint_range(const char* b, const char* e, long lo, long hi, uint32_t* out)
{
    long v = 0;
    if (!to_long(b, e, &v) || v < lo || v > hi)
        return 0;
    *out = (uint32_t)v;
    return 1;
}

/* ------------------------------------------------------------ parser */

void pei_parser_init(pei_parser* p)
{
    memset(p, 0, sizeof *p);
}

void pei_parser_begin(pei_parser* p, const char* command, const char* expect_prefix)
{
    const char* b = command != NULL ? command : "";
    const char* e = b + strlen(b);
    trim_range(&b, &e);
    copy_n(p->command, sizeof p->command, b, (size_t)(e - b));
    copy_n(p->prefix, sizeof p->prefix, expect_prefix != NULL ? expect_prefix : "",
           expect_prefix != NULL ? strlen(expect_prefix) : 0);
    memset(&p->reply, 0, sizeof p->reply);
    p->buf_len = 0;
    p->overlong = 0;
    p->active = 1;
}

static int is_final_error(const char* l)
{
    return equal_ci(l, "ERROR") || starts_with_ci(l, "+CME ERROR") || starts_with_ci(l, "+CMS ERROR") ||
           equal_ci(l, "NO CARRIER") || equal_ci(l, "COMMAND NOT SUPPORT");
}

static void parser_line(pei_parser* p, const char* l)
{
    pei_reply* r = &p->reply;
    if (!p->active || r->done) {
        if (l[0] == '+')
            ++p->unsolicited;
        return;
    }
    if (p->command[0] != '\0' && equal_ci(l, p->command))
        return; /* echo */
    if (equal_ci(l, "OK")) {
        r->done = r->ok = 1;
        p->active = 0;
        return;
    }
    if (is_final_error(l)) {
        r->done = 1;
        r->ok = 0;
        copy_n(r->error, sizeof r->error, l, strlen(l));
        p->active = 0;
        return;
    }
    if (l[0] == '+') {
        /* A command without its own prefix (ATE0, AT+GMI as "+GMI: x" is
         * still its own): '+' lines that do not match are unsolicited. */
        if (p->prefix[0] != '\0' ? !starts_with_ci(l, p->prefix) : strchr(p->command, '+') == NULL) {
            ++p->unsolicited;
            return;
        }
    }
    if (r->line_count < PEI_MAX_LINES) {
        copy_n(r->lines[r->line_count], PEI_LINE_BYTES, l, strlen(l));
        ++r->line_count;
    } else {
        ++r->lines_dropped;
    }
}

int pei_parser_feed(pei_parser* p, const char* data, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const char c = data[i];
        if (c == '\r' || c == '\n') {
            p->buf[p->buf_len] = '\0';
            const char* b = p->buf;
            const char* e = p->buf + p->buf_len;
            trim_range(&b, &e);
            if (b != e) {
                char line[PEI_LINE_BYTES];
                copy_n(line, sizeof line, b, (size_t)(e - b));
                parser_line(p, line);
            }
            p->buf_len = 0;
            p->overlong = 0;
            continue;
        }
        if (c == '\0')
            continue; /* line noise */
        if (p->buf_len + 1 < sizeof p->buf) {
            p->buf[p->buf_len++] = c;
        } else if (!p->overlong) {
            p->overlong = 1;
            ++p->lines_cut;
        }
    }
    return p->reply.done;
}

size_t pei_reply_prefix(const char* command, char* out, size_t out_size)
{
    if (out_size == 0)
        return 0;
    out[0] = '\0';
    if (command == NULL)
        return 0;
    const char* b = command;
    const char* e = b + strlen(b);
    trim_range(&b, &e);
    if (e - b >= 2 && upper_char(b[0]) == 'A' && upper_char(b[1]) == 'T')
        b += 2;
    if (b == e || (*b != '+' && *b != '$' && *b != '%' && *b != '^'))
        return 0;
    const char* p = b + 1;
    while (p < e && (isalnum((unsigned char)*p) || *p == '_'))
        ++p;
    size_t len = (size_t)(p - b);
    if (len + 2 > out_size)
        return 0;
    for (size_t i = 0; i < len; ++i)
        out[i] = upper_char(b[i]);
    out[len] = ':';
    out[len + 1] = '\0';
    return len + 1;
}

void pei_info_text(const pei_reply* r, const char* prefix, char* out, size_t out_size)
{
    if (out_size == 0)
        return;
    out[0] = '\0';
    if (!r->ok)
        return;
    size_t used = 0;
    for (unsigned k = 0; k < r->line_count; ++k) {
        const char* t = r->lines[k];
        int printable = 1;
        for (const unsigned char* c = (const unsigned char*)t; *c != '\0'; ++c)
            if (*c < 0x20 || *c >= 0x7f)
                printable = 0;
        if (!printable)
            continue; /* line noise */
        const char* a = prefix != NULL && prefix[0] != '\0' ? after_prefix(t, prefix) : NULL;
        if (a != NULL)
            t = a;
        else if (t[0] == '+')
            continue; /* an unsolicited result code, not the identification */
        const char* b = t;
        const char* e = t + strlen(t);
        unquote_range(&b, &e);
        if (b == e)
            continue;
        if (used > 0 && used + 1 < out_size)
            out[used++] = ' ';
        size_t n = (size_t)(e - b);
        if (used + n >= out_size)
            n = out_size - 1 - used;
        memcpy(out + used, b, n);
        used += n;
        out[used] = '\0';
        if (used + 1 >= out_size)
            break;
    }
}

/* ------------------------------------------------------------ readings */

int pei_csq_to_dbm(int code, double* dbm)
{
    if (code < 0 || code > 31)
        return 0;
    *dbm = -113.0 + 2.0 * code;
    return 1;
}

int pei_parse_csq(const char* line, pei_csq* out)
{
    const char* rest = after_prefix(line, "+CSQ:");
    if (rest == NULL)
        return 0;
    span f[3];
    const unsigned n = split_fields(rest, f, 3);
    long rssi = 0;
    if (n == 0 || !to_long(f[0].b, f[0].e, &rssi) || rssi < 0 || rssi > 99)
        return 0;
    out->rssi_code = (int)rssi;
    out->ber_code = 99;
    long ber = 0;
    if (n > 1 && to_long(f[1].b, f[1].e, &ber) && ber >= 0 && ber <= 99)
        out->ber_code = (int)ber;
    return 1;
}

const char* pei_registration_name(int stat)
{
    switch (stat) {
    case 0: return "not registered";
    case 1: return "registered";
    case 2: return "searching";
    case 3: return "registration denied";
    case 5: return "registered (roaming)";
    default: return "unknown";
    }
}

/* MNI: "<mcc>-<mnc>", "<mcc> <mnc>" or digits (first three = MCC). */
static int parse_mni(const char* b, const char* e, uint32_t* mcc, uint32_t* mnc)
{
    trim_range(&b, &e);
    const char* sep = NULL;
    for (const char* p = b; p < e; ++p) {
        if (*p == '-' || *p == ' ') {
            sep = p;
            break;
        }
        if (!isdigit((unsigned char)*p))
            return 0;
    }
    if (sep != NULL)
        return to_uint_range(b, sep, 0, 1023, mcc) && to_uint_range(sep + 1, e, 0, 16383, mnc);
    if (e - b < 4 || e - b > 8)
        return 0;
    return to_uint_range(b, b + 3, 0, 1023, mcc) && to_uint_range(b + 3, e, 0, 16383, mnc);
}

int pei_parse_creg(const char* line, int with_mode, pei_registration* out)
{
    const char* rest = after_prefix(line, "+CREG:");
    if (rest == NULL)
        return 0;
    span f[5];
    unsigned n = split_fields(rest, f, 5);
    unsigned i = 0;
    if (with_mode) {
        if (n < 2)
            return 0;
        i = 1;
    }
    long stat = 0;
    if (i >= n || !to_long(f[i].b, f[i].e, &stat) || stat < 0 || stat > 9)
        return 0;
    memset(out, 0, sizeof *out);
    out->stat = (int)stat;
    if (i + 1 < n)
        out->has_la = to_uint_range(f[i + 1].b, f[i + 1].e, 0, 16383, &out->la);
    if (i + 2 < n)
        out->has_network = parse_mni(f[i + 2].b, f[i + 2].e, &out->mcc, &out->mnc);
    return 1;
}

/* ------------------------------------------------------------ cells */

static const char* const field_names[PEI_FIELD_COUNT_] = {
    "-", "carrier", "band", "offset", "freq_hz", "freq_mhz", "mcc",
    "mnc", "la", "cc", "rssi", "csq", "c1", "state"};

const char* pei_field_name(pei_field f)
{
    if ((int)f < 0 || f >= PEI_FIELD_COUNT_)
        return "?";
    return field_names[f];
}

int pei_parse_fields(const char* text, pei_field* fields, unsigned max_fields, char* error,
                     size_t error_size)
{
    if (error_size > 0)
        error[0] = '\0';
    span f[PEI_MAX_FIELDS + 1];
    const unsigned n = split_fields(text != NULL ? text : "", f, PEI_MAX_FIELDS + 1);
    unsigned count = 0;
    for (unsigned k = 0; k < n; ++k) {
        if (f[k].b == f[k].e)
            continue;
        if (count >= max_fields || count >= PEI_MAX_FIELDS) {
            if (error_size > 0)
                copy_n(error, error_size, "too many fields", strlen("too many fields"));
            return -1;
        }
        char name[32];
        copy_n(name, sizeof name, f[k].b, (size_t)(f[k].e - f[k].b));
        int found = -1;
        for (int i = 0; i < (int)PEI_FIELD_COUNT_; ++i)
            if ((size_t)(f[k].e - f[k].b) < sizeof name && equal_ci(name, field_names[i]))
                found = i;
        if (found < 0) {
            if (error_size > 0)
                copy_n(error, error_size, f[k].b, (size_t)(f[k].e - f[k].b));
            return -1;
        }
        fields[count++] = (pei_field)found;
    }
    return (int)count;
}

int pei_parse_cell(const char* line, const char* prefix, const pei_field* fields,
                   unsigned field_count, pei_cell* out)
{
    memset(out, 0, sizeof *out);
    const char* rest = after_prefix(line, prefix);
    if (rest == NULL)
        return 0;
    span v[PEI_MAX_FIELDS];
    const unsigned n = split_fields(rest, v, PEI_MAX_FIELDS);
    int any = 0;
    for (unsigned k = 0; k < field_count && k < n; ++k) {
        const char* b = v[k].b;
        const char* e = v[k].e;
        double d = 0.0;
        uint32_t u = 0;
        long l = 0;
        switch (fields[k]) {
        case PEI_FIELD_CARRIER:
            if ((out->has_carrier = to_uint_range(b, e, 0, 4095, &out->carrier)) != 0)
                any = 1;
            break;
        case PEI_FIELD_BAND:
            if ((out->has_band = to_uint_range(b, e, 1, 9, &out->band)) != 0)
                any = 1;
            break;
        case PEI_FIELD_OFFSET:
            if ((out->has_offset = to_uint_range(b, e, 0, 3, &out->offset)) != 0)
                any = 1;
            break;
        case PEI_FIELD_FREQ_HZ:
            if (to_long(b, e, &l) && l >= 100000000L && l <= 1000000000L) {
                out->freq_hz = (uint64_t)l;
                out->has_freq = any = 1;
            }
            break;
        case PEI_FIELD_FREQ_MHZ:
            if (to_double(b, e, &d) && d >= 100.0 && d <= 1000.0) {
                out->freq_hz = (uint64_t)llround(d * 1e6);
                out->has_freq = any = 1;
            }
            break;
        case PEI_FIELD_MCC:
            if ((out->has_mcc = to_uint_range(b, e, 0, 1023, &out->mcc)) != 0)
                any = 1;
            break;
        case PEI_FIELD_MNC:
            if ((out->has_mnc = to_uint_range(b, e, 0, 16383, &out->mnc)) != 0)
                any = 1;
            break;
        case PEI_FIELD_LA:
            if ((out->has_la = to_uint_range(b, e, 0, 16383, &out->la)) != 0)
                any = 1;
            break;
        case PEI_FIELD_CC:
            if ((out->has_cc = to_uint_range(b, e, 0, 63, &out->cc)) != 0)
                any = 1;
            break;
        case PEI_FIELD_RSSI:
            if (to_double(b, e, &d) && d >= -160.0 && d <= 0.0) {
                out->rssi_dbm = d;
                out->has_rssi = any = 1;
            }
            break;
        case PEI_FIELD_CSQ:
            if (to_uint_range(b, e, 0, 99, &u) && pei_csq_to_dbm((int)u, &d)) {
                out->rssi_dbm = d;
                out->has_rssi = any = 1;
            }
            break;
        case PEI_FIELD_C1:
            if (to_double(b, e, &d) && d >= -200.0 && d <= 200.0) {
                out->c1_db = d;
                out->has_c1 = any = 1;
            }
            break;
        case PEI_FIELD_STATE:
            copy_n(out->state, sizeof out->state, b, (size_t)(e - b));
            if (out->state[0] != '\0')
                any = 1;
            break;
        case PEI_FIELD_SKIP:
        case PEI_FIELD_COUNT_:
        default:
            break;
        }
    }
    return any;
}

static int64_t offset_hz_of(uint32_t code)
{
    switch (code) {
    case 0: return 0;
    case 1: return 6250;
    case 2: return -6250;
    case 3: return 12500;
    default: return -1;
    }
}

uint64_t pei_carrier_hz(uint32_t band, uint32_t carrier, uint32_t offset_code)
{
    if (band < 1 || band > 9 || carrier > 4095 || offset_code > 3)
        return 0;
    return (uint64_t)((int64_t)band * 100000000 + (int64_t)carrier * 25000 + offset_hz_of(offset_code));
}

int pei_carrier_of(uint64_t freq_hz, uint32_t band, uint32_t offset_code, uint32_t* carrier)
{
    if (band < 1 || band > 9 || offset_code > 3)
        return 0;
    const int64_t rel = (int64_t)freq_hz - (int64_t)band * 100000000 - offset_hz_of(offset_code);
    if (rel < -12500)
        return 0;
    const int64_t c = (rel + 12500) / 25000;
    if (c < 0 || c > 4095)
        return 0;
    *carrier = (uint32_t)c;
    return 1;
}
