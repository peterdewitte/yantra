<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Radio common code

The radio plugins' common code, built into each plugin statically:
`radios_common` (record files, scene JSON, `yantra::radios_common`),
`receiver` (DSP, carrier search, TETRA decoder with osmo-tetra and the
Rust protocol crate in `receiver/rust`, level and quality meters),
`tetra_common` (TETRA plugin options and schemas) and `soapy_common` (the
SoapySDR layer).

- Documentation: [docs/RADIO_ARCHITECTURE.md](../docs/RADIO_ARCHITECTURE.md), [docs/RECEIVER_DSP.md](../docs/RECEIVER_DSP.md), [docs/TETRA_DECODER.md](../docs/TETRA_DECODER.md).
- Tests: `tests/radio/receiver/`, `tests/radio/tetra_decoder/`, `tests/radio/tetra_engine/` and the other `tests/radio/` folders (label `radio-common`).
