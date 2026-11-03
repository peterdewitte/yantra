# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Real-signal fixtures (RAD-121, docs/ROBUSTNESS.md): real_fixtures decodes
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
  COMMAND sh -c "rm -rf \"$2\" && mkdir -p \"$2/root/gen\" && \"$0\" --cell id=A,carrier=3600,la=77,cc=5 --cell id=B,carrier=3604,la=78,cc=6,level=-75 --mcc 901 --mnc 4 --rate 500000 --duration 4 --format cu8 --quiet --out \"$2/root/gen/two.sigmf-data\" && printf '%s' '{\"format\":\"yantra-radios-fixture\",\"version\":1,\"radio\":\"yantra-gen\",\"complete\":true,\"cells\":[{\"downlink_hz\":390000000,\"mcc\":901,\"mnc\":4,\"colour_code\":5,\"location_area\":77,\"air_encryption\":false},{\"downlink_hz\":390100000,\"mcc\":901,\"mnc\":4,\"colour_code\":6,\"location_area\":78}]}' > \"$2/root/gen/two.expected.json\" && \"$1\" \"$2/root\" \"$2/work\" && printf '%s' '{\"format\":\"yantra-radios-fixture\",\"version\":1,\"radio\":\"yantra-gen\",\"complete\":true,\"cells\":[{\"downlink_hz\":390000000,\"mcc\":901,\"mnc\":4,\"colour_code\":5,\"location_area\":77}]}' > \"$2/root/gen/two.expected.json\" && ! \"$1\" \"$2/root\" \"$2/work\"; rc=$?; rm -rf \"$2\"; exit $rc"
          $<TARGET_FILE:yantra-gen> $<TARGET_FILE:yantra-check-fixtures> ${_st})
set_tests_properties(real_fixtures_selftest PROPERTIES LABELS fast TIMEOUT 300)
