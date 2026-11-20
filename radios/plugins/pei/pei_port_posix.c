/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Serial ports on POSIX (RAD-80, RAD-04): termios, poll(2), an exclusive
 * flock(2) so two programs never talk to one terminal at once, and the
 * USB ids of a port from sysfs. See pei_port.h.
 * Copied from yantra-v1 c24e8f5: core/gps/byte_stream_posix.cpp (serial
 * part), rewritten in C.
 */
#if !defined(_WIN32)

#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE /* cfmakeraw, realpath, flock, B230400.. */
#endif

#include "pei_port.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

struct pei_port {
    int fd;
    int failed;
    char error[256];
};

static void set_text(char* out, size_t size, const char* text)
{
    if (size > 0)
        snprintf(out, size, "%s", text);
}

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

static void in_use_text(char* error, size_t error_size, const char* path)
{
    if (error_size > 0)
        snprintf(error, error_size,
                 "%s is in use by another program (it holds the port for itself); close that program first",
                 path);
}

pei_port* pei_port_open(const char* path, int baud, int* in_use, char* error, size_t error_size)
{
    if (in_use != NULL)
        *in_use = 0;
    const speed_t code = baud_code(baud);
    if (code == B0) {
        char t[64];
        snprintf(t, sizeof t, "unsupported baud rate %d", baud);
        set_text(error, error_size, t);
        return NULL;
    }
    const int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == EBUSY) { /* TIOCEXCL of another program */
            if (in_use != NULL)
                *in_use = 1;
            in_use_text(error, error_size, path);
        } else if (error_size > 0) {
            snprintf(error, error_size, "cannot open %s: %s", path, strerror(errno));
        }
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
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int e = errno;
        close(fd);
        if (e == EWOULDBLOCK) {
            if (in_use != NULL)
                *in_use = 1;
            in_use_text(error, error_size, path);
        } else if (error_size > 0) {
            snprintf(error, error_size, "cannot lock %s: %s", path, strerror(e));
        }
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
    close(port->fd); /* drops the flock */
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

int pei_port_held_elsewhere(const char* path)
{
    const int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return errno == EBUSY;
    const int held = flock(fd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK;
    close(fd);
    return held;
}

/* ------------------------------------------------------------ listing */

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

static void list_add_cb(void* user, const char* path)
{
    list_add((port_list*)user, path);
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

/* One line of a sysfs attribute file, without the line end; "" when absent. */
static void read_attr(const char* dir, const char* name, char* out, size_t size)
{
    out[0] = '\0';
    char path[PATH_MAX];
    if ((size_t)snprintf(path, sizeof path, "%s/%s", dir, name) >= sizeof path)
        return;
    FILE* f = fopen(path, "r");
    if (f == NULL)
        return;
    if (fgets(out, (int)size, f) == NULL)
        out[0] = '\0';
    fclose(f);
    out[strcspn(out, "\r\n")] = '\0';
}

typedef struct usb_info {
    unsigned vendor_id, product_id;
    char serial[128];
    char description[256];
} usb_info;

/* The USB device above a tty (/dev/ttyUSB0 -> /sys/class/tty/ttyUSB0/
 * device -> .. until a folder with idVendor); zeros for other ports. */
static void usb_of(const char* real, usb_info* u)
{
    memset(u, 0, sizeof *u);
    const char* base = strrchr(real, '/');
    base = base != NULL ? base + 1 : real;
    char link[PATH_MAX], dir[PATH_MAX];
    if ((size_t)snprintf(link, sizeof link, "/sys/class/tty/%s/device", base) >= sizeof link ||
        realpath(link, dir) == NULL)
        return;
    for (int up = 0; up < 6; ++up) {
        char vid[16], pid[16];
        read_attr(dir, "idVendor", vid, sizeof vid);
        read_attr(dir, "idProduct", pid, sizeof pid);
        if (vid[0] != '\0' && pid[0] != '\0') {
            u->vendor_id = (unsigned)strtoul(vid, NULL, 16);
            u->product_id = (unsigned)strtoul(pid, NULL, 16);
            read_attr(dir, "serial", u->serial, sizeof u->serial);
            char maker[120], product[120];
            read_attr(dir, "manufacturer", maker, sizeof maker);
            read_attr(dir, "product", product, sizeof product);
            snprintf(u->description, sizeof u->description, "%s%s%s", maker, maker[0] && product[0] ? " " : "",
                     product);
            return;
        }
        char* slash = strrchr(dir, '/');
        if (slash == NULL || slash == dir)
            return;
        *slash = '\0';
    }
}

int pei_port_list(const char* extra, void (*cb)(void* user, const pei_port_info* info), void* user)
{
    port_list* l = (port_list*)calloc(1, sizeof *l);
    if (l == NULL)
        return 0;
    static const char* const tty_prefixes[] = {"ttyUSB", "ttyACM", NULL};
    list_dir(l, "/dev/serial/by-id", NULL);
    list_dir(l, "/dev", tty_prefixes);
    pei_port_split_list(extra, list_add_cb, l);
    const int count = l->count;
    for (int i = 0; i < count; ++i) {
        usb_info u;
        usb_of(l->real[i], &u);
        pei_port_info info;
        info.path = l->path[i];
        info.device = l->real[i];
        info.description = u.description;
        info.vendor_id = u.vendor_id;
        info.product_id = u.product_id;
        info.serial = u.serial;
        cb(user, &info);
    }
    free(l);
    return count;
}

#endif /* !_WIN32 */
