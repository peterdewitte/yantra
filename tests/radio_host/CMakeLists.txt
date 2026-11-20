# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Radio host tests (PLG-25, PLG-27, PLG-26, NFR-04): a fake radio plugin
# (fake_radio.cpp, radio API in radios/; again with API major 2 for
# the version refusal) driven by the tests, and the simulator, beacon and
# soapysdr_fake (fake SoapySDR module, unplug/replug) plugins built from
# yantra-radios (YantraRadiosBuild.cmake; those cases skip when
# YANTRA_BUILD_RADIOS is OFF or a prebuilt package has no test plugins).
# One ctest per test case.
foreach(_v IN ITEMS 1 2)
  set(_t fake_radio_v${_v})
  add_library(${_t} MODULE fake_radio.cpp)
  if(WIN32)
    # YR_EXPORT is empty on Windows: the exports come from a .def file.
    target_sources(${_t} PRIVATE fake_radio.def)
  endif()
  target_link_libraries(${_t} PRIVATE yantra::radio_api yantra_warnings)
  target_compile_definitions(${_t} PRIVATE FAKE_API_MAJOR=${_v})
  set_target_properties(${_t} PROPERTIES PREFIX "" OUTPUT_NAME fake
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules/v${_v})
endforeach()

set(_dir ${CMAKE_CURRENT_SOURCE_DIR})
# The shipped radio plugins: 4, or 3 without pei (YANTRA_RADIOS_PEI=OFF).
list(LENGTH _yr_shipped _yr_shipped_count)
file(GLOB _srcs CONFIGURE_DEPENDS ${_dir}/test_*.cpp)
add_executable(test_radio_host ${_srcs})
target_link_libraries(test_radio_host PRIVATE yantra_test_main yantra_warnings yantra::core)
add_dependencies(test_radio_host fake_radio_v1 fake_radio_v2 yantra_radio_plugins)
target_compile_definitions(test_radio_host PRIVATE
  FAKE_RADIO_PATH="$<TARGET_FILE:fake_radio_v1>"
  FAKE_RADIO_V2_PATH="$<TARGET_FILE:fake_radio_v2>"
  RADIO_PLUGIN_DIR="${YANTRA_RADIO_PLUGIN_DIR}"
  RADIO_TEST_PLUGIN_DIR="${YANTRA_RADIO_TEST_PLUGIN_DIR}"
  SHIPPED_RADIO_PLUGINS=${_yr_shipped_count}
  FAKE_SDR_SCENARIO="${YANTRA_RADIOS_FAKE_SDR_SCENARIO}"
  FAKE_SDR_CONTROL_DIR="${CMAKE_CURRENT_BINARY_DIR}/fake_sdr_control")
yantra_doctest_tests(test_radio_host GROUP test_radio_host LABELS fast TIMEOUT 600
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
