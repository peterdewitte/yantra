# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Yantra top-level build (OA-01, OA-02, PLG-29): one CMake project, one
# ctest suite, for the application and the radio parts (RAD-141):
#   radio-api/     the radio API header yantra/radio.h (RAD-02, RAD-10)
#   plugins/       one folder per plugin: the radio plugins (hackrf, soapy,
#                  pei, simulator, example) and Yantra's technology and
#                  feature plugins (tetra, probe, beacon)
#   radio-common/  code shared by the radio plugins only (receiver, TETRA
#                  decoder, SoapySDR and TETRA glue, Rust protocol crate)
#   gen/           TETRA test-signal generator (RAD-70)
#   core/, app/    Yantra's measurement core and the desktop application
#   plugin-api/    Yantra's plugin API header yantra/plugin.h (PLG-03)
#   tools/, docs/, tests/ (tests/radio: the radio parts' tests), data/,
#   udev/, packaging/, third_party/
# YANTRA_RADIO_PARTS_ONLY builds the radio parts alone (RAD-141);
# YANTRA_BUILD_RADIOS=OFF leaves them out (no radio plugins).
# Copied from yantra-v2 68ea20a (9540c71): CMakeLists.txt, adapted.

cmake_minimum_required(VERSION 3.20)

project(Yantra
  VERSION 0.2.0
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
# The radio parts (PLG-29, RAD-141): radio-common/, gen/, the radio
# plugins and tools and their tests (tests/radio, in this tree's ctest).
option(YANTRA_BUILD_RADIOS "Build the radio parts: radio-common, the radio plugins, gen and the radio tools" ON)
option(YANTRA_RADIO_PARTS_ONLY "Build only the radio parts, without the application (RAD-141)" OFF)
option(YANTRA_RADIOS_TESTS "Build and register the radio parts' tests (tests/radio, RAD-141)" ON)
if(YANTRA_RADIO_PARTS_ONLY)
  if(NOT YANTRA_BUILD_RADIOS)
    message(FATAL_ERROR "YANTRA_RADIO_PARTS_ONLY needs YANTRA_BUILD_RADIOS=ON")
  endif()
  set(YANTRA_BUILD_APP OFF)
endif()
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
# A program started from the build tree finds the sources' data/ through
# this file (app/model data_paths.h); unlike CMakeCache.txt it is copied
# with the tree to the Windows test machine.
file(CONFIGURE OUTPUT ${PROJECT_BINARY_DIR}/yantra-source-dir.txt CONTENT "${PROJECT_SOURCE_DIR}\n")

# NFR-09 (no personal data): binaries name no folder of the machine they
# were built on, in debug information or in __FILE__ (a home folder is
# part of these paths). Source paths become relative to the repository,
# build paths start with "build". Set before every target; the last
# matching map wins, and the build tree may lie inside the source tree.
# The test install_build_paths checks the installed files (tests/install).
# As yantra-radios tools/deps/deps.cmake.
add_compile_options(-ffile-prefix-map=${PROJECT_SOURCE_DIR}=.
                    -ffile-prefix-map=${PROJECT_BINARY_DIR}=build)
# The cross SDK (YANTRA_SDK_DIR, outside the tree) holds the compiler's,
# libstdc++'s and the MSYS2 packages' headers: "sdk/...". Its folder name
# may start with the source folder's (~/yantra-sdk next to ~/yantra), so
# it comes after that map. As yantra-radios tools/deps/deps.cmake 6fac3ee.
if(YANTRA_SDK_DIR)
  get_filename_component(_yantra_sdk_real ${YANTRA_SDK_DIR} REALPATH)
  foreach(_d IN LISTS YANTRA_SDK_DIR _yantra_sdk_real)
    add_compile_options(-ffile-prefix-map=${_d}=sdk)
  endforeach()
  unset(_yantra_sdk_real)
endif()

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

# Windows tree (cross-compiled, OA-02, RAD-131): its ctest runs natively on
# Windows (the test VM, BUILD.md). YANTRA_WINDOWS_TEST_RUNNER=wsl instead
# runs every .exe from Linux through WSL interop (tools/sdk/wsl_run.sh
# --in-place; WSL2 only).
set(YANTRA_WINDOWS_TEST_RUNNER "" CACHE STRING
  "Cross-compiled Windows tree: '' (ctest runs on Windows) or 'wsl' (ctest on WSL2 runs the .exe through interop)")
set_property(CACHE YANTRA_WINDOWS_TEST_RUNNER PROPERTY STRINGS "" wsl)
if(NOT YANTRA_WINDOWS_TEST_RUNNER STREQUAL "" AND NOT YANTRA_WINDOWS_TEST_RUNNER STREQUAL "wsl")
  message(FATAL_ERROR "YANTRA_WINDOWS_TEST_RUNNER must be '' or 'wsl'")
endif()
if(CMAKE_CROSSCOMPILING AND WIN32 AND YANTRA_WINDOWS_TEST_RUNNER STREQUAL "wsl"
   AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
  execute_process(COMMAND ${PROJECT_SOURCE_DIR}/tools/sdk/wsl_run.sh --check
                  RESULT_VARIABLE _yantra_wsl OUTPUT_QUIET ERROR_QUIET)
  if(_yantra_wsl EQUAL 0)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${PROJECT_SOURCE_DIR}/tools/sdk/wsl_run.sh --in-place)
  else()
    message(WARNING "YANTRA_WINDOWS_TEST_RUNNER=wsl: no WSL interop here; "
                    "the tree's ctest is for a Windows machine (the test VM, BUILD.md)")
  endif()
  unset(_yantra_wsl)
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
include(YantraTestAreas)
include(YantraFuzz)
if(NOT YANTRA_RADIO_PARTS_ONLY)
  include(YantraPdfium)
  include(YantraWindowsResources)
endif()

# nlohmann/json 3.12.0, vendored header-only (spec 16.2, RAD-04).
add_library(yantra_json INTERFACE)
add_library(yantra::json ALIAS yantra_json)
target_include_directories(yantra_json SYSTEM INTERFACE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/nlohmann_json/include>)

if(YANTRA_BUILD_TESTS)
  enable_testing()
  include(YantraTests)
  # doctest 2.4.12, vendored header-only (spec 16.2, RAD-04): every test
  # binary of tests/ and tests/radio.
  add_library(yantra_doctest INTERFACE)
  target_include_directories(yantra_doctest SYSTEM INTERFACE ${PROJECT_SOURCE_DIR}/third_party/doctest)
endif()
# The radio parts' test suite (tests/radio) and their test plugins (the
# example beacon and the fake-SDR builds, which the application's tests
# load too).
if(YANTRA_BUILD_TESTS AND YANTRA_BUILD_RADIOS AND YANTRA_RADIOS_TESTS)
  set(YANTRA_RADIOS_BUILD_TESTS ON)
else()
  set(YANTRA_RADIOS_BUILD_TESTS OFF)
endif()
set(YANTRA_RADIO_TEST_PLUGINS ${YANTRA_BUILD_TESTS})

# Build-time data of a cross-compiled tree (the Windows tree, OA-02): the
# demo projects (DEMO-05, DEMO-12) and the TST-10 record files are made by
# programs (yantra-demo, the TETRA plugin, yantra-gen, yantra-radio-host)
# that cannot run on the build machine as .exe. They come from a Linux
# tree of the same sources, YANTRA_HOST_BUILD_DIR (default: the default
# tree, <source>/build), built first; the data files are the same on both
# platforms. The tests themselves run the tree's own programs.
if(CMAKE_CROSSCOMPILING AND NOT YANTRA_RADIO_PARTS_ONLY)
  set(YANTRA_HOST_BUILD_DIR ${PROJECT_SOURCE_DIR}/build CACHE PATH
      "Linux build tree whose programs make the build-time data of this cross-compiled tree")
  if(NOT EXISTS ${YANTRA_HOST_BUILD_DIR}/CMakeCache.txt)
    message(FATAL_ERROR "Cross build: no Linux tree in YANTRA_HOST_BUILD_DIR (${YANTRA_HOST_BUILD_DIR}): "
                        "configure and build it first (cmake --preset default; ninja -C build), "
                        "it makes the demo projects and record files of this tree (BUILD.md \"Windows\")")
  endif()
  set(YANTRA_HOST_DEMO ${YANTRA_HOST_BUILD_DIR}/bin/yantra-demo)
  set(YANTRA_HOST_TETRA_PLUGIN ${YANTRA_HOST_BUILD_DIR}/lib/yantra/plugins/tetra.so)
  set(YANTRA_HOST_RADIOS_BIN ${YANTRA_HOST_BUILD_DIR}/bin)
  set(YANTRA_HOST_SCENARIO_DIR ${PROJECT_SOURCE_DIR}/data/scenarios)
endif()

# Radio plugin folders of the build tree (PLG-09, PLG-25): the shipped
# radio plugins where the application finds them, the test plugins (the
# example beacon, the fake-SDR builds) apart; .so on Linux, .dll on
# Windows (RAD-131).
set(YANTRA_RADIO_PLUGIN_DIR ${PROJECT_BINARY_DIR}/lib/yantra/plugins/radios)
set(YANTRA_RADIO_TEST_PLUGIN_DIR ${PROJECT_BINARY_DIR}/tests/radio-plugins)
if(WIN32)
  set(YANTRA_RADIO_PLUGIN_SUFFIX .dll)
else()
  set(YANTRA_RADIO_PLUGIN_SUFFIX .so)
endif()

find_package(Threads REQUIRED)
# The radio API (radio-api/), used by both sides, and Yantra's plugin API
# (plugin-api/, the application and its own plugins).
yantra_add_area_subdirectory(radio-api radio-api)
add_subdirectory(plugin-api)
# The radio parts (RAD-141): shared setup, then radio-common/ and gen/;
# their plugins and tools follow in plugins/ and tools/.
if(YANTRA_BUILD_RADIOS)
  include(YantraRadioParts)
  yantra_add_area_subdirectory(radio-common radio-common)
  yantra_add_area_subdirectory(gen gen)
  add_subdirectory(udev)
endif()
# Yantra (not in the radio-parts-only build).
if(NOT YANTRA_RADIO_PARTS_ONLY)
  add_subdirectory(core)
  if(YANTRA_BUILD_APP)
    add_subdirectory(app)
  endif()
endif()
add_subdirectory(plugins)
add_subdirectory(tools)
yantra_add_area_subdirectory(docs docs)
if(NOT YANTRA_RADIO_PARTS_ONLY)
  yantra_add_area_subdirectory(packaging packaging)
endif()

if(YANTRA_BUILD_TESTS)
  if(NOT YANTRA_RADIO_PARTS_ONLY)
    add_subdirectory(tests)
  endif()
  if(YANTRA_RADIOS_BUILD_TESTS)
    add_subdirectory(tests/radio)
  endif()
endif()

# Installed layout (PLG-09, PLG-29): <prefix>/bin/yantra, Yantra's plugins
# in <prefix>/lib/yantra/plugins and the radio plugins in
# <prefix>/lib/yantra/plugins/radios (the application's radio plugin
# folder, found relative to the executable), with the radio parts'
# licence bundle, udev rules, RTL-SDR blacklist, yantra-gen and its
# scenarios (cmake/YantraRadioParts.cmake and the folders' own rules).
# Packaging: packaging/.

# PDFium (IND-02): the prebuilt library next to Yantra's own libraries
# with its licence texts (THIRD_PARTY.md). The installed programs find it
# through their run path $ORIGIN/../lib/yantra (CMAKE_INSTALL_RPATH); on
# Windows a DLL is found next to the program (bin).
if(YANTRA_PDFIUM_FOUND AND NOT YANTRA_RADIO_PARTS_ONLY)
  if(WIN32)
    install(FILES $<TARGET_FILE:yantra_pdfium_lib> DESTINATION bin)
  else()
    install(FILES $<TARGET_FILE:yantra_pdfium_lib> DESTINATION lib/yantra)
  endif()
  install(DIRECTORY ${YANTRA_PDFIUM_LICENSES}/ DESTINATION share/licenses/pdfium)
endif()

# Windows (OA-02): the DLLs of the MSYS2 UCRT64 packages and PDFium that
# the programs import, with what they import in turn, next to the programs
# in <build>/bin (tools/windows/copy_dlls.py; list in yantra-dlls.txt).
# The build tree also gets Mesa's opengl32.dll (llvmpipe, 16.2 test tool):
# the tests render off-screen in the test VM, whose display adapter has no
# OpenGL 3 (BUILD.md "Windows"). Never packaged.
if(WIN32 AND CMAKE_CROSSCOMPILING AND NOT YANTRA_RADIO_PARTS_ONLY)
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
  # The installed programs and plugins get the DLLs they import next to
  # yantra.exe (NFR-09; a plugin's imports are searched in the program's
  # folder too, platform::DynamicLibrary), never Mesa: the installed app
  # uses the system's OpenGL driver. Runs after every other install rule
  # (the subfolders' rules come first). List: <build>/packaging/installed-dlls.txt.
  install(CODE "
    set(_prefix \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
    execute_process(COMMAND \"${Python3_EXECUTABLE}\" \"${PROJECT_SOURCE_DIR}/tools/windows/copy_dlls.py\"
                            --dest \"\${_prefix}/bin\" --list \"${PROJECT_BINARY_DIR}/packaging/installed-dlls.txt\"
                            ${_dll_search}
                            \"\${_prefix}/bin\" \"\${_prefix}/lib/yantra/plugins\" \"\${_prefix}/lib/yantra/plugins/radios\"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR \"copy_dlls.py failed (\${_rc})\")
    endif()")
endif()

message(STATUS "Yantra ${PROJECT_VERSION}: SIMD=${YANTRA_SIMD} "
               "APP=${YANTRA_BUILD_APP} TESTS=${YANTRA_BUILD_TESTS} "
               "SANITIZER='${YANTRA_SANITIZER}' "
               "RADIO_API=${YANTRA_RADIO_API_VERSION_MAJOR}.${YANTRA_RADIO_API_VERSION_MINOR} "
               "RADIOS=${YANTRA_BUILD_RADIOS} RADIO_PARTS_ONLY=${YANTRA_RADIO_PARTS_ONLY} "
               "RADIO_TESTS=${YANTRA_RADIOS_BUILD_TESTS} PDFIUM=${YANTRA_PDFIUM_FOUND} "
               "PROBE=${YANTRA_PLUGIN_PROBE} PEI=${YANTRA_RADIOS_PEI}")
