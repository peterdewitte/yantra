/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Serial ports (RAD-80); see pei_port.h. What the POSIX and the Win32
 * implementations share, and the pure helpers (every platform, so both
 * test trees check them).
 */
#include "pei_port.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int kBauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};

int pei_baud_supported(int baud)
{
    for (size_t i = 0; i < sizeof kBauds / sizeof kBauds[0]; ++i)
        if (kBauds[i] == baud)
            return 1;
    return 0;
}

static int starts_with_nocase(const char* s, const char* prefix)
{
    for (; *prefix != '\0'; ++s, ++prefix)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
    return 1;
}

/* COMn with n 1..256 and nothing after it; the number, else 0. */
static int com_number(const char* s)
{
    if (!starts_with_nocase(s, "COM"))
        return 0;
    s += 3;
    if (*s < '1' || *s > '9')
        return 0;
    int n = 0;
    for (; *s != '\0'; ++s) {
        if (!isdigit((unsigned char)*s) || n > 256)
            return 0;
        n = n * 10 + (*s - '0');
    }
    return n >= 1 && n <= 256 ? n : 0;
}

int pei_port_win32_name(const char* path, char* out, size_t size)
{
    if (path == NULL)
        path = "";
    if (starts_with_nocase(path, "\\\\.\\pipe\\") && path[9] != '\0') {
        snprintf(out, size, "%s", path);
        return PEI_PORT_KIND_PIPE;
    }
    const char* name = starts_with_nocase(path, "\\\\.\\") ? path + 4 : path;
    const int n = com_number(name);
    if (n > 0) {
        snprintf(out, size, "\\\\.\\COM%d", n);
        return PEI_PORT_KIND_COM;
    }
    snprintf(out, size, "%s", path);
    return PEI_PORT_KIND_OTHER;
}

/* `count` hex digits at s; -1 when they are not there. */
static long hex_at(const char* s, int count)
{
    long v = 0;
    for (int i = 0; i < count; ++i) {
        const int c = (unsigned char)s[i];
        if (!isxdigit(c))
            return -1;
        v = v * 16 + (isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
    }
    return v;
}

static const char* find_nocase(const char* s, const char* what)
{
    for (; *s != '\0'; ++s)
        if (starts_with_nocase(s, what))
            return s;
    return NULL;
}

int pei_port_parse_usb_ids(const char* id, unsigned* vendor_id, unsigned* product_id, char* serial,
                           size_t serial_size)
{
    unsigned vid = 0, pid = 0;
    if (serial_size > 0)
        serial[0] = '\0';
    if (id == NULL)
        id = "";
    const char* v = find_nocase(id, "VID_");
    const char* p = find_nocase(id, "PID_");
    const long vv = v != NULL ? hex_at(v + 4, 4) : -1;
    const long pv = p != NULL ? hex_at(p + 4, 4) : -1;
    if (vv >= 0)
        vid = (unsigned)vv;
    if (pv >= 0)
        pid = (unsigned)pv;
    if (vendor_id != NULL)
        *vendor_id = vid;
    if (product_id != NULL)
        *product_id = pid;
    if (vv < 0 || pv < 0)
        return 0;
    const char* after = p + 8; /* "PID_xxxx" */
    if (starts_with_nocase(id, "FTDIBUS\\") && *after == '+') {
        /* FTDIBUS\VID_0403+PID_6001+<serial><port letter>\0000 */
        const char* s = after + 1;
        size_t len = strcspn(s, "\\");
        if (len > 1)
            --len; /* the FTDI driver appends the port letter (A, B, ...) */
        if (len > 0 && serial_size > 0)
            snprintf(serial, serial_size, "%.*s", (int)len, s);
    } else if (starts_with_nocase(id, "USB\\") && *after == '\\') {
        /* USB\VID_xxxx&PID_yyyy\<serial>; a made-up instance id of a
         * device without a serial number has '&' in it. */
        const char* s = after + 1;
        if (*s != '\0' && strchr(s, '&') == NULL && strchr(s, '\\') == NULL && serial_size > 0)
            snprintf(serial, serial_size, "%s", s);
    }
    return 1;
}

void pei_port_split_list(const char* list, void (*cb)(void* user, const char* path), void* user)
{
    if (list == NULL)
        return;
    const char* p = list;
    while (*p != '\0') {
        const char* end = strchr(p, PEI_PORTS_SEPARATOR);
        const size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        if (len > 0 && len < PEI_PORT_PATH_MAX) {
            char path[PEI_PORT_PATH_MAX];
            memcpy(path, p, len);
            path[len] = '\0';
            cb(user, path);
        }
        if (end == NULL)
            break;
        p = end + 1;
    }
}
