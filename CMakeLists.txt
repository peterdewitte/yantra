# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-radios top-level build (RAD-01..03, Yantra OA-01/02): the radio
# API header, the receiver and generator libraries, the radio plugins, the
# tools and the test suite. C++20, CMake >= 3.20, Ninja; on Linux clang of
# the cross SDK by default (presets, tools/sdk, RAD-139) or the system gcc
# (presets gcc, gcc-scalar, ci-gcc); gcc in MSYS2 UCRT64 on Windows (RAD-131).
cmake_minimum_required(VERSION 3.20)
project(YantraRadios VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(YANTRA_RADIOS_BUILD_TESTS "Build the test suite" ON)
option(YANTRA_SIMD "Use SIMD kernels (OFF = plain C++ fallback with the same results, RAD-90, TST-03)" ON)
option(YANTRA_FUZZ "libFuzzer fuzz targets (clang + asan-ubsan, preset fuzz, RAD-96)" OFF)
option(YANTRA_WERROR "Treat compiler warnings as errors (ON in CI, RAD-03, RAD-90)" OFF)
set(YANTRA_SANITIZER "" CACHE STRING
  "Developer sanitizer build: '' (none), 'asan-ubsan' or 'tsan' (RAD-90, Yantra TST-05)")
set_property(CACHE YANTRA_SANITIZER PROPERTY STRINGS "" asan-ubsan tsan)
set(YANTRA_RADIOS_SOURCE_URL "https://github.com/peterdewitte/yantra-radio-plugins"
  CACHE STRING "Public source repository (RAD-12 descriptor, RAD-137)")

# Windows tree (cross-compiled, RAD-131): its ctest runs natively on
# Windows (the test VM, BUILD.md), so the test commands are the .exe,
# cmake -P or python3, and the doctest cases are listed when ctest runs
# there. YANTRA_WINDOWS_TEST_RUNNER=wsl instead runs every .exe from Linux
# through WSL interop (tools/sdk/wsl_run.sh --in-place; WSL2 only).
set(YANTRA_WINDOWS_TEST_RUNNER "" CACHE STRING
  "Cross-compiled Windows tree: '' (ctest runs on Windows) or 'wsl' (ctest on WSL2 runs the .exe through interop)")
set_property(CACHE YANTRA_WINDOWS_TEST_RUNNER PROPERTY STRINGS "" wsl)
if(NOT YANTRA_WINDOWS_TEST_RUNNER STREQUAL "" AND NOT YANTRA_WINDOWS_TEST_RUNNER STREQUAL "wsl")
  message(FATAL_ERROR "YANTRA_WINDOWS_TEST_RUNNER must be '' or 'wsl'")
endif()
if(CMAKE_CROSSCOMPILING AND WIN32 AND YANTRA_WINDOWS_TEST_RUNNER STREQUAL "wsl"
   AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/sdk/wsl_run.sh --check
                  RESULT_VARIABLE _yantra_wsl OUTPUT_QUIET ERROR_QUIET)
  if(_yantra_wsl EQUAL 0)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${CMAKE_CURRENT_SOURCE_DIR}/tools/sdk/wsl_run.sh --in-place)
  else()
    message(WARNING "YANTRA_WINDOWS_TEST_RUNNER=wsl: no WSL interop here; "
                    "the tree's ctest is for a Windows machine (the test VM, BUILD.md)")
  endif()
  unset(_yantra_wsl)
endif()

# Tests that check the build machine or run its tools (SDK compilers,
# cargo, Doxygen, reuse, the dependency and release scripts) run in a
# cross-compiled tree through cmake/build_host_test.py: skipped (77) when
# its ctest runs on Windows, run on the build machine (ctest -L build_host).
# add_test(NAME x COMMAND ${YANTRA_BUILD_HOST_TEST} prog args...), then
# yantra_build_host_tests(x ...). Both are no-ops in a native tree.
set(YANTRA_BUILD_HOST_TEST "")
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(YANTRA_BUILD_HOST_TEST ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_host_test.py)
endif()
function(yantra_build_host_tests)
  if(YANTRA_BUILD_HOST_TEST)
    set_tests_properties(${ARGN} PROPERTIES SKIP_RETURN_CODE 77)
    set_property(TEST ${ARGN} APPEND PROPERTY LABELS build_host)
  endif()
endfunction()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(YantraWarnings)
include(YantraSanitizers)
include(YantraFuzz)
# Bundled C libraries built statically from third_party/ (RAD-136), then
# the osmo-tetra lower layers on them (RAD-101).
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/deps.cmake)
include(OsmoTetra)
include(RustProto)
include(YantraRadioPlugin)
include(YantraSoapy)

# SIMD kernels or the plain C++ fallback; both give the same results.
if(YANTRA_SIMD)
  add_compile_definitions(YANTRA_SIMD=1)
else()
  add_compile_definitions(YANTRA_SIMD=0)
endif()

# Plugins in bin/plugins so that the test host finds them next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(YANTRA_RADIO_PLUGIN_DIR ${CMAKE_BINARY_DIR}/bin/plugins)
file(MAKE_DIRECTORY ${YANTRA_RADIO_PLUGIN_DIR})

# The radio API (RAD-10): one public C header, nothing to link.
add_library(yantra_radio_api INTERFACE)
target_include_directories(yantra_radio_api INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
add_library(yantra::radio_api ALIAS yantra_radio_api)

# nlohmann/json (RAD-04), vendored header-only release (third_party/nlohmann_json).
add_library(yantra_json INTERFACE)
target_include_directories(yantra_json SYSTEM INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include>)
add_library(yantra::json ALIAS yantra_json)

if(YANTRA_RADIOS_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(common)
add_subdirectory(receiver)
add_subdirectory(gen)
add_subdirectory(plugins)
add_subdirectory(tools)
add_subdirectory(docs)
if(YANTRA_RADIOS_BUILD_TESTS)
  add_subdirectory(tests)
endif()

add_subdirectory(udev)

# Install (RAD-99), all relative to the prefix (never /etc; tested into a
# DESTDIR by tests/install):
#   lib/yantra-radios/plugins  the self-contained plugins (RAD-132): the
#                              plugins' CMakeLists; the example beacon is
#                              not installed (Yantra would offer it)
#   bin/                       yantra-radio-host, yantra-gen, yantra-hackrf
#   share/yantra-radios/       the scenarios with their scenes (RAD-74)
#   lib/udev/rules.d, lib/modprobe.d  udev rules and the RTL-SDR DVB-T
#                              blacklist (udev/)
#   share/licenses/yantra-radios  the licence bundle (RAD-137): LICENSE,
#                              LICENSES/, THIRD_PARTY.md, the licence
#                              texts of every bundled library and the
#                              index BUNDLE.md
#   share/doc/yantra-radios    README, CHANGELOG, the top-level docs and docs/
include(GNUInstallDirs)
install(DIRECTORY data/scenarios DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra-radios)
# The licence bundle is made at build time by tools/ci/licence_bundle.py
# from tools/deps/manifest.tsv (checked against THIRD_PARTY.md and
# third_party/ first), the same bundle the release packages carry.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(YANTRA_LICENCE_BUNDLE ${CMAKE_BINARY_DIR}/licence-bundle)
file(GLOB _bundle_inputs ${PROJECT_SOURCE_DIR}/LICENSES/*.txt)
add_custom_command(OUTPUT ${YANTRA_LICENCE_BUNDLE}/BUNDLE.md
  COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/ci/licence_bundle.py bundle
          ${PROJECT_SOURCE_DIR} ${YANTRA_LICENCE_BUNDLE}
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/ci/licence_bundle.py ${PROJECT_SOURCE_DIR}/tools/deps/manifest.tsv
          ${PROJECT_SOURCE_DIR}/THIRD_PARTY.md ${PROJECT_SOURCE_DIR}/LICENSE ${_bundle_inputs}
  COMMENT "Licence bundle (RAD-137) in ${YANTRA_LICENCE_BUNDLE}"
  VERBATIM)
add_custom_target(licence_bundle ALL DEPENDS ${YANTRA_LICENCE_BUNDLE}/BUNDLE.md)
install(DIRECTORY ${YANTRA_LICENCE_BUNDLE}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/yantra-radios)
file(GLOB _docs ${PROJECT_SOURCE_DIR}/docs/*.md)
install(FILES README.md CHANGELOG.md ARCHITECTURE.md BUILD.md CONTRIBUTING.md CONTRIBUTORS.md
              DEVICES.md REAL_WORLD_CHECKLIST.md ${_docs}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/yantra-radios)
install(DIRECTORY docs/plugins DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/yantra-radios)
# Where the README's images look for them.
install(DIRECTORY docs/img DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/yantra-radios/docs)

# Release folder <build>/release/v<version> (RAD-98, RAD-134, RAD-137):
# the plugin package with the licence bundle, the corresponding source,
# SHA256SUMS and the release notes (tools/ci/make_release.sh). Build the
# The tag is v${PROJECT_VERSION}; the packaged artefacts are built first.
set(_release_platform linux-x86_64)
if(WIN32)
  set(_release_platform windows-x86_64)
endif()
add_custom_target(release
  COMMAND ${CMAKE_COMMAND} -E env bash ${PROJECT_SOURCE_DIR}/tools/ci/make_release.sh
          ${CMAKE_BINARY_DIR} ${CMAKE_BINARY_DIR}/release/v${PROJECT_VERSION} v${PROJECT_VERSION}
          ${_release_platform}
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Release v${PROJECT_VERSION} in ${CMAKE_BINARY_DIR}/release/v${PROJECT_VERSION}"
  VERBATIM USES_TERMINAL)
foreach(_t licence_bundle hackrf soapysdr simulator pei yantra-radio-host yantra-gen yantra-hackrf docs-manual)
  if(TARGET ${_t})
    add_dependencies(release ${_t})
  endif()
endforeach()
