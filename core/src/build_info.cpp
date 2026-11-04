// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/build_info.h"

#include "yantra/core/build_config.h"
#include "yantra/plugin.h"

namespace yantra::core {

const BuildInfo& build_info() noexcept {
    static constexpr BuildInfo info{
        Version{YANTRA_VERSION_MAJOR, YANTRA_VERSION_MINOR, YANTRA_VERSION_PATCH},
        YANTRA_VERSION_STRING,
        YANTRA_SIMD != 0,
        YANTRA_PLUGIN_ABI_VERSION_MAJOR,
        YANTRA_PLUGIN_ABI_VERSION_MINOR,
    };
    return info;
}

}  // namespace yantra::core
