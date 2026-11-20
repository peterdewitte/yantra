# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The static dependency builds (RAD-136): tools/deps/build_deps.sh builds
# and installs the bundled libraries into a prefix in the build tree and
# lists the dependency manifest; a second run finds the prefix up to date.
# (The main build uses the same deps.cmake; the CI configures with
# YANTRA_DEPS_PREFIX on the cached prefix.)
# The prefix is built with this tree's compiler: its toolchain file (the
# cross SDK's clang, Linux or Windows) goes to the script as CMake reads
# it, in the environment (CMake >= 3.21), with the tree's SDK folder when
# it was given (YANTRA_SDK_DIR, e.g. the radios build inside Yantra's
# build, PLG-29).
set(_prefix ${CMAKE_BINARY_DIR}/deps-prefix-check)
set(_env "")
if(CMAKE_TOOLCHAIN_FILE)
  if(YANTRA_SDK_DIR)
    set(_env ENVIRONMENT "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}\;YANTRA_SDK_DIR=${YANTRA_SDK_DIR}")
  else()
    set(_env ENVIRONMENT "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  endif()
endif()
add_test(NAME deps_build COMMAND ${YANTRA_BUILD_HOST_TEST} ${PROJECT_SOURCE_DIR}/tools/deps/build_deps.sh ${_prefix})
set_tests_properties(deps_build PROPERTIES ${_env} LABELS fast TIMEOUT 600 FIXTURES_SETUP deps_prefix
  PASS_REGULAR_EXPRESSION "(bundled libraries in|is up to date)")
add_test(NAME deps_build_cached COMMAND ${YANTRA_BUILD_HOST_TEST} ${PROJECT_SOURCE_DIR}/tools/deps/build_deps.sh ${_prefix})
set_tests_properties(deps_build_cached PROPERTIES ${_env} LABELS fast TIMEOUT 120 FIXTURES_REQUIRED deps_prefix
  PASS_REGULAR_EXPRESSION "is up to date")
add_test(NAME deps_manifest COMMAND ${YANTRA_BUILD_HOST_TEST} ${CMAKE_COMMAND} -E cat ${_prefix}/deps-manifest.tsv)
set_tests_properties(deps_manifest PROPERTIES LABELS fast TIMEOUT 60 FIXTURES_REQUIRED deps_prefix
  PASS_REGULAR_EXPRESSION "talloc\t2\\.4\\.3\tLGPL-3\\.0-or-later\nlibosmocore\t1\\.11\\.0\tGPL-2\\.0-or-later\n")
# Builds with the build machine's compiler: build-host checks in the
# Windows tree.
yantra_build_host_tests(deps_build deps_build_cached deps_manifest)
