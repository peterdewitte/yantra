<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# SoapySDR 0.8.1 (vendored subset)

The files of the SoapySDR 0.8.1 release (BSL-1.0) that the static build
needs, unchanged: `include/SoapySDR/*`, `lib/*.cpp`, `lib/*.hpp` and `LICENSE_1_0.txt`.

- Source: https://github.com/pothosware/SoapySDR/archive/refs/tags/soapy-sdr-0.8.1.tar.gz
- SHA-256 of that archive: a508083875ed75d1090c24f88abef9895ad65f0f1b54e96d74094478f0c400e6

Used as the only SDR front layer (RAD-50). `tools/deps/sdr.cmake` compiles the library without `lib/Modules.in.cpp`, whose module loader is replaced by `tools/deps/soapysdr/modules_static.cpp` (modules are compiled in and register themselves; nothing is loaded from the system); `lib/Version.in.cpp` is configured with the version, as upstream's CMake does. It is compiled statically into the SDR plugins (RAD-132,
RAD-136); `tools/deps/verify_sources.py` checks these files against the
archive. The complete release is the corresponding source offered with
every plugin release (RAD-137).
