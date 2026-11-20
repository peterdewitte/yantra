// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden files of the test suite (TST-01; as radio parts RAD-93).
// Copied from yantra-radios fb194f5: tests/golden.hpp. A test compares its output with
// a checked-in file; run the test binary with --update (or with the
// environment variable YANTRA_UPDATE_GOLDEN=1) to rewrite the files on
// purpose, and give the reason in the commit message.
#pragma once

namespace yantra::tests {

/// True when golden files are to be rewritten instead of compared.
bool update_golden();

}  // namespace yantra::tests
