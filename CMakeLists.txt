# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Yantra top-level build (OA-01, OA-02). Layout: core/ (library without UI),
# app/ (desktop application), tools/ (command-line tools), plugins/,
# tests/ (ctest, TST-01) and the public plugin header include/yantra/.
# Radio plugins and their header come from the yantra-radios submodule in
# third_party/yantra-radios (PLG-26).
# Copied from yantra-v2 68ea20a (9540c71): CMakeLists.txt, adapted.

cmake_minimum_required(VERSION 3.20)

project(Yantra
  VERSION 0.1.0
  DESCRIPTION "Indoor TETRA coverage measurement on a software-defined radio"
  HOMEPAGE_URL "https://curtat.be"
  LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported; use e.g. "
                      "cmake -S . -B build -G Ninja")
endif()

option(YANTRA_SIMD "Build the SIMD kernels (OFF: plain C++ fallback only, TST-03)" ON)
option(YANTRA_BUILD_APP "Build the desktop application (needs the GUI libraries)" ON)
option(YANTRA_BUILD_TESTS "Build the test suite and register it with ctest" ON)
option(YANTRA_WERROR "Treat compiler warnings as errors (CI, TST-02)" OFF)
# PLG-01 check (step 11.2): the optional plugins can be left out of a
# build; every test of the core, the indoor work and the app then still
# passes (preset plugins-off, ctest plugins_off_check).
option(YANTRA_PLUGIN_PROBE "Build the probe feature plugin (remote API, PLG-24); OFF: no network listener" ON)
option(YANTRA_RADIOS_PEI "Ship the TETRA terminal radio plugin pei (RAD-80, TRM-01)" ON)
# NFR-01/NFR-09: the AGPL source link of About, the remote API status
# reply and the probe plugin, and the packages' source field.
set(YANTRA_SOURCE_URL "https://github.com/peterdewitte/yantra" CACHE STRING
    "Source code link shown in the About dialog and the remote API (NFR-01, NFR-09, UI-15)")
set(YANTRA_SANITIZER "" CACHE STRING
    "Developer sanitizer build (TST-05): empty, asan-ubsan or tsan")
set_property(CACHE YANTRA_SANITIZER PROPERTY STRINGS "" asan-ubsan tsan)
# TST-12: the fuzz targets (tests/fuzz) as libFuzzer binaries (clang only);
# OFF: they replay their corpus with seeded mutations under ctest.
option(YANTRA_FUZZ "Build the fuzz targets for libFuzzer (clang, TST-12)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# All executables (app, tools, test_<module>) end up in <build>/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# NFR-09 (no personal data): binaries name no folder of the machine they
# were built on, in debug information or in __FILE__ (a home folder is
# part of these paths). Source paths become relative to the repository,
# build paths start with "build". Set before every target; the last
# matching map wins, and the build tree may lie inside the source tree.
# The test install_build_paths checks the installed files (tests/install).
# As yantra-radios tools/deps/deps.cmake.
add_compile_options(-ffile-prefix-map=${PROJECT_SOURCE_DIR}=.
                    -ffile-prefix-map=${PROJECT_BINARY_DIR}=build)

# NFR-09: installed programs find the libraries Yantra ships next to them
# (PDFium in <prefix>/lib/yantra), wherever the prefix is (.deb, DESTDIR).
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib/yantra")

# Windows cross build (OA-02, HW-06): the libraries come from Yantra's
# MSYS2 UCRT64 packages in the SDK's sysroot (tools/windows/msys2.lock).
# pkg-config reads only their .pc files, with the prefix taken from where
# each file lies (MSYS2 writes /ucrt64); curl's CMake package file wants
# its curl.exe, which the SDK leaves out, so FindCURL looks for the
# library itself.
if(WIN32 AND CMAKE_CROSSCOMPILING)
  set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/lib/pkgconfig")
  set(ENV{PKG_CONFIG_PATH} "")
  set(PKG_CONFIG_ARGN --define-prefix)
  set(CURL_NO_CURL_CMAKE ON)
endif()

# Build-machine checks of a cross-compiled tree (the Windows tree, whose
# ctest runs on Windows): tests that check the sources or run the build
# machine's tools (Doxygen, reuse, pandoc, the lints, the SDK's compilers)
# put ${YANTRA_BUILD_HOST_TEST} before their command and are listed in
# yantra_build_host_tests(): label build_host, skipped on Windows (exit
# 77), run on Linux with `ctest --test-dir build-win -L build_host`.
# Copied from yantra-radios 6fac3ee: CMakeLists.txt. No-ops in a native tree.
set(YANTRA_BUILD_HOST_TEST "")
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(YANTRA_BUILD_HOST_TEST ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/cmake/build_host_test.py)
endif()
function(yantra_build_host_tests)
  if(YANTRA_BUILD_HOST_TEST)
    set_tests_properties(${ARGN} PROPERTIES SKIP_RETURN_CODE 77)
    set_property(TEST ${ARGN} APPEND PROPERTY LABELS build_host)
  endif()
endfunction()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(YantraWarnings)
include(YantraSanitizers)
include(YantraPdfium)

# Public plugin API headers: plugin.h (C ABI, PLG-03) and plugin.hpp (C++
# helpers).
add_library(yantra_plugin_api INTERFACE)
add_library(yantra::plugin_api ALIAS yantra_plugin_api)
target_include_directories(yantra_plugin_api INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

# Radio API header from the yantra-radios submodule (PLG-26).
# nlohmann/json 3.12.0, vendored header-only (spec 16.2).
add_library(yantra_json INTERFACE)
add_library(yantra::json ALIAS yantra_json)
target_include_directories(yantra_json SYSTEM INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/nlohmann_json/include>)

include(YantraRadios)
# The radio plugins Yantra ships, built from the submodule (or a prebuilt
# yantra-radios package) into <build>/lib/yantra/plugins/radios (PLG-26).
include(YantraRadiosBuild)

if(YANTRA_BUILD_TESTS)
  enable_testing()
endif()

# Build-time data of a cross-compiled tree (the Windows tree, OA-02): the
# demo projects (DEMO-05, DEMO-12) and the TST-10 record files are made by
# programs (yantra-demo, the TETRA plugin, yantra-gen, yantra-radio-host)
# that cannot run on the build machine as .exe. They come from a Linux
# tree of the same sources, YANTRA_HOST_BUILD_DIR (default: the default
# tree, <source>/build), built first; the data files are the same on both
# platforms. The tests themselves run the tree's own programs.
if(CMAKE_CROSSCOMPILING)
  set(YANTRA_HOST_BUILD_DIR ${PROJECT_SOURCE_DIR}/build CACHE PATH
      "Linux build tree whose programs make the build-time data of this cross-compiled tree")
  if(NOT EXISTS ${YANTRA_HOST_BUILD_DIR}/CMakeCache.txt)
    message(FATAL_ERROR "Cross build: no Linux tree in YANTRA_HOST_BUILD_DIR (${YANTRA_HOST_BUILD_DIR}): "
                        "configure and build it first (cmake --preset default; ninja -C build), "
                        "it makes the demo projects and record files of this tree (BUILD.md \"Windows\")")
  endif()
  set(YANTRA_HOST_DEMO ${YANTRA_HOST_BUILD_DIR}/bin/yantra-demo)
  set(YANTRA_HOST_TETRA_PLUGIN ${YANTRA_HOST_BUILD_DIR}/lib/yantra/plugins/tetra.so)
  set(YANTRA_HOST_RADIOS_BIN ${YANTRA_HOST_BUILD_DIR}/radios/install/bin)
  set(YANTRA_HOST_SCENARIO_DIR ${YANTRA_HOST_BUILD_DIR}/radios/install/share/yantra-radios/scenarios)
endif()

find_package(Threads REQUIRED)
add_subdirectory(core)
if(YANTRA_BUILD_APP)
  add_subdirectory(app)
endif()
add_subdirectory(tools)
add_subdirectory(plugins)
add_subdirectory(docs)
add_subdirectory(packaging)

if(YANTRA_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# Installed layout (PLG-09, PLG-26): <prefix>/bin/yantra, the radio plugins
# in <prefix>/lib/yantra/plugins/radios (the application's radio plugin
# folder, found relative to the executable) with yantra-radios' licence
# bundle, udev rules and RTL-SDR blacklist. Packaging: phase 12.
if(NOT YANTRA_RADIOS_KIND STREQUAL "none")
  install(DIRECTORY ${YANTRA_RADIO_PLUGIN_DIR}/ DESTINATION lib/yantra/plugins/radios
          FILES_MATCHING PATTERN "*${YANTRA_RADIO_PLUGIN_SUFFIX}")
  install(DIRECTORY ${YANTRA_RADIOS_STAGE}/share/licenses/yantra-radios
          DESTINATION share/licenses OPTIONAL)
  install(DIRECTORY ${YANTRA_RADIOS_STAGE}/lib/udev ${YANTRA_RADIOS_STAGE}/lib/modprobe.d
          DESTINATION lib OPTIONAL)
endif()

# PDFium (IND-02): the prebuilt library next to Yantra's own libraries
# with its licence texts (THIRD_PARTY.md). The installed programs find it
# through their run path $ORIGIN/../lib/yantra (CMAKE_INSTALL_RPATH).
if(YANTRA_PDFIUM_FOUND)
  install(FILES $<TARGET_FILE:yantra_pdfium_lib> DESTINATION lib/yantra)
  install(DIRECTORY ${YANTRA_PDFIUM_LICENSES}/ DESTINATION share/licenses/pdfium)
endif()

# Windows (OA-02): the DLLs of the MSYS2 UCRT64 packages and PDFium that
# the programs import, with what they import in turn, next to the programs
# in <build>/bin (tools/windows/copy_dlls.py; list in yantra-dlls.txt).
# The build tree also gets Mesa's opengl32.dll (llvmpipe, 16.2 test tool):
# the tests render off-screen in the test VM, whose display adapter has no
# OpenGL 3 (BUILD.md "Windows"). Never packaged.
if(WIN32 AND CMAKE_CROSSCOMPILING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_dll_search --search ${CMAKE_SYSROOT}/bin)
  if(YANTRA_PDFIUM_FOUND)
    get_target_property(_pdfium_dll yantra_pdfium_lib IMPORTED_LOCATION)
    get_filename_component(_pdfium_bin ${_pdfium_dll} DIRECTORY)
    list(APPEND _dll_search --search ${_pdfium_bin})
  endif()
  set(_dll_direct glfw3.dll libsqlite3-0.dll libzstd.dll libcurl-4.dll libhpdf.dll
                  libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
  if(YANTRA_PDFIUM_FOUND)
    list(APPEND _dll_direct pdfium.dll)
  endif()
  if(YANTRA_BUILD_TESTS)
    list(APPEND _dll_direct opengl32.dll)
  endif()
  list(TRANSFORM _dll_direct PREPEND "--extra=")
  # The fuzz targets (TST-12) have their own folder.
  add_custom_target(yantra_runtime_dlls ALL
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/windows/copy_dlls.py
            --dest ${PROJECT_BINARY_DIR}/bin ${_dll_search} ${_dll_direct}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/windows/copy_dlls.py
            --dest ${PROJECT_BINARY_DIR}/fuzz ${_dll_search} ${_dll_direct}
    COMMENT "Runtime DLLs into ${PROJECT_BINARY_DIR}/bin (OA-02)"
    VERBATIM)
endif()

message(STATUS "Yantra ${PROJECT_VERSION}: SIMD=${YANTRA_SIMD} "
               "APP=${YANTRA_BUILD_APP} TESTS=${YANTRA_BUILD_TESTS} "
               "SANITIZER='${YANTRA_SANITIZER}' "
               "RADIO_API=${YANTRA_RADIO_API_VERSION_MAJOR}.${YANTRA_RADIO_API_VERSION_MINOR} "
               "RADIOS=${YANTRA_RADIOS_KIND} PDFIUM=${YANTRA_PDFIUM_FOUND} "
               "PROBE=${YANTRA_PLUGIN_PROBE} PEI=${YANTRA_RADIOS_PEI}")
