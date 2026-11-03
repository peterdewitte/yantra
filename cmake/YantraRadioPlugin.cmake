# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra_add_radio_plugin(<name> SOURCES <src>... [LIBS <lib>...]
#                         [OUTPUT_DIR <dir>])
#
# Builds a self-contained radio plugin module (RAD-131..133): no "lib"
# prefix, hidden visibility, only yantra_radio_entry exported (version
# script on Linux, .def file on Windows), every bundled static library's
# symbols hidden (--exclude-libs,ALL), the GCC runtime linked statically
# (static libstdc++/libgcc; on Windows also winpthreads, DEFERRED) and no
# undefined symbols. Output goes to ${YANTRA_RADIO_PLUGIN_DIR} unless
# OUTPUT_DIR is given. tools/ci/check_completeness.py checks the result.

set(YANTRA_RADIO_PLUGIN_MAP ${CMAKE_CURRENT_LIST_DIR}/radio_plugin.map)
set(YANTRA_RADIO_PLUGIN_DEF ${CMAKE_CURRENT_LIST_DIR}/radio_plugin.def)

function(yantra_add_radio_plugin name)
  cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SOURCES;LIBS" ${ARGN})
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
  set_target_properties(${name} PROPERTIES
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY ${ARG_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${ARG_OUTPUT_DIR})
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(${name} PRIVATE -static-libstdc++ -static-libgcc)
    if(WIN32)
      target_link_options(${name} PRIVATE -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
    else()
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
endfunction()
