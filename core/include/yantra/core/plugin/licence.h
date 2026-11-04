// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Licence rule for plugins (PLG-10): a plugin runs inside Yantra and forms
// one program with it, so it must be under a GPL-3.0-compatible licence,
// declared as an SPDX expression.
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plugin/licence.h.
#pragma once

#include <string>

namespace yantra::plugin {

struct LicenceCheck {
    bool compatible = false;
    std::string message;  // the reason when not compatible
};

// Checks an SPDX expression ("MIT", "GPL-2.0-or-later OR MIT",
// "(Apache-2.0 AND BSD-3-Clause)", "GPL-3.0-or-later WITH <exception>").
// OR needs one compatible side, AND needs both. Identifiers are matched
// case-insensitively; unknown ones (including LicenseRef-*) count as not
// compatible.
LicenceCheck check_licence(const std::string& spdx_expression);

// The notice the plugin manager shows next to the licences (PLG-10).
const char* licence_notice() noexcept;

}  // namespace yantra::plugin
