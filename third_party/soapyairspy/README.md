<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# SoapyAirspy 0.2.1 (vendored subset)

The files of the SoapyAirspy 0.2.1 release (MIT) that the static build
needs, unchanged: `SoapyAirspy.hpp`, `Registration.cpp`, `Settings.cpp`, `Streaming.cpp` and `LICENSE.txt`.

- Source: https://github.com/pothosware/SoapyAirspy/archive/refs/tags/soapy-airspy-0.2.1.tar.gz
- SHA-256 of that archive: 6170c8462a026282c48b89cadf70d93b671999b3930267442a1754e2a67248f2

Used as the SoapySDR module of the Airspy (RAD-50). It is compiled statically into the SDR plugins (RAD-132,
RAD-136); `tools/deps/verify_sources.py` checks these files against the
archive. The complete release is the corresponding source offered with
every plugin release (RAD-137).
