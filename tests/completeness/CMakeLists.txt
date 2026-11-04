# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RAD-133 completeness checks under ctest:
#   completeness_selftest  the readelf/objdump parsers (Linux and Windows)
#   completeness_fixture   a C++ plugin built with yantra_add_radio_plugin()
#                          passes (only allowed NEEDED, one export)
#   completeness_negative  a plugin linked the ordinary way is caught
#                          (libstdc++/libgcc_s NEEDED, many exports)
#   completeness_plugins   every plugin in the plugin folder, and every
#                          plugin this configuration builds is there
#   completeness_surprises thread-local data in the initial-exec model is
#                          caught; the version script hides a GNU unique
#                          symbol
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_check ${PROJECT_SOURCE_DIR}/tools/ci/check_completeness.py)
set(_fixture_dir ${CMAKE_CURRENT_BINARY_DIR}/fixture_plugins)

set(_allow "")
if(NOT YANTRA_SANITIZER STREQUAL "")
  # Sanitizer builds link the sanitizer runtimes dynamically.
  set(_allow --allow "lib(asan|ubsan|tsan|lsan)\\.so\\.[0-9]+" --allow "libstdc\\+\\+\\.so\\.6"
             --allow "libgcc_s\\.so\\.1")
endif()

yantra_add_radio_plugin(completeness_good SOURCES fixture_plugin.cpp OUTPUT_DIR ${_fixture_dir}/good
                        NO_TESTS) # a fixture: completeness_fixture checks it

add_library(completeness_bad MODULE fixture_plugin.cpp)
target_link_libraries(completeness_bad PRIVATE yantra::radio_api)
set_target_properties(completeness_bad PROPERTIES PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY ${_fixture_dir}/bad)

# Thread-local data in the initial-exec model; a GNU unique symbol, hidden.
yantra_add_radio_plugin(completeness_surprises SOURCES fixture_plugin.cpp
                        OUTPUT_DIR ${_fixture_dir}/surprises NO_TESTS)
target_compile_definitions(completeness_surprises PRIVATE YANTRA_FIXTURE_SURPRISES)
target_compile_options(completeness_surprises PRIVATE -ftls-model=initial-exec)

add_test(NAME completeness_selftest COMMAND Python3::Interpreter ${_check} --self-test)
add_test(NAME completeness_fixture
         COMMAND Python3::Interpreter ${_check} ${_allow} $<TARGET_FILE:completeness_good>)
# Every plugin that this configuration builds must be there (RAD-133).
set(_required "")
foreach(_p beacon simulator soapysdr hackrf pei)
  if(TARGET ${_p})
    list(APPEND _required ${_p})
  endif()
endforeach()
list(JOIN _required "," _required)
add_test(NAME completeness_plugins
         COMMAND Python3::Interpreter ${_check} ${_allow} --require "${_required}"
                 ${YANTRA_RADIO_PLUGIN_DIR})
if(YANTRA_SANITIZER STREQUAL "")
  add_test(NAME completeness_negative
           COMMAND Python3::Interpreter ${_check} --expect needed,exports
                   $<TARGET_FILE:completeness_bad>)
  add_test(NAME completeness_surprises
           COMMAND Python3::Interpreter ${_check} --expect static_tls
                   $<TARGET_FILE:completeness_surprises>)
  set(_neg completeness_negative completeness_surprises)
endif()
set_tests_properties(completeness_selftest completeness_fixture completeness_plugins ${_neg}
  PROPERTIES LABELS "fast;completeness" TIMEOUT 120)
