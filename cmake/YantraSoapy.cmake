# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Helpers of the SoapySDR plugins (RAD-54, RAD-92).
#
# yantra_soapy_module_table(<var> [ENTRIES e...]): the module table of a
#   plugin (Options::modules): the YANTRA_SOAPY_MODULES entries (or the
#   given ones) joined with ",".
# yantra_fake_soapy_library(<target> DRIVER <name>): a static library with
#   the fake SoapySDR module of tests/fake_soapy registered as driver
#   <name> (test builds only, never installed); link it whole together
#   with yantra::soapysdr. 5.3's HackRF plugin tests use DRIVER hackrf.
function(yantra_soapy_module_table var)
  cmake_parse_arguments(ARG "" "" "ENTRIES" ${ARGN})
  if(NOT ARG_ENTRIES)
    set(ARG_ENTRIES ${YANTRA_SOAPY_MODULES})
  endif()
  string(REPLACE ";" "," _table "${ARG_ENTRIES}")
  set(${var} "${_table}" PARENT_SCOPE)
endfunction()

set(YANTRA_FAKE_SOAPY_SOURCE ${PROJECT_SOURCE_DIR}/tests/radio/fake_soapy/fake_soapy.cpp)

function(yantra_fake_soapy_library target)
  cmake_parse_arguments(ARG "" "DRIVER" "" ${ARGN})
  if(NOT ARG_DRIVER)
    set(ARG_DRIVER yantrafake)
  endif()
  add_library(${target} STATIC ${YANTRA_FAKE_SOAPY_SOURCE})
  target_compile_definitions(${target} PRIVATE YANTRA_FAKE_SOAPY_DRIVER="${ARG_DRIVER}")
  target_link_libraries(${target}
    PUBLIC yantra::gen
    PRIVATE yantra_warnings yantra::json $<COMPILE_ONLY:yantra_soapysdr_whole>
            $<COMPILE_ONLY:yantra_soapysdr>)
  set_target_properties(${target} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  add_library(${target}_whole INTERFACE)
  target_link_libraries(${target}_whole INTERFACE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,${target}>" yantra::soapysdr yantra::gen)
endfunction()
