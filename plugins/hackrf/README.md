<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# hackrf radio plugin

The HackRF One radio plugin `hackrf` (RAD-60): SoapySDR with SoapyHackRF and
libhackrf built in statically, the receiver and the TETRA decoder
(`radio-common/`). Shipped in `lib/yantra/plugins/radios/`; the test build
`hackrf_fake` on the fake SoapySDR module goes to the tests' plugin folder
only.

- Documentation: [docs/plugins/hackrf.md](../../docs/plugins/hackrf.md), the tool
[docs/YANTRA_HACKRF.md](../../docs/YANTRA_HACKRF.md).
- Tests: `tests/radio/hackrf/`, `tests/radio/yantra_hackrf/`; ctests `completeness_hackrf`, `conformance_hackrf` (label `plugin:hackrf`).
