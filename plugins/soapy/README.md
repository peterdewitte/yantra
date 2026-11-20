<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# soapysdr radio plugin

The SoapySDR radio plugin `soapysdr` (RAD-50): RTL-SDR, Airspy and network
radios (SoapyRemote) through the SoapySDR modules built in statically
(`cmake/YantraSoapy.cmake`). Shipped in `lib/yantra/plugins/radios/`; the
test build `soapysdr_fake` goes to the tests' plugin folder only.

- Documentation: [docs/plugins/soapysdr.md](../../docs/plugins/soapysdr.md).
- Tests: `tests/radio/soapysdr/`, `tests/radio/soapy_remote/`, `tests/radio/soapy_deps/`, `tests/radio/fake_soapy/` (label `plugin:soapy`).
