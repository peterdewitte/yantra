<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# pocketfft (C++ header-only, vendored)

`pocketfft_hdronly.h` and `LICENSE.md` of pocketfft's C++ branch `cpp` at
commit 4d865cba24359b05b37da4e75aee24498c3b78e7 (2026-10-18), BSD-3-Clause,
unchanged.

- Source: https://github.com/mreineck/pocketfft (branch `cpp`)
- SHA-256 of `pocketfft_hdronly.h`: 518a95c552aeaee352aa30122137cd9e135d23c91e2863d9272aaa758744c86d
- SHA-256 of `LICENSE.md`: a85ca13fdf90160b64a0698215868c13b74d835ad0a4e2ba44713b8c5058a056

Used by the receiver library (`receiver/src/fft.cpp`, RAD-100) with
`POCKETFFT_NO_MULTITHREADING`; header-only, compiled into the plugins
(RAD-132).
