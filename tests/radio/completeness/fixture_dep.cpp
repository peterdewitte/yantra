// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test fixture for the RAD-133 completeness check on Windows: a DLL that
// the negative fixture plugin imports (fixture_plugin.cpp,
// YANTRA_FIXTURE_IMPORT), standing for a library linked dynamically.
extern "C" __declspec(dllexport) int yantra_fixture_dep(void)
{
    return 1;
}
