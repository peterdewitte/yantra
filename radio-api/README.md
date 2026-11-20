<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Radio API

The radio API (RAD-10, RAD-11): `include/yantra/radio.h` (plain C ABI),
`radio.hpp` (C++ helpers). The reference host of the API with the
conformance kit (RAD-91) is `tools/yantra-radio-host`. CMake target `yantra::radio_api`, the only part of the radio parts that
the application compiles. `ninja docs-radio-api` builds the Doxygen
reference (`Doxyfile.in`).

- Documentation: [docs/RADIO_PLUGINS.md](../docs/RADIO_PLUGINS.md), [CHANGELOG.md](../CHANGELOG.md).
- Tests: `tests/radio/radio_api_header/`, `tests/radio/radio_host_kit/`, `tests/radio_api/` (label `radio-api`).
