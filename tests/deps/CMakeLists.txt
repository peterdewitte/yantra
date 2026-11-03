# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The static dependency builds (RAD-136): tools/deps/build_deps.sh builds
# and installs the bundled libraries into a prefix in the build tree and
# lists the dependency manifest; a second run finds the prefix up to date.
# (The main build uses the same deps.cmake; the CI configures with
# YANTRA_DEPS_PREFIX on the cached prefix.)
set(_prefix ${CMAKE_BINARY_DIR}/deps-prefix-check)
add_test(NAME deps_build COMMAND ${PROJECT_SOURCE_DIR}/tools/deps/build_deps.sh ${_prefix})
set_tests_properties(deps_build PROPERTIES LABELS fast TIMEOUT 600 FIXTURES_SETUP deps_prefix
  PASS_REGULAR_EXPRESSION "(bundled libraries in|is up to date)")
add_test(NAME deps_build_cached COMMAND ${PROJECT_SOURCE_DIR}/tools/deps/build_deps.sh ${_prefix})
set_tests_properties(deps_build_cached PROPERTIES LABELS fast TIMEOUT 120 FIXTURES_REQUIRED deps_prefix
  PASS_REGULAR_EXPRESSION "is up to date")
add_test(NAME deps_manifest COMMAND ${CMAKE_COMMAND} -E cat ${_prefix}/deps-manifest.tsv)
set_tests_properties(deps_manifest PROPERTIES LABELS fast TIMEOUT 60 FIXTURES_REQUIRED deps_prefix
  PASS_REGULAR_EXPRESSION "talloc\t2\\.4\\.3\tLGPL-3\\.0-or-later\nlibosmocore\t1\\.11\\.0\tGPL-2\\.0-or-later\n")
