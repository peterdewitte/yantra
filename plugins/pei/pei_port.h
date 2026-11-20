/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Serial ports for the TETRA terminal plugin (RAD-80): one small
 * serial-port interface, own code on the native APIs (RAD-04):
 *   pei_port_posix.c  POSIX termios (Linux), ports from /dev/serial/by-id,
 *                     /dev/ttyUSB*, /dev/ttyACM* with their USB ids from
 *                     sysfs;
 *   pei_port_win32.c  the Win32 communications API (CreateFileW on
 *                     \\.\COMn, SetCommState, SetCommTimeouts, overlapped
 *                     ReadFile/WriteFile with timed waits), ports from the
 *                     SetupAPI (Ports class) and the COMn DOS names. A
 *                     named pipe \\.\pipe\NAME opens through the same
 *                     code (no DCB): the fake terminal of the tests
 *                     (tools/yantra-pei-sim) on Windows.
 *   pei_port.c        what both share, and the pure helpers below.
 * Copied from yantra-v1 c24e8f5: core/gps/byte_stream_posix.cpp (serial
 * part), rewritten in C.
 */
#ifndef YANTRA_PEI_PORT_H
#define YANTRA_PEI_PORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pei_port pei_port;

/** Separator of the extra port list ($YANTRA_PEI_PORTS). */
#if defined(_WIN32)
#define PEI_PORTS_SEPARATOR ';'
#else
#define PEI_PORTS_SEPARATOR ':'
#endif

/** One port of pei_port_list. Strings are valid during the callback. */
typedef struct pei_port_info {
    const char* path;        /**< What pei_port_open takes (/dev/..., COM3, \\.\pipe\NAME). */
    const char* device;      /**< The device behind it (/dev/ttyUSB0, \\.\COM3). */
    const char* description; /**< USB product or Windows friendly name; "" unknown. */
    unsigned vendor_id;      /**< USB vendor id; 0 unknown. */
    unsigned product_id;     /**< USB product id; 0 unknown. */
    const char* serial;      /**< USB serial number; "" unknown. */
} pei_port_info;

/** Nonzero for a supported rate (1200..921600 baud). */
int pei_baud_supported(int baud);
/** Opens `path` raw (8N1, no flow control) at `baud` for this program
 *  alone (Linux: an exclusive flock(2); Windows: no sharing); NULL with a
 *  one-line reason in `error`, and *in_use (when given) 1 when another
 *  program holds the port. */
pei_port* pei_port_open(const char* path, int baud, int* in_use, char* error, size_t error_size);
void pei_port_close(pei_port* port);
/** Reads up to n bytes, waiting at most timeout_ms: > 0 bytes read, 0 none
 *  in time, -1 on an I/O error (unplugged, closed by the other end) with
 *  the reason in pei_port_error. */
long pei_port_read(pei_port* port, char* buf, size_t n, int timeout_ms);
/** Writes all n bytes (waits at most 1 s for room); 0 on an error. */
int pei_port_write(pei_port* port, const char* buf, size_t n);
const char* pei_port_error(const pei_port* port);
/** Drops unread input (before a new command). */
void pei_port_flush_input(pei_port* port);

/** Port path state: 1 present and readable/writable, 0 absent, -1 present
 *  without access (reason in `why`). Opens nothing. */
int pei_port_check(const char* path, char* why, size_t why_size);
/** 1 when another program holds the port now (an open for this program
 *  alone fails), 0 when it is free or absent. Opens the port briefly when
 *  it is free: used only for ports that were found held before. */
int pei_port_held_elsewhere(const char* path);

/** Calls `cb` for each serial port worth offering, without opening any:
 *  Linux: the entries of /dev/serial/by-id first (stable names), then
 *  /dev/ttyUSB* and /dev/ttyACM* not reached through a by-id name;
 *  Windows: the COM ports of the SetupAPI (Ports class, with USB ids and
 *  friendly names), then the other COMn DOS device names, by number;
 *  then the existing entries of the PEI_PORTS_SEPARATOR-separated list
 *  `extra` (e.g. a pseudo-terminal or a pipe, from $YANTRA_PEI_PORTS).
 *  Returns the number of ports. */
int pei_port_list(const char* extra, void (*cb)(void* user, const pei_port_info* info), void* user);

/* Pure helpers (every platform; unit-tested on both). */

#define PEI_PORT_KIND_OTHER 0 /**< Any other path, used as given. */
#define PEI_PORT_KIND_COM 1   /**< A COM port: COMn or \\.\COMn, n 1..256. */
#define PEI_PORT_KIND_PIPE 2  /**< A named pipe \\.\pipe\NAME. */

/** The Win32 device name of `path` in `out`: COMn and \\.\COMn give
 *  \\.\COMn (needed from COM10 on), anything else is copied. Returns the
 *  PEI_PORT_KIND_* of the path. */
int pei_port_win32_name(const char* path, char* out, size_t size);
/** USB ids from a Windows device instance id or hardware id
 *  ("USB\VID_0403&PID_6001\A9XYZ", "FTDIBUS\VID_0403+PID_6001+A9XYZA\0000",
 *  "USB\VID_1234&PID_5678&MI_00\6&1a2b&0&0000"): vendor and product id (0
 *  when absent) and the serial number when the id carries one ("" else).
 *  Returns 1 when VID and PID were found. */
int pei_port_parse_usb_ids(const char* id, unsigned* vendor_id, unsigned* product_id, char* serial,
                           size_t serial_size);
/** Calls `cb` for each non-empty entry of the PEI_PORTS_SEPARATOR-separated
 *  `list` (entries of PEI_PORT_PATH_MAX bytes or more are skipped). */
void pei_port_split_list(const char* list, void (*cb)(void* user, const char* path), void* user);

#define PEI_PORT_PATH_MAX 512

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YANTRA_PEI_PORT_H */
