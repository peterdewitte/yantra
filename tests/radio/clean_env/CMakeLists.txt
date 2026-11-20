# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RAD-133: the conformance kit on every plugin (bin/plugins) and the fake-
# module test builds (tests/plugins, RAD-92) in a clean environment: a
# bubblewrap sandbox with only the RAD-132 system libraries
# (tools/ci/clean_env.py; skipped when bubblewrap cannot run). A fixture
# plugin that needs a library of the build tree must be refused there.
# The CI job runs the same script in a bare container (--sandbox none).
if(WIN32)
  yantra_skip_test(clean_env_conformance REASON "a bubblewrap sandbox (Linux); on Windows the completeness_* tests check the imports and the CI's Windows runner, a machine without the build tools, runs the kit natively")
  return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT YANTRA_SANITIZER STREQUAL ""
   OR NOT TARGET yantra-radio-host OR NOT TARGET simulator)
  return()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_library(yantra_clean_env_foreign SHARED foreign_dep.c)
set_target_properties(yantra_clean_env_foreign PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/foreign)
add_library(clean_env_foreign MODULE ${PROJECT_SOURCE_DIR}/tests/radio/completeness/fixture_plugin.cpp)
# --no-as-needed: the fixture calls nothing of it, the NEEDED entry must stay.
target_link_libraries(clean_env_foreign PRIVATE yantra::radio_api "-Wl,--no-as-needed"
  yantra_clean_env_foreign)
set_target_properties(clean_env_foreign PROPERTIES PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fixture
  BUILD_RPATH ${CMAKE_CURRENT_BINARY_DIR}/foreign)
add_test(NAME clean_env_conformance
         COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/ci/clean_env.py
                 --host $<TARGET_FILE:yantra-radio-host>
                 --plugins ${YANTRA_RADIO_PLUGIN_DIR} --plugins ${YANTRA_RADIO_TEST_PLUGIN_DIR}
                 --fixture ${PROJECT_SOURCE_DIR}/tests/radio/fixtures/soapy/fake_two_cells.json
                 --report-dir ${PROJECT_BINARY_DIR}/conformance/clean
                 --expect-fail $<TARGET_FILE:clean_env_foreign>)
# The kit's records check is real time (label timing, as the per-plugin
# conformance tests); it runs after the build, when every plugin exists.
set_tests_properties(clean_env_conformance PROPERTIES LABELS "fast;timing" TIMEOUT 900
  SKIP_RETURN_CODE 77)
