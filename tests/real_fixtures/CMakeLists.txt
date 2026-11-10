# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Real-signal fixtures (RAD-121, REAL_WORLD_CHECKLIST.md): real_fixtures decodes
# every recording in tests/fixtures/real with decode-iq and compares the
# cells with its NAME.expected.json (passes with a message while there are
# none); real_fixtures_selftest checks the checker on a yantra-gen
# recording written into the build tree.
if(NOT TARGET yantra::radio_host_iq)
  message(STATUS "real_fixtures: left out (needs the osmo-tetra submodule)")
  return()
endif()
add_executable(yantra-check-fixtures check_fixtures.cpp)
target_link_libraries(yantra-check-fixtures PRIVATE yantra::radio_host_iq yantra::radios_common yantra::json
  yantra_warnings)
add_test(NAME real_fixtures
  COMMAND yantra-check-fixtures ${CMAKE_SOURCE_DIR}/tests/fixtures/real ${CMAKE_BINARY_DIR}/real_fixtures_work)
set_tests_properties(real_fixtures PROPERTIES LABELS fast TIMEOUT 600)
set(_st ${CMAKE_BINARY_DIR}/real_fixtures_selftest)
add_test(NAME real_fixtures_selftest
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/fixtures_selftest.py
          $<TARGET_FILE:yantra-gen> $<TARGET_FILE:yantra-check-fixtures> ${_st})
set_tests_properties(real_fixtures_selftest PROPERTIES LABELS fast TIMEOUT 300)
