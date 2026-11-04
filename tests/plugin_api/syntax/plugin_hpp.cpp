// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Syntax check of yantra/plugin.hpp with extra warnings (-Wconversion),
// compiled by tests/plugin_api/CMakeLists.txt.
#include <yantra/plugin.hpp>

int yantra_syntax_check();
int yantra_syntax_check()
{
    return static_cast<int>(
        yantra::plugin_api::refusal_reason(yantra_api_version_compiled(), nullptr).size());
}
