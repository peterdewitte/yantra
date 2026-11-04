// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Version and build configuration of the Yantra core library, for the About
// dialog, the API status reply and log headers.
#pragma once

#include <string_view>

namespace yantra::core {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

struct BuildInfo {
    Version version;
    std::string_view version_string;  // "major.minor.patch"
    bool simd = false;                // built with YANTRA_SIMD=ON
    unsigned plugin_abi_major = 0;    // include/yantra/plugin.h
    unsigned plugin_abi_minor = 0;
};

// Build information of the linked core library (constant for a build).
const BuildInfo& build_info() noexcept;

}  // namespace yantra::core
