/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Serial ports on Windows (RAD-80, RAD-04): the Win32 communications API.
 * CreateFileW on \\.\COMn without sharing (a port held by another program
 * gives "in use"), SetCommState (8N1, no flow control, DTR/RTS on),
 * SetCommTimeouts, overlapped ReadFile/WriteFile with timed waits and
 * CancelIoEx. Ports from the SetupAPI (Ports class: PortName, friendly
 * name, USB ids from the instance id or its parent's) and the other COMn
 * DOS device names (QueryDosDeviceW). A named pipe \\.\pipe\NAME goes
 * through the same open/read/write code without the DCB: the fake
 * terminal of the tests (tools/yantra-pei-sim). Hot-plug: the plugin's
 * watcher polls pei_port_list and pei_port_check. See pei_port.h.
 */
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cfgmgr32.h>
#include <setupapi.h>

#include "pei_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* GUID_DEVCLASS_PORTS (devguid.h), spelled out: no initguid.h games. */
static const GUID kPortsClass = {0x4d36e978, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

struct pei_port {
    HANDLE h;
    int kind;
    int failed;
    OVERLAPPED rov, wov;
    char error[256];
};

static void set_text(char* out, size_t size, const char* text)
{
    if (size > 0)
        snprintf(out, size, "%s", text);
}

static void system_text(DWORD code, char* out, size_t size)
{
    wchar_t w[256];
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, w,
                                   (DWORD)(sizeof w / sizeof w[0]), NULL);
    if (n == 0 || WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)size, NULL, NULL) == 0) {
        snprintf(out, size, "Windows error %lu", (unsigned long)code);
        return;
    }
    out[strcspn(out, "\r\n")] = '\0';
    const size_t len = strlen(out);
    if (len > 0 && out[len - 1] == '.')
        out[len - 1] = '\0';
}

static int to_wide(const char* s, wchar_t* out, int count)
{
    return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, count) != 0;
}

static void to_utf8(const wchar_t* w, char* out, size_t size)
{
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)size, NULL, NULL) == 0 && size > 0)
        out[0] = '\0';
}

/* An I/O error: the port is gone for this connection. */
static int io_fail(pei_port* p, DWORD code, const char* what)
{
    p->failed = 1;
    switch (code) {
    case ERROR_BROKEN_PIPE:
    case ERROR_PIPE_NOT_CONNECTED:
    case ERROR_NO_DATA:
        snprintf(p->error, sizeof p->error, "the port was closed by the other end");
        break;
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_DEV_NOT_EXIST:
    case ERROR_BAD_COMMAND:
    case ERROR_GEN_FAILURE:
    case ERROR_ACCESS_DENIED:
    case ERROR_FILE_NOT_FOUND:
        snprintf(p->error, sizeof p->error, "the port went away (unplugged?)");
        break;
    default: {
        char t[200];
        system_text(code, t, sizeof t);
        snprintf(p->error, sizeof p->error, "%s: %s", what, t);
    }
    }
    return -1;
}

pei_port* pei_port_open(const char* path, int baud, int* in_use, char* error, size_t error_size)
{
    if (in_use != NULL)
        *in_use = 0;
    if (!pei_baud_supported(baud)) {
        if (error_size > 0)
            snprintf(error, error_size, "unsupported baud rate %d", baud);
        return NULL;
    }
    char dev[PEI_PORT_PATH_MAX];
    const int kind = pei_port_win32_name(path, dev, sizeof dev);
    wchar_t w[PEI_PORT_PATH_MAX];
    if (!to_wide(dev, w, PEI_PORT_PATH_MAX)) {
        if (error_size > 0)
            snprintf(error, error_size, "cannot open %s: the name is not valid", path);
        return NULL;
    }
    HANDLE h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    DWORD e = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (h == INVALID_HANDLE_VALUE && kind == PEI_PORT_KIND_PIPE && e == ERROR_PIPE_BUSY && WaitNamedPipeW(w, 500)) {
        /* The pipe's server was between two clients: it listens again. */
        h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        e = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    }
    if (h == INVALID_HANDLE_VALUE) {
        if (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION || e == ERROR_PIPE_BUSY) {
            if (in_use != NULL)
                *in_use = 1;
            if (error_size > 0)
                snprintf(error, error_size, "%s is in use by another program; close that program first", path);
        } else if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
            if (error_size > 0)
                snprintf(error, error_size, "cannot open %s: no such port", path);
        } else if (error_size > 0) {
            char t[200];
            system_text(e, t, sizeof t);
            snprintf(error, error_size, "cannot open %s: %s", path, t);
        }
        return NULL;
    }
    if (kind != PEI_PORT_KIND_PIPE) {
        DCB dcb;
        memset(&dcb, 0, sizeof dcb);
        dcb.DCBlength = sizeof dcb;
        if (!GetCommState(h, &dcb)) {
            if (error_size > 0)
                snprintf(error, error_size, "%s is not a serial port", path);
            CloseHandle(h);
            return NULL;
        }
        dcb.BaudRate = (DWORD)baud;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fTXContinueOnXoff = TRUE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fErrorChar = FALSE;
        dcb.fNull = FALSE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fAbortOnError = FALSE;
        /* A read returns as soon as one byte is there; the timed wait of
         * pei_port_read ends it sooner (CancelIoEx). */
        COMMTIMEOUTS t;
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = 60000;
        t.WriteTotalTimeoutMultiplier = 0;
        t.WriteTotalTimeoutConstant = 1000;
        if (!SetCommState(h, &dcb) || !SetCommTimeouts(h, &t)) {
            char why[200];
            system_text(GetLastError(), why, sizeof why);
            if (error_size > 0)
                snprintf(error, error_size, "cannot configure %s: %s", path, why);
            CloseHandle(h);
            return NULL;
        }
        PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }
    pei_port* p = (pei_port*)calloc(1, sizeof *p);
    if (p != NULL) {
        p->rov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        p->wov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    }
    if (p == NULL || p->rov.hEvent == NULL || p->wov.hEvent == NULL) {
        if (p != NULL) {
            if (p->rov.hEvent != NULL)
                CloseHandle(p->rov.hEvent);
            if (p->wov.hEvent != NULL)
                CloseHandle(p->wov.hEvent);
            free(p);
        }
        set_text(error, error_size, "out of memory");
        CloseHandle(h);
        return NULL;
    }
    p->h = h;
    p->kind = kind;
    return p;
}

void pei_port_close(pei_port* port)
{
    if (port == NULL)
        return;
    CancelIoEx(port->h, NULL);
    CloseHandle(port->h);
    CloseHandle(port->rov.hEvent);
    CloseHandle(port->wov.hEvent);
    free(port);
}

static void reset_overlapped(OVERLAPPED* ov)
{
    HANDLE e = ov->hEvent;
    memset(ov, 0, sizeof *ov);
    ov->hEvent = e;
    ResetEvent(e);
}

long pei_port_read(pei_port* port, char* buf, size_t n, int timeout_ms)
{
    if (port->failed)
        return -1;
    if (n == 0)
        return 0;
    reset_overlapped(&port->rov);
    const DWORD want = n > 65536 ? 65536 : (DWORD)n;
    DWORD got = 0;
    if (ReadFile(port->h, buf, want, &got, &port->rov))
        return (long)got;
    DWORD e = GetLastError();
    if (e != ERROR_IO_PENDING)
        return io_fail(port, e, "read");
    const DWORD w = WaitForSingleObject(port->rov.hEvent, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    if (w != WAIT_OBJECT_0)
        CancelIoEx(port->h, &port->rov);
    if (!GetOverlappedResult(port->h, &port->rov, &got, TRUE)) {
        e = GetLastError();
        if (e == ERROR_OPERATION_ABORTED) /* our own cancel: nothing in time */
            return (long)got;
        return io_fail(port, e, "read");
    }
    return (long)got;
}

int pei_port_write(pei_port* port, const char* buf, size_t n)
{
    while (n > 0 && !port->failed) {
        reset_overlapped(&port->wov);
        const DWORD chunk = n > 65536 ? 65536 : (DWORD)n;
        DWORD done = 0;
        if (!WriteFile(port->h, buf, chunk, &done, &port->wov)) {
            const DWORD e = GetLastError();
            if (e != ERROR_IO_PENDING) {
                io_fail(port, e, "write");
                return 0;
            }
            if (WaitForSingleObject(port->wov.hEvent, 1000) != WAIT_OBJECT_0) {
                CancelIoEx(port->h, &port->wov);
                GetOverlappedResult(port->h, &port->wov, &done, TRUE);
                port->failed = 1;
                snprintf(port->error, sizeof port->error, "write timed out");
                return 0;
            }
            if (!GetOverlappedResult(port->h, &port->wov, &done, FALSE)) {
                io_fail(port, GetLastError(), "write");
                return 0;
            }
        }
        if (done == 0) {
            port->failed = 1;
            snprintf(port->error, sizeof port->error, "write timed out");
            return 0;
        }
        buf += done;
        n -= done;
    }
    return !port->failed;
}

const char* pei_port_error(const pei_port* port)
{
    return port->error;
}

void pei_port_flush_input(pei_port* port)
{
    if (port->failed)
        return;
    if (port->kind != PEI_PORT_KIND_PIPE) {
        PurgeComm(port->h, PURGE_RXCLEAR);
        return;
    }
    for (int guard = 0; guard < 64; ++guard) {
        DWORD avail = 0;
        if (!PeekNamedPipe(port->h, NULL, 0, NULL, &avail, NULL) || avail == 0)
            return;
        char tmp[512];
        if (pei_port_read(port, tmp, avail < sizeof tmp ? avail : sizeof tmp, 0) <= 0)
            return;
    }
}

/* A pipe of that name exists: listed in \\.\pipe\ (listing connects to
 * nothing, unlike opening it). */
static int pipe_exists(const char* dev)
{
    wchar_t want[PEI_PORT_PATH_MAX];
    if (!to_wide(dev + 9, want, PEI_PORT_PATH_MAX)) /* after \\.\pipe\ */
        return 0;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(L"\\\\.\\pipe\\*", &fd);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    int found = 0;
    do {
        if (_wcsicmp(fd.cFileName, want) == 0)
            found = 1;
    } while (!found && FindNextFileW(f, &fd));
    FindClose(f);
    return found;
}

int pei_port_check(const char* path, char* why, size_t why_size)
{
    set_text(why, why_size, "");
    if (path == NULL || path[0] == '\0')
        return 0;
    char dev[PEI_PORT_PATH_MAX];
    const int kind = pei_port_win32_name(path, dev, sizeof dev);
    if (kind == PEI_PORT_KIND_PIPE)
        return pipe_exists(dev);
    wchar_t w[PEI_PORT_PATH_MAX];
    if (kind == PEI_PORT_KIND_COM) {
        wchar_t target[512];
        return to_wide(dev + 4, w, PEI_PORT_PATH_MAX) && QueryDosDeviceW(w, target, 512) != 0;
    }
    return to_wide(dev, w, PEI_PORT_PATH_MAX) && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}

int pei_port_held_elsewhere(const char* path)
{
    char dev[PEI_PORT_PATH_MAX];
    pei_port_win32_name(path, dev, sizeof dev);
    wchar_t w[PEI_PORT_PATH_MAX];
    if (!to_wide(dev, w, PEI_PORT_PATH_MAX))
        return 0;
    HANDLE h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    DWORD e = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (h == INVALID_HANDLE_VALUE && e == ERROR_PIPE_BUSY && WaitNamedPipeW(w, 200)) {
        h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        e = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    }
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return 0;
    }
    return e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION || e == ERROR_PIPE_BUSY;
}

/* ------------------------------------------------------------ listing */

#define PEI_MAX_PORTS 64

typedef struct port_entry {
    char path[PEI_PORT_PATH_MAX];
    char device[PEI_PORT_PATH_MAX];
    char description[256];
    char serial[128];
    unsigned vendor_id, product_id;
    int number; /* COMn, else 0 */
} port_entry;

typedef struct port_list {
    port_entry e[PEI_MAX_PORTS];
    int count;
} port_list;

static port_entry* list_add(port_list* l, const char* path)
{
    char dev[PEI_PORT_PATH_MAX];
    const int kind = pei_port_win32_name(path, dev, sizeof dev);
    for (int i = 0; i < l->count; ++i)
        if (_stricmp(l->e[i].device, dev) == 0)
            return NULL;
    if (l->count >= PEI_MAX_PORTS)
        return NULL;
    port_entry* e = &l->e[l->count++];
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s", kind == PEI_PORT_KIND_COM ? dev + 4 : path);
    snprintf(e->device, sizeof e->device, "%s", dev);
    e->number = kind == PEI_PORT_KIND_COM ? atoi(dev + 7) : 0;
    return e;
}

static int compare_ports(const void* a, const void* b)
{
    const port_entry* x = (const port_entry*)a;
    const port_entry* y = (const port_entry*)b;
    return (x->number > y->number) - (x->number < y->number);
}

/* USB ids of a device: from its own instance id, else (composite USB
 * functions, vendor bus drivers) from its parent's. */
static void usb_ids(DEVINST inst, port_entry* e)
{
    for (int level = 0; level < 2; ++level) {
        wchar_t id[MAX_DEVICE_ID_LEN + 1];
        if (CM_Get_Device_IDW(inst, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
            return;
        char text[MAX_DEVICE_ID_LEN * 3 + 1];
        to_utf8(id, text, sizeof text);
        unsigned vid = 0, pid = 0;
        char serial[128];
        if (pei_port_parse_usb_ids(text, &vid, &pid, serial, sizeof serial)) {
            if (e->vendor_id == 0) {
                e->vendor_id = vid;
                e->product_id = pid;
            }
            if (serial[0] != '\0') {
                snprintf(e->serial, sizeof e->serial, "%s", serial);
                return;
            }
        }
        DEVINST parent;
        if (CM_Get_Parent(&parent, inst, 0) != CR_SUCCESS)
            return;
        inst = parent;
    }
}

static void list_setupapi(port_list* l)
{
    HDEVINFO set = SetupDiGetClassDevsW(&kPortsClass, NULL, NULL, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE)
        return;
    SP_DEVINFO_DATA dev;
    dev.cbSize = sizeof dev;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); ++i) {
        HKEY key = SetupDiOpenDevRegKey(set, &dev, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
        if (key == INVALID_HANDLE_VALUE)
            continue;
        wchar_t name[64];
        DWORD size = sizeof name - sizeof name[0];
        DWORD type = 0;
        const LONG r = RegQueryValueExW(key, L"PortName", NULL, &type, (BYTE*)name, &size);
        RegCloseKey(key);
        if (r != ERROR_SUCCESS || type != REG_SZ)
            continue;
        name[size / sizeof name[0]] = L'\0';
        char port[64];
        to_utf8(name, port, sizeof port);
        char dev_name[PEI_PORT_PATH_MAX];
        if (pei_port_win32_name(port, dev_name, sizeof dev_name) != PEI_PORT_KIND_COM)
            continue; /* LPT and others */
        port_entry* e = list_add(l, port);
        if (e == NULL)
            continue;
        wchar_t friendly[256];
        if (SetupDiGetDeviceRegistryPropertyW(set, &dev, SPDRP_FRIENDLYNAME, NULL, (BYTE*)friendly,
                                              sizeof friendly - sizeof friendly[0], NULL)) {
            friendly[sizeof friendly / sizeof friendly[0] - 1] = L'\0';
            to_utf8(friendly, e->description, sizeof e->description);
        }
        usb_ids(dev.DevInst, e);
    }
    SetupDiDestroyDeviceInfoList(set);
}

/* COMn DOS names the SetupAPI did not list (legacy or virtual ports). */
static void list_dos_names(port_list* l)
{
    DWORD size = 65536;
    wchar_t* names = (wchar_t*)malloc(size * sizeof *names);
    if (names == NULL)
        return;
    const DWORD n = QueryDosDeviceW(NULL, names, size);
    for (DWORD i = 0; i < n && names[i] != L'\0'; i += (DWORD)wcslen(names + i) + 1) {
        char name[128];
        to_utf8(names + i, name, sizeof name);
        char dev[PEI_PORT_PATH_MAX];
        if (pei_port_win32_name(name, dev, sizeof dev) == PEI_PORT_KIND_COM && _strnicmp(name, "\\\\", 2) != 0)
            list_add(l, name);
    }
    free(names);
}

static void list_extra(void* user, const char* path)
{
    char why[8];
    if (pei_port_check(path, why, sizeof why) != 0)
        list_add((port_list*)user, path);
}

int pei_port_list(const char* extra, void (*cb)(void* user, const pei_port_info* info), void* user)
{
    port_list* l = (port_list*)calloc(1, sizeof *l);
    if (l == NULL)
        return 0;
    list_setupapi(l);
    list_dos_names(l);
    qsort(l->e, (size_t)l->count, sizeof l->e[0], compare_ports);
    pei_port_split_list(extra, list_extra, l);
    const int count = l->count;
    for (int i = 0; i < count; ++i) {
        const port_entry* e = &l->e[i];
        pei_port_info info;
        info.path = e->path;
        info.device = e->device;
        info.description = e->description;
        info.vendor_id = e->vendor_id;
        info.product_id = e->product_id;
        info.serial = e->serial;
        cb(user, &info);
    }
    free(l);
    return count;
}

#endif /* _WIN32 */
