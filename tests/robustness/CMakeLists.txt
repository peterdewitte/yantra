# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Real-world robustness checklist (RAD-124, docs/ROBUSTNESS.md) and level
# linearity (RAD-125): generator captures with radio, air and program
# impairments through the whole measurement engine (search, channelizer,
# demodulator, decoder, level meter, records). One binary, two tests:
# test_robustness (label fast) and test_robustness_slow (doctest suite
# "slow": the high sample rates and the long run; labels slow, opt_in).
if(NOT YANTRA_OSMO_TETRA_FOUND)
  message(STATUS "test_robustness: left out (needs the osmo-tetra submodule)")
  return()
endif()
file(GLOB _srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(test_robustness ${_srcs})
target_link_libraries(test_robustness PRIVATE yantra_test_main yantra_warnings
  yantra::radio_api yantra::tetra_engine yantra::gen)
target_include_directories(test_robustness PRIVATE ${CMAKE_SOURCE_DIR}/tests/tetra_engine)
# One ctest per test case (cmake/YantraTests.cmake); the slow suite is
# opt-in (label opt_in, YANTRA_TESTS_OPT_IN=1).
yantra_doctest_tests(test_robustness GROUP test_robustness FILTERS --test-suite-exclude=slow
  LABELS fast TIMEOUT 600 WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
yantra_doctest_tests(test_robustness GROUP test_robustness_slow FILTERS --test-suite=slow
  LABELS slow TIMEOUT 1800 OPT_IN WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
