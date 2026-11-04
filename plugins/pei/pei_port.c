/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Serial ports (RAD-80); see pei_port.h. POSIX termios; the Windows path
 * is DEFERRED (Windows) and only compiles.
 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE /* cfmakeraw, realpath, B230400.. */
#endif

#include "pei_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_text(char* out, size_t size, const char* text)
{
    if (size == 0)
        return;
    snprintf(out, size, "%s", text);
}

static const int kBauds[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};

int pei_baud_supported(int baud)
{
    for (size_t i = 0; i < sizeof kBauds / sizeof kBauds[0]; ++i)
        if (kBauds[i] == baud)
            return 1;
    return 0;
}

#if defined(_WIN32)

/* DEFERRED (Windows): COM ports through CreateFile/SetCommState come with
 * the Windows build (RAD-131). */
struct pei_port {
    int unused;
};

pei_port* pei_port_open(const char* path, int baud, char* error, size_t error_size)
{
    (void)path;
    (void)baud;
    set_text(error, error_size, "serial ports are not supported on Windows yet");
    return NULL;
}
void pei_port_close(pei_port* port)
{
    (void)port;
}
long pei_port_read(pei_port* port, char* buf, size_t n, int timeout_ms)
{
    (void)port;
    (void)buf;
    (void)n;
    (void)timeout_ms;
    return -1;
}
int pei_port_write(pei_port* port, const char* buf, size_t n)
{
    (void)port;
    (void)buf;
    (void)n;
    return 0;
}
const char* pei_port_error(const pei_port* port)
{
    (void)port;
    return "serial ports are not supported on Windows yet";
}
void pei_port_flush_input(pei_port* port)
{
    (void)port;
}
int pei_port_check(const char* path, char* why, size_t why_size)
{
    (void)path;
    set_text(why, why_size, "serial ports are not supported on Windows yet");
    return 0;
}
int pei_port_list(const char* extra, void (*cb)(void* user, const char* path), void* user)
{
    (void)extra;
    (void)cb;
    (void)user;
    return 0;
}

#else /* POSIX */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

struct pei_port {
    int fd;
    int failed;
    char error[256];
};

static speed_t baud_code(int baud)
{
    switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
#ifdef B230400
    case 230400: return B230400;
#endif
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default: return B0;
    }
}

static int fail(pei_port* p, const char* what)
{
    p->failed = 1;
    snprintf(p->error, sizeof p->error, "%s: %s", what, strerror(errno));
    return -1;
}

pei_port* pei_port_open(const char* path, int baud, char* error, size_t error_size)
{
    const speed_t code = baud_code(baud);
    if (code == B0) {
        char t[64];
        snprintf(t, sizeof t, "unsupported baud rate %d", baud);
        set_text(error, error_size, t);
        return NULL;
    }
    const int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (error_size > 0)
            snprintf(error, error_size, "cannot open %s: %s", path, strerror(errno));
        return NULL;
    }
    struct termios t;
    memset(&t, 0, sizeof t);
    if (!isatty(fd) || tcgetattr(fd, &t) != 0) {
        if (error_size > 0)
            snprintf(error, error_size, "%s is not a serial port", path);
        close(fd);
        return NULL;
    }
    cfmakeraw(&t);
    t.c_cflag |= (tcflag_t)(CLOCAL | CREAD);
    t.c_cflag &= ~(tcflag_t)(CSTOPB | PARENB | CRTSCTS | CSIZE);
    t.c_cflag |= (tcflag_t)CS8;
    t.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (cfsetispeed(&t, code) != 0 || cfsetospeed(&t, code) != 0 || tcsetattr(fd, TCSANOW, &t) != 0) {
        if (error_size > 0)
            snprintf(error, error_size, "cannot configure %s: %s", path, strerror(errno));
        close(fd);
        return NULL;
    }
    tcflush(fd, TCIFLUSH);
    pei_port* p = (pei_port*)calloc(1, sizeof *p);
    if (p == NULL) {
        set_text(error, error_size, "out of memory");
        close(fd);
        return NULL;
    }
    p->fd = fd;
    return p;
}

void pei_port_close(pei_port* port)
{
    if (port == NULL)
        return;
    close(port->fd);
    free(port);
}

long pei_port_read(pei_port* port, char* buf, size_t n, int timeout_ms)
{
    if (port->failed)
        return -1;
    struct pollfd pfd;
    pfd.fd = port->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int r = poll(&pfd, 1, timeout_ms);
    if (r < 0)
        return errno == EINTR ? 0 : fail(port, "poll");
    if (r == 0)
        return 0;
    const ssize_t got = read(port->fd, buf, n);
    if (got > 0)
        return (long)got;
    if (got < 0 && (errno == EAGAIN || errno == EINTR))
        return 0;
    /* 0 or EIO: the device went away (a pulled USB adapter gives EOF, a
     * pseudo-terminal whose other side closed gives EIO). */
    if (got == 0) {
        port->failed = 1;
        snprintf(port->error, sizeof port->error, "the port was closed by the other end");
        return -1;
    }
    return fail(port, "read");
}

int pei_port_write(pei_port* port, const char* buf, size_t n)
{
    while (n > 0 && !port->failed) {
        const ssize_t w = write(port->fd, buf, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN) {
                struct pollfd pfd;
                pfd.fd = port->fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                if (poll(&pfd, 1, 1000) <= 0) {
                    port->failed = 1;
                    snprintf(port->error, sizeof port->error, "write timed out");
                    return 0;
                }
                continue;
            }
            fail(port, "write");
            return 0;
        }
        buf += w;
        n -= (size_t)w;
    }
    return !port->failed;
}

const char* pei_port_error(const pei_port* port)
{
    return port->error;
}

void pei_port_flush_input(pei_port* port)
{
    tcflush(port->fd, TCIFLUSH);
}

int pei_port_check(const char* path, char* why, size_t why_size)
{
    set_text(why, why_size, "");
    struct stat st;
    if (path == NULL || path[0] == '\0' || stat(path, &st) != 0)
        return 0;
    if (access(path, R_OK | W_OK) != 0) {
        if (why_size > 0)
            snprintf(why, why_size,
                     "no read/write access to %s: add the user to the group of the port (often "
                     "\"dialout\") and log in again",
                     path);
        return -1;
    }
    return 1;
}

#define PEI_MAX_PORTS 64

typedef struct port_list {
    char path[PEI_MAX_PORTS][PATH_MAX];
    char real[PEI_MAX_PORTS][PATH_MAX];
    char names[PEI_MAX_PORTS][256]; /* scratch of list_dir */
    int count;
} port_list;

static void list_add(port_list* l, const char* path)
{
    if (l->count >= PEI_MAX_PORTS)
        return;
    char real[PATH_MAX];
    if (realpath(path, real) == NULL)
        return;
    for (int i = 0; i < l->count; ++i)
        if (strcmp(l->real[i], real) == 0 || strcmp(l->path[i], path) == 0)
            return;
    snprintf(l->path[l->count], PATH_MAX, "%s", path);
    snprintf(l->real[l->count], PATH_MAX, "%s", real);
    ++l->count;
}

static int compare_names(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b);
}

/* Entries of `dir` (sorted) whose name starts with one of the prefixes
 * (NULL: every entry but dot files). */
static void list_dir(port_list* l, const char* dir, const char* const* prefixes)
{
    DIR* d = opendir(dir);
    if (d == NULL)
        return;
    char (*names)[256] = l->names; /* no static buffer: enumerate and the watcher list at once */
    int n = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL && n < PEI_MAX_PORTS) {
        if (e->d_name[0] == '.')
            continue;
        int match = prefixes == NULL;
        for (const char* const* p = prefixes; p != NULL && *p != NULL; ++p)
            if (strncmp(e->d_name, *p, strlen(*p)) == 0)
                match = 1;
        if (match)
            snprintf(names[n++], sizeof l->names[0], "%s", e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof l->names[0], compare_names);
    for (int i = 0; i < n; ++i) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        list_add(l, path);
    }
}

int pei_port_list(const char* extra, void (*cb)(void* user, const char* path), void* user)
{
    port_list* l = (port_list*)calloc(1, sizeof *l);
    if (l == NULL)
        return 0;
    static const char* const tty_prefixes[] = {"ttyUSB", "ttyACM", NULL};
    list_dir(l, "/dev/serial/by-id", NULL);
    list_dir(l, "/dev", tty_prefixes);
    if (extra != NULL) {
        const char* p = extra;
        while (*p != '\0') {
            const char* end = strchr(p, ':');
            const size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
            if (len > 0 && len < PATH_MAX) {
                char path[PATH_MAX];
                memcpy(path, p, len);
                path[len] = '\0';
                list_add(l, path);
            }
            if (end == NULL)
                break;
            p = end + 1;
        }
    }
    const int count = l->count;
    for (int i = 0; i < count; ++i)
        cb(user, l->path[i]);
    free(l);
    return count;
}

#endif
