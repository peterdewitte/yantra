<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# SoapyRTLSDR 0.3.4 (vendored subset)

The files of the SoapyRTLSDR 0.3.4 release (MIT) that the static build
needs, unchanged: `SoapyRTLSDR.hpp`, `Registration.cpp`, `Settings.cpp`, `Streaming.cpp` and `LICENSE.txt`.

- Source: https://github.com/pothosware/SoapyRTLSDR/archive/refs/tags/soapy-rtlsdr-0.3.4.tar.gz
- SHA-256 of that archive: 1602035b849bca2e9c189712a08d22dc0c7c3a936f9063ac9851bf3229d4460e

Used as the SoapySDR module of the RTL-SDR (RAD-50). It is compiled statically into the SDR plugins (RAD-132,
RAD-136); `tools/deps/verify_sources.py` checks these files against the
archive. The complete release is the corresponding source offered with
every plugin release (RAD-137).
