# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio plugins Yantra ships (PLG-26): built from the yantra-radios
# submodule in a build of its own, or taken from a prebuilt yantra-radios
# install tree or release package. They land in the build tree's radio
# plugin folder (PLG-09, PLG-25):
#
#   <build>/lib/yantra/plugins/radios/  hackrf.so, simulator.so,
#                                       soapysdr.so, pei.so (shipped;
#                                       .dll on Windows)
#   <build>/tests/radio-plugins/        beacon.so, the radio API example
#                                       (RAD-64), and soapysdr_fake.so,
#                                       the generic SoapySDR plugin with
#                                       the fake SoapySDR module (RAD-92),
#                                       for the tests only
#
# Options:
#   YANTRA_RADIOS_PREBUILT   "" (default: build from the submodule) or a
#                            yantra-radios install tree, release package
#                            (.tar.gz) or release folder holding one
#                            (e.g. build/release/v0.1.0 of yantra-radios);
#                            no beacon plugin then.
#   YANTRA_BUILD_RADIOS      ON: build or copy the radio plugins; OFF: no
#                            radio plugins (the radio plugin tests skip).
#   YANTRA_RADIOS_PEI        (top level) OFF: pei.so is not shipped and its
#                            tests are not registered (PLG-01 check).
#   YANTRA_RADIOS_FAKE_SDR   ON: also build soapysdr_fake from the
#                            submodule (tests only; not with a prebuilt
#                            package, whose test then skips).
#   YANTRA_RADIOS_CACHE_DIR  where the bundled libraries of the radio
#                            plugins are built once (tools/deps/
#                            build_deps.sh of the submodule), in
#                            deps-<submodule commit>-<compiler>; default
#                            <build>/radios-cache. Survives reconfigures;
#                            point several trees at one folder to share it.
#
# The submodule build runs with the tree's compiler (its toolchain file,
# e.g. the SDK's clang, or its compilers), build type,
# YANTRA_SIMD and YANTRA_SANITIZER (sanitizer trees build the bundled
# libraries in-tree, instrumented, instead of from the cache), without its
# tests. Yantra itself compiles no source of the submodule (RAD-26, HW-01).
#
# Results for the rest of the build:
#   YANTRA_RADIO_PLUGIN_DIR       the shipped radio plugin folder above
#   YANTRA_RADIO_TEST_PLUGIN_DIR  the beacon folder above
#   YANTRA_RADIOS_KIND            build | prebuilt | none
#   YANTRA_RADIOS_PEI_SIM, YANTRA_RADIOS_PEI_SCENARIO  tests: the simulated
#                                 PEI terminal and its scenario (build only)
#   target yantra_radio_plugins   builds/copies the plugins (ALL)

include(ExternalProject)

option(YANTRA_BUILD_RADIOS "Build (or copy) the radio plugins Yantra ships (PLG-26)" ON)
set(YANTRA_RADIOS_PREBUILT "" CACHE PATH
    "Prebuilt yantra-radios install tree or release package (.tar.gz); empty = build the submodule")
set(YANTRA_RADIOS_CACHE_DIR ${PROJECT_BINARY_DIR}/radios-cache CACHE PATH
    "Cache of the radio plugins' bundled libraries (deps-<submodule commit>)")
option(YANTRA_RADIOS_FAKE_SDR
  "Build yantra-radios' test plugin soapysdr_fake (fake SoapySDR module) for the unplug/replug test (NFR-04)" ON)

# Plugin files: .so on Linux, .dll on Windows (RAD-131); programs .exe.
if(WIN32)
  set(YANTRA_RADIO_PLUGIN_SUFFIX .dll)
else()
  set(YANTRA_RADIO_PLUGIN_SUFFIX .so)
endif()
set(YANTRA_RADIO_PLUGIN_DIR ${PROJECT_BINARY_DIR}/lib/yantra/plugins/radios)
set(YANTRA_RADIO_TEST_PLUGIN_DIR ${PROJECT_BINARY_DIR}/tests/radio-plugins)
set(_yr_shipped hackrf simulator soapysdr pei)
if(NOT YANTRA_RADIOS_PEI)
  list(REMOVE_ITEM _yr_shipped pei)
endif()

if(NOT YANTRA_BUILD_RADIOS)
  set(YANTRA_RADIOS_KIND none)
  add_custom_target(yantra_radio_plugins)
  return()
endif()

file(MAKE_DIRECTORY ${YANTRA_RADIO_PLUGIN_DIR} ${YANTRA_RADIO_TEST_PLUGIN_DIR})

if(YANTRA_RADIOS_PREBUILT)
  # --- prebuilt install tree or release package ---------------------------
  set(YANTRA_RADIOS_KIND prebuilt)
  set(_root ${YANTRA_RADIOS_PREBUILT})
  # A release folder (build/release/<tag> of yantra-radios): its Linux package.
  if(IS_DIRECTORY ${_root} AND NOT EXISTS ${_root}/lib/yantra-radios/plugins)
    file(GLOB _pkg ${_root}/yantra-radios-*-linux-*.tar.gz)
    list(LENGTH _pkg _n)
    if(_n EQUAL 1)
      set(_root ${_pkg})
    endif()
  endif()
  if(_root MATCHES "\\.tar\\.gz$|\\.tgz$")
    if(NOT EXISTS ${_root})
      message(FATAL_ERROR "YANTRA_RADIOS_PREBUILT: ${_root} does not exist")
    endif()
    set(_unpacked ${PROJECT_BINARY_DIR}/radios/prebuilt)
    file(REMOVE_RECURSE ${_unpacked})
    file(ARCHIVE_EXTRACT INPUT ${_root} DESTINATION ${_unpacked})
    file(GLOB _top LIST_DIRECTORIES true ${_unpacked}/*)
    set(_root ${_unpacked})
    foreach(_d IN LISTS _top)
      if(EXISTS ${_d}/lib/yantra-radios/plugins)
        set(_root ${_d})
      endif()
    endforeach()
  endif()
  if(NOT EXISTS ${_root}/lib/yantra-radios/plugins)
    message(FATAL_ERROR "YANTRA_RADIOS_PREBUILT: no lib/yantra-radios/plugins in ${_root}")
  endif()
  set(YANTRA_RADIOS_STAGE ${_root})
  set(_copies)
  foreach(_p IN LISTS _yr_shipped)
    if(EXISTS ${_root}/lib/yantra-radios/plugins/${_p}${YANTRA_RADIO_PLUGIN_SUFFIX})
      list(APPEND _copies COMMAND ${CMAKE_COMMAND} -E copy_if_different
           ${_root}/lib/yantra-radios/plugins/${_p}${YANTRA_RADIO_PLUGIN_SUFFIX}
           ${YANTRA_RADIO_PLUGIN_DIR}/${_p}${YANTRA_RADIO_PLUGIN_SUFFIX})
    endif()
  endforeach()
  add_custom_target(yantra_radio_plugins ALL ${_copies}
    COMMENT "Radio plugins from ${YANTRA_RADIOS_PREBUILT} (PLG-26)" VERBATIM)
  message(STATUS "Radio plugins: prebuilt ${_root}")
  return()
endif()

# --- build from the submodule -------------------------------------------------
set(YANTRA_RADIOS_KIND build)
find_program(YANTRA_BASH bash REQUIRED)

# Cache key: the submodule commit (or, in a source package without git,
# a hash of the dependency manifest). build_deps.sh keeps its own stamp
# too, so a stale key never gives stale libraries.
execute_process(COMMAND git -C ${YANTRA_RADIOS_DIR} rev-parse --short=12 HEAD
                OUTPUT_VARIABLE _yr_commit OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _yr_git ERROR_QUIET)
if(NOT _yr_git EQUAL 0 OR _yr_commit STREQUAL "")
  file(SHA256 ${YANTRA_RADIOS_DIR}/tools/deps/manifest.tsv _yr_commit)
  string(SUBSTRING ${_yr_commit} 0 12 _yr_commit)
else()
  # A moved pin (checkout of another submodule commit) reconfigures, so
  # the cache key follows the commit.
  execute_process(COMMAND git -C ${YANTRA_RADIOS_DIR} rev-parse --path-format=absolute --git-path HEAD
                  OUTPUT_VARIABLE _yr_head OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE _yr_head_git ERROR_QUIET)
  if(_yr_head_git EQUAL 0 AND EXISTS "${_yr_head}")
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_yr_head})
  endif()
endif()
# A file that changes only with the radios commit: build steps that use the
# radio tools' output (the demo projects from yantra-gen) depend on it, so
# a moved pin makes them again (the radios build itself is BUILD_ALWAYS).
set(YANTRA_RADIOS_COMMIT_STAMP ${PROJECT_BINARY_DIR}/radios/commit.txt)
file(CONFIGURE OUTPUT ${YANTRA_RADIOS_COMMIT_STAMP} CONTENT "${_yr_commit}\n")

# The compiler (OA-02): the tree's toolchain file (clang from the cross
# SDK of the submodule, RAD-139, with its SDK folder) or, without one, the
# tree's compilers (the gcc tree). The bundled libraries are built with
# the same compiler (tools/deps/build_deps.sh reads CC/CXX or
# CMAKE_TOOLCHAIN_FILE from the environment); their cache folder is per
# compiler, so a gcc and a clang tree can share YANTRA_RADIOS_CACHE_DIR.
if(CMAKE_TOOLCHAIN_FILE)
  set(_yr_compiler -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
  set(_yr_deps_env CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
  if(YANTRA_SDK_DIR)
    list(APPEND _yr_compiler -DYANTRA_SDK_DIR=${YANTRA_SDK_DIR})
    list(APPEND _yr_deps_env YANTRA_SDK_DIR=${YANTRA_SDK_DIR})
  endif()
else()
  set(_yr_compiler -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
  set(_yr_deps_env CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER})
endif()
string(TOLOWER "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}" _yr_compiler_tag)
# The same clang builds for Linux and Windows: the target is part of the
# key (deps-<commit>-clang-<version>-windows).
if(CMAKE_CROSSCOMPILING)
  string(TOLOWER "${_yr_compiler_tag}-${CMAKE_SYSTEM_NAME}" _yr_compiler_tag)
endif()

set(_yr_args
  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  ${_yr_compiler}
  -DYANTRA_RADIOS_BUILD_TESTS=OFF
  -DYANTRA_SIMD=${YANTRA_SIMD}
  -DYANTRA_SANITIZER=${YANTRA_SANITIZER}
  -DYANTRA_WERROR=${YANTRA_WERROR})
set(_yr_depends)
if(YANTRA_SANITIZER STREQUAL "")
  set(YANTRA_RADIOS_DEPS_PREFIX ${YANTRA_RADIOS_CACHE_DIR}/deps-${_yr_commit}-${_yr_compiler_tag})
  ExternalProject_Add(yantra_radios_deps
    SOURCE_DIR ${YANTRA_RADIOS_DIR}
    BINARY_DIR ${PROJECT_BINARY_DIR}/radios/deps-step
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    # Returns at once while the prefix's stamp matches the sources.
    BUILD_COMMAND ${CMAKE_COMMAND} -E env ${_yr_deps_env}
                  ${YANTRA_BASH} ${YANTRA_RADIOS_DIR}/tools/deps/build_deps.sh
                  ${YANTRA_RADIOS_DEPS_PREFIX}
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON)
  list(APPEND _yr_args -DYANTRA_DEPS_PREFIX=${YANTRA_RADIOS_DEPS_PREFIX})
  set(_yr_depends DEPENDS yantra_radios_deps)
endif()

set(YANTRA_RADIOS_BINARY_DIR ${PROJECT_BINARY_DIR}/radios/build)
set(YANTRA_RADIOS_STAGE ${PROJECT_BINARY_DIR}/radios/install)
set(_copies)
foreach(_p IN LISTS _yr_shipped)
  list(APPEND _copies COMMAND ${CMAKE_COMMAND} -E copy_if_different
       ${YANTRA_RADIOS_STAGE}/lib/yantra-radios/plugins/${_p}${YANTRA_RADIO_PLUGIN_SUFFIX}
       ${YANTRA_RADIO_PLUGIN_DIR}/${_p}${YANTRA_RADIO_PLUGIN_SUFFIX})
endforeach()
list(APPEND _copies COMMAND ${CMAKE_COMMAND} -E copy_if_different
     ${YANTRA_RADIOS_BINARY_DIR}/bin/plugins/beacon${YANTRA_RADIO_PLUGIN_SUFFIX}
     ${YANTRA_RADIO_TEST_PLUGIN_DIR}/beacon${YANTRA_RADIO_PLUGIN_SUFFIX})

ExternalProject_Add(yantra_radios
  SOURCE_DIR ${YANTRA_RADIOS_DIR}
  BINARY_DIR ${YANTRA_RADIOS_BINARY_DIR}
  INSTALL_DIR ${YANTRA_RADIOS_STAGE}
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  CMAKE_ARGS ${_yr_args} -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
  ${_yr_depends}
  # The submodule's own ninja decides what is out of date.
  BUILD_ALWAYS ON
  LOG_CONFIGURE ON
  LOG_BUILD ON
  LOG_INSTALL ON
  LOG_OUTPUT_ON_FAILURE ON)
ExternalProject_Add_Step(yantra_radios copy_plugins
  ${_copies}
  DEPENDEES install
  ALWAYS ON
  COMMENT "Radio plugins into ${YANTRA_RADIO_PLUGIN_DIR} (PLG-26)")

# Test only: the generic SoapySDR plugin and the HackRF plugin built with
# yantra-radios' fake SoapySDR module in place of the real modules
# (soapysdr_fake, hackrf_fake, RAD-92), for the radio host's unplug/replug
# test (NFR-04; the fake module unplugs a device when a control file
# appears) and the radio windows' tests (step 9.6). A build of its own with the
# submodule's tests switched on, of that one target only (its closure:
# about half a minute); after the main radios build, with the same
# bundled libraries. Never installed or shipped.
set(_yr_fake_target)
if(YANTRA_RADIOS_FAKE_SDR)
  set(_yr_fake_args ${_yr_args})
  list(TRANSFORM _yr_fake_args REPLACE "^-DYANTRA_RADIOS_BUILD_TESTS=OFF$" "-DYANTRA_RADIOS_BUILD_TESTS=ON")
  ExternalProject_Add(yantra_radios_fake_sdr
    SOURCE_DIR ${YANTRA_RADIOS_DIR}
    BINARY_DIR ${PROJECT_BINARY_DIR}/radios/build-fake-sdr
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_ARGS ${_yr_fake_args}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target soapysdr_fake hackrf_fake
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    <BINARY_DIR>/tests/plugins/soapysdr_fake${YANTRA_RADIO_PLUGIN_SUFFIX}
                    ${YANTRA_RADIO_TEST_PLUGIN_DIR}/soapysdr_fake${YANTRA_RADIO_PLUGIN_SUFFIX}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    <BINARY_DIR>/tests/plugins/hackrf_fake${YANTRA_RADIO_PLUGIN_SUFFIX}
                    ${YANTRA_RADIO_TEST_PLUGIN_DIR}/hackrf_fake${YANTRA_RADIO_PLUGIN_SUFFIX}
    DEPENDS yantra_radios
    BUILD_ALWAYS ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_OUTPUT_ON_FAILURE ON)
  set(_yr_fake_target yantra_radios_fake_sdr)
  set(YANTRA_RADIOS_FAKE_SDR_SCENARIO ${YANTRA_RADIOS_DIR}/tests/fixtures/soapy/fake_two_cells.json)
endif()
# Test only: the simulated TETRA terminal behind a pseudo-terminal for the
# PEI plugin (the radio windows' tests, step 9.6); built by the radios build.
if(YANTRA_RADIOS_PEI)
  set(YANTRA_RADIOS_PEI_SIM ${YANTRA_RADIOS_BINARY_DIR}/bin/yantra-pei-sim${CMAKE_EXECUTABLE_SUFFIX})
  set(YANTRA_RADIOS_PEI_SCENARIO ${YANTRA_RADIOS_DIR}/tests/fixtures/pei/terminal_cells.json)
endif()
add_custom_target(yantra_radio_plugins ALL DEPENDS yantra_radios ${_yr_fake_target})
message(STATUS "Radio plugins: built from ${YANTRA_RADIOS_DIR} (${_yr_commit})")
