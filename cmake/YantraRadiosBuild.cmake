# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio plugins Yantra ships (PLG-26): built from the yantra-radios
# submodule in a build of its own, or taken from a prebuilt yantra-radios
# install tree or release package. They land in the build tree's radio
# plugin folder (PLG-09, PLG-25):
#
#   <build>/lib/yantra/plugins/radios/  hackrf.so, simulator.so,
#                                       soapysdr.so, pei.so (shipped)
#   <build>/tests/radio-plugins/        beacon.so, the radio API example
#                                       (RAD-64), for the tests only
#
# Options:
#   YANTRA_RADIOS_PREBUILT   "" (default: build from the submodule) or a
#                            yantra-radios install tree, release package
#                            (.tar.gz) or release folder holding one
#                            (e.g. build/release/v0.1.0 of yantra-radios);
#                            no beacon plugin then.
#   YANTRA_BUILD_RADIOS      ON: build or copy the radio plugins; OFF: no
#                            radio plugins (the radio plugin tests skip).
#   YANTRA_RADIOS_CACHE_DIR  where the bundled libraries of the radio
#                            plugins are built once (tools/deps/
#                            build_deps.sh of the submodule), in
#                            deps-<submodule commit>; default
#                            <build>/radios-cache. Survives reconfigures;
#                            point several trees at one folder to share it.
#
# The submodule build runs with the tree's compiler, build type,
# YANTRA_SIMD and YANTRA_SANITIZER (sanitizer trees build the bundled
# libraries in-tree, instrumented, instead of from the cache), without its
# tests. Yantra itself compiles no source of the submodule (RAD-26, HW-01).
#
# Results for the rest of the build:
#   YANTRA_RADIO_PLUGIN_DIR       the shipped radio plugin folder above
#   YANTRA_RADIO_TEST_PLUGIN_DIR  the beacon folder above
#   YANTRA_RADIOS_KIND            build | prebuilt | none
#   target yantra_radio_plugins   builds/copies the plugins (ALL)

include(ExternalProject)

option(YANTRA_BUILD_RADIOS "Build (or copy) the radio plugins Yantra ships (PLG-26)" ON)
set(YANTRA_RADIOS_PREBUILT "" CACHE PATH
    "Prebuilt yantra-radios install tree or release package (.tar.gz); empty = build the submodule")
set(YANTRA_RADIOS_CACHE_DIR ${PROJECT_BINARY_DIR}/radios-cache CACHE PATH
    "Cache of the radio plugins' bundled libraries (deps-<submodule commit>)")

set(YANTRA_RADIO_PLUGIN_DIR ${PROJECT_BINARY_DIR}/lib/yantra/plugins/radios)
set(YANTRA_RADIO_TEST_PLUGIN_DIR ${PROJECT_BINARY_DIR}/tests/radio-plugins)
set(_yr_shipped hackrf simulator soapysdr pei)

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
    if(EXISTS ${_root}/lib/yantra-radios/plugins/${_p}.so)
      list(APPEND _copies COMMAND ${CMAKE_COMMAND} -E copy_if_different
           ${_root}/lib/yantra-radios/plugins/${_p}.so ${YANTRA_RADIO_PLUGIN_DIR}/${_p}.so)
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

set(_yr_args
  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DYANTRA_RADIOS_BUILD_TESTS=OFF
  -DYANTRA_SIMD=${YANTRA_SIMD}
  -DYANTRA_SANITIZER=${YANTRA_SANITIZER}
  -DYANTRA_WERROR=${YANTRA_WERROR})
set(_yr_depends)
if(YANTRA_SANITIZER STREQUAL "")
  set(YANTRA_RADIOS_DEPS_PREFIX ${YANTRA_RADIOS_CACHE_DIR}/deps-${_yr_commit})
  ExternalProject_Add(yantra_radios_deps
    SOURCE_DIR ${YANTRA_RADIOS_DIR}
    BINARY_DIR ${PROJECT_BINARY_DIR}/radios/deps-step
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    # Returns at once while the prefix's stamp matches the sources.
    BUILD_COMMAND ${YANTRA_BASH} ${YANTRA_RADIOS_DIR}/tools/deps/build_deps.sh
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
       ${YANTRA_RADIOS_STAGE}/lib/yantra-radios/plugins/${_p}.so ${YANTRA_RADIO_PLUGIN_DIR}/${_p}.so)
endforeach()
list(APPEND _copies COMMAND ${CMAKE_COMMAND} -E copy_if_different
     ${YANTRA_RADIOS_BINARY_DIR}/bin/plugins/beacon.so ${YANTRA_RADIO_TEST_PLUGIN_DIR}/beacon.so)

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
add_custom_target(yantra_radio_plugins ALL DEPENDS yantra_radios)
message(STATUS "Radio plugins: built from ${YANTRA_RADIOS_DIR} (${_yr_commit})")
