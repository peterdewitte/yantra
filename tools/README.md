<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tools

Programs: `yantra-demo` (demo projects and the sample report),
`yantra-probe` (the headless probe), `yantra-plugin-test` (the plugin
test kit), and the radio tools `yantra-radio-host` (radio test host and
conformance kit), `yantra-gen` (TETRA test-signal generator),
`yantra-hackrf` (HackRF check), `yantra-pei-sim` (simulated terminal)
and `yantra-sens` (sensitivity runs). Scripts: `ci/` (lints, release,
licence bundle), `docs/` (HTML docs and PDF manuals), `sdk/` (the cross
SDK), `deps/` (the radio plugins' bundled libraries), `windows/`,
`screenshots/`. The radio tools use the radio parts only, never `app/`
or `core/` (PLG-30).
