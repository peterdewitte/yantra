// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thread names (OA-03), see thread_name.h.
#include "yantra/core/platform/thread_name.h"

#if defined(__linux__)
#include <pthread.h>
#endif

#include <cstring>

namespace yantra::platform {

void set_thread_name(const char* name) {
#if defined(__linux__)
    char buf[16] = {};
    std::strncpy(buf, name ? name : "", sizeof buf - 1);
    (void)pthread_setname_np(pthread_self(), buf);
#else
    (void)name;
#endif
}

std::string thread_name() {
#if defined(__linux__)
    char buf[16] = {};
    if (pthread_getname_np(pthread_self(), buf, sizeof buf) == 0) return buf;
#endif
    return {};
}

}  // namespace yantra::platform
