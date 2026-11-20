// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Same generator output on every Windows machine (RAD-93, RAD-131): the
// Universal C Runtime picks FMA3 code paths for some math functions when
// the CPU has FMA3, so the last bits of exp/log/sin/... (and the golden
// digests of tests/yantra_gen and tests/gen_sim) would depend on the
// machine. Programs that compute generator output (yantra-gen, the test
// mains) call use_portable_math() first. No effect on other platforms.
#pragma once

#ifdef _WIN32
// Exported by ucrtbase (api-ms-win-crt-math); the MinGW-w64 headers do
// not declare it (MSVC's math.h does, x64 only).
extern "C" int __cdecl _set_FMA3_enable(int flag);
#endif

namespace yantra::gen {

/// Turns the C runtime's CPU-dependent FMA3 math paths off (Windows).
inline void use_portable_math()
{
#ifdef _WIN32
    _set_FMA3_enable(0);
#endif
}

}  // namespace yantra::gen
