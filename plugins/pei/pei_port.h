/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Serial ports for the TETRA terminal plugin (RAD-80), own code on the
 * native APIs (RAD-04): POSIX termios on Linux. Windows (Win32 COM ports)
 * is DEFERRED: the functions compile there and report "not supported".
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

/** Nonzero for a supported rate (1200..921600 baud). */
int pei_baud_supported(int baud);
/** Opens `path` raw (8N1, no flow control) at `baud`; NULL with a
 *  one-line reason in `error`. */
pei_port* pei_port_open(const char* path, int baud, char* error, size_t error_size);
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
 *  without access (reason in `why`). */
int pei_port_check(const char* path, char* why, size_t why_size);

/** Calls `cb` for each serial port worth offering: the entries of /dev/serial/by-id
 *  first (stable names), then /dev/ttyUSB* and /dev/ttyACM* not reached
 *  through a by-id name, then the existing entries of the ':'-separated
 *  list `extra` (e.g. a pseudo-terminal, from $YANTRA_PEI_PORTS). Returns
 *  the number of ports. */
int pei_port_list(const char* extra, void (*cb)(void* user, const char* path), void* user);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YANTRA_PEI_PORT_H */
