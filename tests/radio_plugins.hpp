// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test cases that load a radio plugin (the simulator, the fake-SDR
// builds) or run the radio tools (PLG-30). The build without the radio
// parts (YANTRA_BUILD_RADIOS=OFF, preset plugins-off) has none of them:
// such a case is marked
//   TEST_CASE("..." * yantra::tests::needs_radio_plugins())
// (or its TEST_SUITE is), and doctest leaves it out there; it is then not
// listed either, so it is no ctest of that tree.
#pragma once

#include <doctest/doctest.h>

#ifndef YANTRA_TEST_RADIO_PLUGINS
#error "YANTRA_TEST_RADIO_PLUGINS comes from tests/CMakeLists.txt (yantra_test_main)"
#endif

namespace yantra::tests {

/// Skips (leaves out) the decorated test case or suite when the radio
/// plugins are not built.
inline doctest::skip needs_radio_plugins()
{
    return doctest::skip(YANTRA_TEST_RADIO_PLUGINS == 0);
}

}  // namespace yantra::tests
