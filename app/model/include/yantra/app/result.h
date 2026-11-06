// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The outcome of an app command: ok, and the message for the status line.
// Copied from yantra-v2 68ea20a: app/model/include/yantra/app/result.h.
#pragma once

#include <string>

namespace yantra::app {

struct Result {
    bool ok = true;
    std::string message;
};

}  // namespace yantra::app
