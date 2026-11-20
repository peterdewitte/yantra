/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hand-written config.h for talloc 2.4.3 (RAD-136): talloc's own build
 * (waf) is not on the tool lists, so tools/deps/deps.cmake compiles
 * talloc.c directly. These are the results its configure step gives on
 * glibc and on MinGW-w64 (MSYS2 UCRT64, the cross SDK's clang) for the features talloc.c and
 * lib/replace/replace.h use; nothing from lib/replace's C sources is
 * needed with them.
 */
#ifndef YANTRA_TALLOC_CONFIG_H
#define YANTRA_TALLOC_CONFIG_H

#define TALLOC_BUILD_VERSION_MAJOR 2
#define TALLOC_BUILD_VERSION_MINOR 4
#define TALLOC_BUILD_VERSION_RELEASE 3

#define HAVE_BOOL 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_LIMITS_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_INTPTR_T 1
#define HAVE_UINTPTR_T 1
#define HAVE_VA_COPY 1
#define HAVE_MEMMOVE 1
#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_C99_VSNPRINTF 1
#define HAVE_ASPRINTF 1
#define HAVE_VASPRINTF 1
#define HAVE_STRNLEN 1
#define HAVE_STRNDUP 1
#define HAVE_USLEEP 1
#define HAVE_USECONDS_T 1
#define HAVE_DECL_EWOULDBLOCK 1
#define HAVE_CONSTRUCTOR_ATTRIBUTE 1

#ifndef _WIN32
#define HAVE_DLFCN_H 1
#else
/* MinGW-w64 (clang of the cross SDK and MSYS2 UCRT64 gcc): what talloc's
 * configure finds there. replace.h declares POSIX replacements with these
 * types; talloc.c calls none of those functions, so only the types are
 * needed, never the rep_* code of lib/replace. */
#define HAVE_STRERROR 1
#define HAVE_ERRNO_DECL 1
#define HAVE_PTRDIFF_T 1
#define HAVE_WINDOWS_H 1
#include <sys/types.h>
typedef int uid_t;
typedef int gid_t;
typedef long long loff_t;
#define RTLD_DEFAULT ((void *)0)
#endif

#endif
