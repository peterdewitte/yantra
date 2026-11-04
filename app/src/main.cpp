// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Yantra desktop application entry point (stub of the build skeleton).

#include <cstdio>
#include <string_view>

#include "yantra/core/build_info.h"

int main(int argc, char** argv) {
    const auto& info = yantra::core::build_info();
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--version") {
            std::printf("yantra %.*s\n", static_cast<int>(info.version_string.size()),
                        info.version_string.data());
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            std::printf("Usage: yantra [--version] [--help]\n");
            return 0;
        }
        std::fprintf(stderr, "yantra: unknown option '%s' (try --help)\n", argv[i]);
        return 2;
    }
    std::printf("yantra %.*s: the user interface is not built yet.\n",
                static_cast<int>(info.version_string.size()), info.version_string.data());
    return 0;
}
