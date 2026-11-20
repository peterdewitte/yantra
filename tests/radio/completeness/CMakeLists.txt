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
#   shipped_paths_selftest  the scanner of tools/ci/check_shipped_paths.py
#   shipped_paths  `cmake --install` of this tree into a scratch prefix;
#                  no installed file (plugins, programs, data, docs) holds
#                  the source tree, the build tree, the cross SDK or the
#                  home folder, and no program or library the configured
#                  install folders (yantra-gen finds its scenarios relative
#                  to itself; RAD-137, RAD-138, Yantra NFR-09). Run on the
#                  build machine (label build_host in the Windows trees).
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
# Every plugin that this configuration builds must be there (RAD-133): the
# shipped ones in the radio plugin folder, the example beacon next to the
# test plugins.
set(_required "")
foreach(_p beacon simulator soapysdr hackrf pei)
  if(TARGET ${_p})
    list(APPEND _required ${_p})
  endif()
endforeach()
list(JOIN _required "," _required)
add_test(NAME completeness_plugins
         COMMAND Python3::Interpreter ${_check} ${_platform} ${_allow} --require "${_required}"
                 ${YANTRA_RADIO_PLUGIN_DIR} $<TARGET_FILE:beacon>)
if(WIN32)
  # Every .exe of the radio parts imports only system DLLs and the UCRT;
  # the test programs hold the build tree's paths on purpose
  # (YANTRA_SOURCE_DIR etc.), the shipped programs must not. The
  # application's programs share <build>/bin and import their DLLs
  # (OA-02): the list is the radio folders' programs, collected when every
  # folder has been read (_yantra_radio_programs_test).
  cmake_language(DEFER DIRECTORY ${PROJECT_SOURCE_DIR} CALL _yantra_radio_programs_test
                 "${_check}" "${_platform}")
  set(_shipped "")
  foreach(_p yantra-radio-host yantra-gen yantra-hackrf)
    if(TARGET ${_p})
      list(APPEND _shipped $<TARGET_FILE:${_p}>)
    endif()
  endforeach()
  add_test(NAME completeness_programs_shipped
           COMMAND Python3::Interpreter ${_check} ${_platform} --programs ${_shipped})
  set(_programs completeness_programs_shipped)
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

add_test(NAME shipped_paths_selftest
         COMMAND Python3::Interpreter -B ${PROJECT_SOURCE_DIR}/tools/ci/check_shipped_paths.py --self-test)
set_tests_properties(shipped_paths_selftest PROPERTIES LABELS "fast;completeness" TIMEOUT 60)
# Sanitizer trees are never installed or shipped (gcc's ASan keeps the
# source paths on purpose).
if(YANTRA_SANITIZER STREQUAL "")
  include(GNUInstallDirs)
  set(_forbid --forbid ${PROJECT_SOURCE_DIR} --forbid ${PROJECT_BINARY_DIR})
  if(YANTRA_SDK_DIR)
    list(APPEND _forbid --forbid ${YANTRA_SDK_DIR})
  endif()
  add_test(NAME shipped_paths
           COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -B
                   ${PROJECT_SOURCE_DIR}/tools/ci/check_shipped_paths.py --cmake ${CMAKE_COMMAND}
                   --build ${PROJECT_BINARY_DIR} --root ${CMAKE_CURRENT_BINARY_DIR}/shipped_paths
                   ${_forbid}
                   --forbid-binary ${CMAKE_INSTALL_FULL_DATADIR}/yantra-radios
                   --forbid-binary ${CMAKE_INSTALL_FULL_LIBDIR}/yantra-radios)
  # It installs the build tree, as the install and release tests do.
  set_tests_properties(shipped_paths PROPERTIES LABELS "fast;completeness" TIMEOUT 300
                       RESOURCE_LOCK install_tree)
  yantra_build_host_tests(shipped_paths)
endif()

# The executables of the radio folders and their subfolders (Windows).
function(_yantra_radio_executables dir var)
  set(_out)
  get_property(_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_t IN LISTS _targets)
    get_target_property(_type ${_t} TYPE)
    if(_type STREQUAL "EXECUTABLE")
      list(APPEND _out $<TARGET_FILE:${_t}>)
    endif()
  endforeach()
  get_property(_subs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
  foreach(_s IN LISTS _subs)
    _yantra_radio_executables(${_s} _more)
    list(APPEND _out ${_more})
  endforeach()
  set(${var} ${_out} PARENT_SCOPE)
endfunction()
function(_yantra_radio_programs_test check platform)
  set(_programs)
  foreach(_d radio-common gen tests/radio tools/yantra-radio-host tools/yantra-gen tools/yantra-hackrf
             tools/yantra-pei-sim tools/yantra-sens)
    if(IS_DIRECTORY ${PROJECT_SOURCE_DIR}/${_d})
      _yantra_radio_executables(${PROJECT_SOURCE_DIR}/${_d} _p)
      list(APPEND _programs ${_p})
    endif()
  endforeach()
  add_test(NAME completeness_programs
           COMMAND ${Python3_EXECUTABLE} ${check} ${platform} --programs --allow-build-paths ${_programs})
  set_tests_properties(completeness_programs PROPERTIES LABELS "fast;completeness;packaging" TIMEOUT 120)
endfunction()
