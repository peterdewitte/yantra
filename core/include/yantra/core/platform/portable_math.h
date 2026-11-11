// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The same numbers on every machine (DEMO-06, HW-06): the Universal C
// Runtime of Windows picks FMA3 code paths for some math functions (exp,
// log, pow, ...) when the CPU has FMA3, so their last bits depend on the
// machine, and a demo project made on Windows would differ from the one
// made on Linux. Programs whose output is compared byte for byte
// (yantra-demo) call use_portable_math() first. No effect elsewhere.
// Copied from yantra-radios 6fac3ee: gen/include/yantra/gen/portable_math.hpp
// (yantra::gen::use_portable_math), in Yantra's namespace.
#pragma once

#ifdef _WIN32
// Exported by ucrtbase (api-ms-win-crt-math); the MinGW-w64 headers do not
// declare it (MSVC's math.h does, x64 only).
extern "C" int __cdecl _set_FMA3_enable(int flag);
#endif

namespace yantra::platform {

/// Turns the C runtime's CPU-dependent FMA3 math paths off (Windows).
inline void use_portable_math() {
#ifdef _WIN32
    _set_FMA3_enable(0);
#endif
}

}  // namespace yantra::platform
