# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RAD-133 completeness checks under ctest:
#   completeness_selftest  the readelf/objdump parsers and the PE reader (Linux and Windows)
#   completeness_fixture   a C++ plugin built with yantra_add_radio_plugin()
#                          passes (only allowed NEEDED, one export)
#   completeness_negative  a plugin linked the ordinary way is caught
#                          (libstdc++/libgcc_s NEEDED, many exports; on
#                          Windows: an imported non-system DLL, every
#                          symbol exported without a .def file)
#   completeness_plugins   every plugin in the plugin folder, and every
#                          plugin this configuration builds is there
#   completeness_build_paths  a file that holds a forbidden text is caught
#   completeness_surprises thread-local data in the initial-exec model is
#                          caught; the version script hides a GNU unique
#                          symbol (ELF only: skipped on Windows)
# On Windows (preset "windows") every check reads the PE files itself
# (YANTRA_COMPLETENESS_PLATFORM; Python only, so it runs in the Windows
# test VM as well), and
# completeness_programs checks every .exe: only Windows system DLLs and the
# UCRT imported (RAD-132); completeness_programs_shipped also that the
# shipped programs hold no build folder.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_check ${PROJECT_SOURCE_DIR}/tools/ci/check_completeness.py)
set(_fixture_dir ${CMAKE_CURRENT_BINARY_DIR}/fixture_plugins)

set(_allow "")
if(NOT YANTRA_SANITIZER STREQUAL "")
  # Sanitizer builds link the sanitizer runtimes dynamically.
  set(_allow --allow "lib(asan|ubsan|tsan|lsan)\\.so\\.[0-9]+" --allow "libstdc\\+\\+\\.so\\.6"
             --allow "libgcc_s\\.so\\.1")
endif()

if(YANTRA_SANITIZER STREQUAL "asan-ubsan")
  list(APPEND _allow --allow-build-paths) # gcc's ASan keeps the source paths
endif()

yantra_add_radio_plugin(completeness_good SOURCES fixture_plugin.cpp OUTPUT_DIR ${_fixture_dir}/good
                        NO_TESTS) # a fixture: completeness_fixture checks it

add_library(completeness_bad MODULE fixture_plugin.cpp)
target_link_libraries(completeness_bad PRIVATE yantra::radio_api)
set_target_properties(completeness_bad PROPERTIES PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY ${_fixture_dir}/bad RUNTIME_OUTPUT_DIRECTORY ${_fixture_dir}/bad)
if(WIN32)
  # The toolchain links every module -static: the "ordinary" dynamic
  # dependency is a DLL of our own, imported like any library DLL.
  add_library(completeness_dep SHARED fixture_dep.cpp)
  set_target_properties(completeness_dep PROPERTIES PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${_fixture_dir}/dep ARCHIVE_OUTPUT_DIRECTORY ${_fixture_dir}/dep)
  target_link_libraries(completeness_bad PRIVATE completeness_dep)
  target_compile_definitions(completeness_bad PRIVATE YANTRA_FIXTURE_IMPORT)
endif()

# Thread-local data in the initial-exec model; a GNU unique symbol, hidden.
yantra_add_radio_plugin(completeness_surprises SOURCES fixture_plugin.cpp
                        OUTPUT_DIR ${_fixture_dir}/surprises NO_TESTS)
target_compile_definitions(completeness_surprises PRIVATE YANTRA_FIXTURE_SURPRISES)
target_compile_options(completeness_surprises PRIVATE -ftls-model=initial-exec)

set(_platform ${YANTRA_COMPLETENESS_PLATFORM})
add_test(NAME completeness_selftest COMMAND Python3::Interpreter ${_check} --self-test)
add_test(NAME completeness_fixture
         COMMAND Python3::Interpreter ${_check} ${_platform} ${_allow} $<TARGET_FILE:completeness_good>)
# Every plugin that this configuration builds must be there (RAD-133).
set(_required "")
foreach(_p beacon simulator soapysdr hackrf pei)
  if(TARGET ${_p})
    list(APPEND _required ${_p})
  endif()
endforeach()
list(JOIN _required "," _required)
add_test(NAME completeness_plugins
         COMMAND Python3::Interpreter ${_check} ${_platform} ${_allow} --require "${_required}"
                 ${YANTRA_RADIO_PLUGIN_DIR})
if(WIN32)
  # Every .exe imports only system DLLs and the UCRT; the test programs
  # hold the build tree's paths on purpose (YANTRA_SOURCE_DIR etc.), the
  # shipped programs must not.
  add_test(NAME completeness_programs
           COMMAND Python3::Interpreter ${_check} ${_platform} --programs --allow-build-paths
                   ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
  set(_shipped "")
  foreach(_p yantra-radio-host yantra-gen yantra-hackrf)
    if(TARGET ${_p})
      list(APPEND _shipped $<TARGET_FILE:${_p}>)
    endif()
  endforeach()
  add_test(NAME completeness_programs_shipped
           COMMAND Python3::Interpreter ${_check} ${_platform} --programs ${_shipped})
  set(_programs completeness_programs completeness_programs_shipped)
endif()
if(YANTRA_SANITIZER STREQUAL "")
  add_test(NAME completeness_negative
           COMMAND Python3::Interpreter ${_check} ${_platform} --expect needed,exports
                   $<TARGET_FILE:completeness_bad>)
  if(WIN32)
    yantra_skip_test(completeness_surprises
      REASON "static TLS and GNU unique symbols are ELF (Linux) properties; a PE file has neither")
  else()
    add_test(NAME completeness_surprises
             COMMAND Python3::Interpreter ${_check} --expect static_tls
                     $<TARGET_FILE:completeness_surprises>)
  endif()
  add_test(NAME completeness_build_paths
           COMMAND Python3::Interpreter ${_check} ${_platform} --expect build_paths --forbid yantra_radio_entry
                   $<TARGET_FILE:completeness_good>)
  set(_neg completeness_negative completeness_surprises completeness_build_paths)
endif()
set_tests_properties(completeness_selftest completeness_fixture completeness_plugins ${_neg} ${_programs}
  PROPERTIES LABELS "fast;completeness" TIMEOUT 120)
