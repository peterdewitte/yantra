// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The dependency manifest compiled into every artefact that links the
// receiver (RAD-136): name, version and licence of each bundled library,
// generated at configure time from tools/deps/manifest.tsv. The test
// host's --version and the plugins' "Build info" sensor (RAD-45) show it.
#pragma once

#include <span>

namespace yantra::receiver {

struct BundledLibrary {
    const char* name;
    const char* version;
    const char* licence;  ///< SPDX identifier
};

/// Every bundled library, in manifest order.
std::span<const BundledLibrary> bundled_libraries() noexcept;

}  // namespace yantra::receiver
