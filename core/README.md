<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Measurement core

Yantra's measurement core, the static library `yantra::core`: the
plugin loader (`plugin/`), the radio host that loads the radio plugins
through the radio API (`radio/`), the measurement pipeline (`measure/`),
recordings, survey, binning and the coverage map (`survey/`), the
building, plan, project and cell database storage, exports and reports.
Records come in through the radio API; the core has no DSP, decoder or
SoapySDR code and never includes or links a plugin or `radio-common/`
(PLG-30, ctest `boundary_lint`).

- Documentation: [ARCHITECTURE.md](../ARCHITECTURE.md), [docs/MEASUREMENT_PIPELINE.md](../docs/MEASUREMENT_PIPELINE.md).
- Tests: `tests/` (one folder per module, label `core`).
