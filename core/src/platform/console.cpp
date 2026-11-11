// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console of a windowed program, see console.h.
#include "yantra/core/platform/console.h"

#if defined(_WIN32)
#include <windows.h>

#include <cstdio>
#endif

namespace yantra::platform {

void attach_parent_console() {
#if defined(_WIN32)
    const auto unset = [](DWORD which) {
        const HANDLE h = GetStdHandle(which);
        return h == nullptr || h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_UNKNOWN;
    };
    const bool out = unset(STD_OUTPUT_HANDLE);
    const bool err = unset(STD_ERROR_HANDLE);
    if ((!out && !err) || !AttachConsole(ATTACH_PARENT_PROCESS)) return;
    if (out) (void)std::freopen("CONOUT$", "w", stdout);
    if (err) (void)std::freopen("CONOUT$", "w", stderr);
#endif
}

}  // namespace yantra::platform
