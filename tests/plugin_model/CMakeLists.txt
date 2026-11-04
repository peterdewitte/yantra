# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Host-side plugin model tests (PLG-02..08, PLG-10, PLG-12, API-04): the
# descriptor, schema, record and licence checks of yantra_core, run on the
# C test plugin of tests/plugin_api and on broken copies of it.
yantra_add_test_dir(plugin_model LIBS yantra::core)
target_sources(test_plugin_model PRIVATE ${PROJECT_SOURCE_DIR}/tests/plugin_api/header_c.c)
target_include_directories(test_plugin_model PRIVATE ${PROJECT_SOURCE_DIR}/tests/plugin_api)
