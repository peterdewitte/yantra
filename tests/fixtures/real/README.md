<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Real-signal fixtures (RAD-121)

Short, anonymised recordings of real TETRA cells, one sub-folder per
radio type (`rtlsdr/`, `hackrf/`, `airspy/`, ...), each recording as
`NAME.sigmf-data`, `NAME.sigmf-meta` and `NAME.expected.json`. Format,
anonymisation and how to add one: [REAL_WORLD_CHECKLIST.md](../../../REAL_WORLD_CHECKLIST.md),
section "Real-signal fixtures". The ctest `real_fixtures` checks every
recording here.

Status: BLOCKED, no real recordings yet (never faked).
