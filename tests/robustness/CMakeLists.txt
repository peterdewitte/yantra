# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Real-world robustness checklist (RAD-124, docs/ROBUSTNESS.md) and level
# linearity (RAD-125): generator captures with radio, air and program
# impairments through the whole measurement engine (search, channelizer,
# demodulator, decoder, level meter, records). One binary, two tests:
# test_robustness (label fast) and test_robustness_slow (doctest suite
# "slow": the high sample rates and the long run; label slow).
if(NOT YANTRA_OSMO_TETRA_FOUND)
  message(STATUS "test_robustness: left out (needs the osmo-tetra submodule)")
  return()
endif()
file(GLOB _srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(test_robustness ${_srcs})
target_link_libraries(test_robustness PRIVATE yantra_test_main yantra_warnings
  yantra::radio_api yantra::tetra_engine yantra::gen)
target_include_directories(test_robustness PRIVATE ${CMAKE_SOURCE_DIR}/tests/tetra_engine)
add_test(NAME test_robustness COMMAND test_robustness --test-suite-exclude=slow)
set_tests_properties(test_robustness PROPERTIES LABELS fast TIMEOUT 600
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME test_robustness_slow COMMAND test_robustness --test-suite=slow)
set_tests_properties(test_robustness_slow PROPERTIES LABELS slow TIMEOUT 1800
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
