<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# SoapyHackRF 0.3.6 (vendored subset)

The files of the SoapyHackRF 0.3.6 release (MIT) that the static build
needs, unchanged: `SoapyHackRF.hpp`, `HackRF_*.cpp` and `LICENSE`.

- Source: https://github.com/pothosware/SoapyHackRF/archive/refs/tags/soapy-hackrf-0.3.6.tar.gz
- SHA-256 of that archive: 7bdecb65074f0c30af707234fbb970687626d4b6e1768592325aa706ae46e572

Used as the SoapySDR module of the HackRF (RAD-50, RAD-60). It is compiled statically into the SDR plugins (RAD-132,
RAD-136); `tools/deps/verify_sources.py` checks these files against the
archive. The complete release is the corresponding source offered with
every plugin release (RAD-137).
