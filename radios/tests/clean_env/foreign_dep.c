// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A shared library that only the build tree has: the clean-environment
// check (tools/ci/clean_env.py --expect-fail) must refuse a plugin that
// needs it.
int yantra_clean_env_foreign(void) { return 42; }
