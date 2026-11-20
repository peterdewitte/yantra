<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# libairspy 1.0.10 (vendored subset)

The files of the libairspy 1.0.10 release (BSD-3-Clause) that the static build
needs, unchanged: `libairspy/src/*.c` and `*.h` (BSD-3-Clause, see their headers).

- Source: https://github.com/airspy/airspyone_host/archive/refs/tags/v1.0.10.tar.gz
- SHA-256 of that archive: fcca23911c9a9da71cebeffeba708c59d1d6401eec6eb2dd73cae35b8ea3c613

Used as the vendor library of SoapyAirspy. It is compiled statically into the SDR plugins (RAD-132,
RAD-136); `tools/deps/verify_sources.py` checks these files against the
archive. The complete release is the corresponding source offered with
every plugin release (RAD-137).
