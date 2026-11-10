/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hand-written config.h for libusb 1.0.30 (RAD-136): libusb's own build
 * (autotools) is not on the tool lists, so tools/deps/sdr.cmake compiles
 * its sources directly. These are the configure results for glibc on
 * Linux (usbfs backend, netlink hot-plug: no libudev) and for MinGW-w64
 * (cross SDK clang or MSYS2 UCRT64 gcc, WinUSB backend). The library is
 * static: DEFAULT_VISIBILITY is empty, so every symbol follows the hidden
 * visibility preset and stays inside the plugin (RAD-133).
 */
#ifndef YANTRA_LIBUSB_CONFIG_H
#define YANTRA_LIBUSB_CONFIG_H

#define DEFAULT_VISIBILITY
#define PRINTF_FORMAT(a, b) __attribute__((__format__(__printf__, a, b)))
#define ENABLE_LOGGING 1

#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_STRUCT_TIMESPEC 1
#define HAVE_CLOCK_GETTIME 1
#define STDC_HEADERS 1

#if defined(_WIN32)
#define PLATFORM_WINDOWS 1
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#endif
#define LIBUSB_WINDOWS_HOTPLUG 1
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#define PLATFORM_POSIX 1
#define HAVE_ASM_TYPES_H 1
#define HAVE_NFDS_T 1
#define HAVE_PIPE2 1
#define HAVE_EVENTFD 1
#define HAVE_DECL_EFD_CLOEXEC 1
#define HAVE_DECL_EFD_NONBLOCK 1
#define HAVE_TIMERFD 1
#define HAVE_DECL_TFD_CLOEXEC 1
#define HAVE_DECL_TFD_NONBLOCK 1
#define HAVE_PTHREAD_CONDATTR_SETCLOCK 1
#define HAVE_PTHREAD_SETNAME_NP 1
#define HAVE_DLFCN_H 1
#endif

#endif
