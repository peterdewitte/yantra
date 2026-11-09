// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thread names (OA-03: every thread is named). set_thread_name names the
// calling thread, so debuggers, top -H and /proc/<pid>/task/*/comm show
// what each thread does. Linux keeps at most 15 characters; longer names
// are cut. Every Yantra thread starts with "yt-". Windows is DEFERRED
// (SetThreadDescription); other systems: no-op.
#pragma once

#include <string>

namespace yantra::platform {

// Names the calling thread (cut to 15 characters).
void set_thread_name(const char* name);
// The calling thread's name ("" when unknown).
std::string thread_name();

}  // namespace yantra::platform
