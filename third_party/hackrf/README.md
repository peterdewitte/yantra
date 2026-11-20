<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# libhackrf 2026.01.3 (vendored subset)

The files of the libhackrf 2026.01.3 release (BSD-3-Clause) that the static build
needs, unchanged: `host/libhackrf/src/hackrf.c` and `hackrf.h` (BSD-3-Clause, see their headers).

- Source: https://github.com/greatscottgadgets/hackrf/releases/download/v2026.01.3/hackrf-2026.01.3.tar.xz
- SHA-256 of that archive: d2b76a1115d9b4df648c29efb2f3c8e80009b7cf9a8adf37abbfdba72101b086

Used as the vendor library of SoapyHackRF. It is compiled statically into the SDR plugins (RAD-132,
RAD-136); `tools/deps/verify_sources.py` checks these files against the
archive. The complete release is the corresponding source offered with
every plugin release (RAD-137).
