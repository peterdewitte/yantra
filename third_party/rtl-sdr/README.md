<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# librtlsdr 2.0.3 (vendored subset)

The files of the librtlsdr 2.0.3 release (GPL-2.0-or-later) that the static build
needs, unchanged: `src/librtlsdr.c`, `src/tuner_*.c`, `include/*.h`, `COPYING` and `AUTHORS`.

- Source: https://github.com/osmocom/rtl-sdr/archive/refs/tags/v2.0.3.tar.gz
- SHA-256 of that archive: 23dec530f40456a8396e7c897f313859877e18b4c6432c6d323645871e351f6f

Used as the vendor library of SoapyRTLSDR (RTL-SDR v4 included); compiled with `DETACH_KERNEL_DRIVER` as distributions do. It is compiled statically into the SDR plugins (RAD-132,
RAD-136); `tools/deps/verify_sources.py` checks these files against the
archive. The complete release is the corresponding source offered with
every plugin release (RAD-137).
