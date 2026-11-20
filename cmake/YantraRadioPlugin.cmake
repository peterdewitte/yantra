# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra_add_radio_plugin(<name> SOURCES <src>... [LIBS <lib>...]
#                         [OUTPUT_DIR <dir>] [NO_TESTS])
#
# Builds a self-contained radio plugin module (RAD-131..133): no "lib"
# prefix, hidden visibility, only yantra_radio_entry exported (version
# script on Linux, .def file on Windows), every bundled static library's
# symbols hidden (--exclude-libs,ALL), the GCC runtime linked statically
# (static libstdc++/libgcc; on Windows -static, also winpthreads) and no
# undefined symbols. Output goes to ${YANTRA_RADIO_PLUGIN_DIR} unless
# OUTPUT_DIR is given.
#
# With tests enabled every plugin gets two ctests (label "fast"):
#   completeness_<name>  tools/ci/check_completeness.py on the module (RAD-133)
#   conformance_<name>   the conformance kit, `yantra-radio-host
#                        conformance` (RAD-91), JSON report in
#                        <build>/conformance/<name>.json
# NO_TESTS leaves both out (for a test plugin the host must refuse).

set(YANTRA_RADIO_PLUGIN_MAP ${CMAKE_CURRENT_LIST_DIR}/radio_plugin.map)
set(YANTRA_RADIO_PLUGIN_DEF ${CMAKE_CURRENT_LIST_DIR}/radio_plugin.def)
# Arguments of tools/ci/check_completeness.py for this platform: on
# Windows the PE checks (the script reads the PE files itself, so the
# Windows tree's ctest runs it on Windows too). A cross-compiled tree also
# names the build machine's folders: on the test VM the script cannot know
# them (the build_paths check).
set(YANTRA_COMPLETENESS_PLATFORM "")
if(WIN32)
  set(YANTRA_COMPLETENESS_PLATFORM --platform windows)
  if(CMAKE_CROSSCOMPILING)
    list(APPEND YANTRA_COMPLETENESS_PLATFORM --build-folder ${PROJECT_SOURCE_DIR})
    if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
      list(APPEND YANTRA_COMPLETENESS_PLATFORM --build-folder $ENV{HOME})
    endif()
  endif()
endif()

function(yantra_add_radio_plugin name)
  cmake_parse_arguments(ARG "NO_TESTS" "OUTPUT_DIR" "SOURCES;LIBS" ${ARGN})
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "yantra_add_radio_plugin(${name}): SOURCES missing")
  endif()
  if(NOT ARG_OUTPUT_DIR)
    set(ARG_OUTPUT_DIR ${YANTRA_RADIO_PLUGIN_DIR})
  endif()
  if(WIN32)
    list(APPEND ARG_SOURCES ${YANTRA_RADIO_PLUGIN_DEF})
  endif()
  add_library(${name} MODULE ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE yantra::radio_api yantra_warnings ${ARG_LIBS})
  # The descriptor's version is the product version (PLG-29); the example
  # plugin of the tutorial keeps its own, as a third-party plugin would.
  target_compile_definitions(${name} PRIVATE YANTRA_VERSION="${YANTRA_VERSION}")
  set_target_properties(${name} PROPERTIES
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY ${ARG_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${ARG_OUTPUT_DIR})
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(WIN32)
      # -static: libstdc++, libgcc and winpthreads of the MSYS2 UCRT64 GCC
      # runtime go into the .dll, which then imports only Windows system
      # DLLs and the UCRT (RAD-132). The cross SDK's toolchain file sets it
      # for every link (YANTRA_WIN_STATIC_RUNTIME), MSYS2's gcc does not.
      # No -Wl,-Bdynamic after it: the driver adds libstdc++ last, and a
      # trailing -Bdynamic would import libstdc++-6.dll again.
      target_link_options(${name} PRIVATE -static)
    else()
      # libstdc++ only where the linker is the C++ one (a C plugin has no
      # libstdc++ to link, and clang warns about the unused flag).
      target_link_options(${name} PRIVATE $<$<LINK_LANGUAGE:CXX>:-static-libstdc++> -static-libgcc)
      target_link_options(${name} PRIVATE
        -Wl,--version-script=${YANTRA_RADIO_PLUGIN_MAP}
        -Wl,--exclude-libs,ALL)
      set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS ${YANTRA_RADIO_PLUGIN_MAP})
      # Sanitizer runtimes leave symbols to the executable.
      if(YANTRA_SANITIZER STREQUAL "")
        target_link_options(${name} PRIVATE -Wl,--no-undefined)
      endif()
    endif()
  endif()
  if(YANTRA_RADIOS_BUILD_TESTS AND NOT ARG_NO_TESTS)
    yantra_radio_plugin_tests(${name})
  endif()
endfunction()

# The two ctests of a plugin target (see above); also usable on its own.
function(yantra_radio_plugin_tests name)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_allow "")
  if(NOT YANTRA_SANITIZER STREQUAL "")
    # A sanitizer build links the sanitizer runtimes and the shared C++ runtime.
    set(_allow --allow "lib(asan|ubsan|tsan|lsan)\\.so\\.[0-9]+" --allow "libstdc\\+\\+\\.so\\.6"
               --allow "libgcc_s\\.so\\.1")
  endif()
  if(YANTRA_SANITIZER STREQUAL "asan-ubsan")
    list(APPEND _allow --allow-build-paths) # gcc's ASan keeps the source paths
  endif()
  add_test(NAME completeness_${name}
           COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/ci/check_completeness.py
                   ${YANTRA_COMPLETENESS_PLATFORM} ${_allow} $<TARGET_FILE:${name}>)
  set_tests_properties(completeness_${name} PROPERTIES LABELS fast)
  file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/conformance)
  add_test(NAME conformance_${name}
           COMMAND yantra-radio-host conformance --plugin $<TARGET_FILE:${name}>
                   --report ${PROJECT_BINARY_DIR}/conformance/${name}.json)
  set_tests_properties(conformance_${name} PROPERTIES LABELS fast TIMEOUT 180)
endfunction()
