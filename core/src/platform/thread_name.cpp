// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thread names (OA-03), see thread_name.h.
#include "yantra/core/platform/thread_name.h"

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include <cstring>
#include <string>

namespace yantra::platform {

void set_thread_name(const char* name) {
#if defined(__linux__)
    char buf[16] = {};
    std::strncpy(buf, name ? name : "", sizeof buf - 1);
    (void)pthread_setname_np(pthread_self(), buf);
#elif defined(_WIN32)
    // OA-03 on Windows: the thread description (debuggers, Process Explorer).
    std::wstring w;
    for (const char* c = name ? name : ""; *c != '\0'; ++c) w += static_cast<wchar_t>(static_cast<unsigned char>(*c));
    (void)SetThreadDescription(GetCurrentThread(), w.c_str());
#else
    (void)name;
#endif
}

std::string thread_name() {
#if defined(__linux__)
    char buf[16] = {};
    if (pthread_getname_np(pthread_self(), buf, sizeof buf) == 0) return buf;
#elif defined(_WIN32)
    PWSTR w = nullptr;
    std::string out;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &w)) && w != nullptr) {
        for (const wchar_t* c = w; *c != L'\0'; ++c) out += *c < 128 ? static_cast<char>(*c) : '?';
        LocalFree(w);
    }
    return out;
#endif
    return {};
}

}  // namespace yantra::platform
