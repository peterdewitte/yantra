# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Windows resources of Yantra's programs (BRD-06, NFR-09): the Yantra icon
# and the version information, compiled by the SDK's llvm-rc.
#
#   yantra_windows_resources(<target> DESCRIPTION <text> [GUI])
#
# The icon <build>/packaging/yantra.ico is made once from the logo PNGs as
# supplied (packaging/windows/make_ico.py, target yantra_ico); the setup
# .exe uses the same file. GUI: a windowed program (WIN32_EXECUTABLE: no
# console window when started from the Start menu; it attaches to the
# console it was started from, platform::attach_parent_console()). No-op
# on other systems.
include_guard(GLOBAL)

set(YANTRA_ICO ${PROJECT_BINARY_DIR}/packaging/yantra.ico)
if(WIN32)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_logo_pngs)
  foreach(_size 16 24 32 48 64 128 256)
    list(APPEND _logo_pngs ${PROJECT_SOURCE_DIR}/data/logo/png/yantra_logo_${_size}.png)
  endforeach()
  add_custom_command(OUTPUT ${YANTRA_ICO}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/packaging/windows/make_ico.py
            --png-dir ${PROJECT_SOURCE_DIR}/data/logo/png --out ${YANTRA_ICO}
    DEPENDS ${PROJECT_SOURCE_DIR}/packaging/windows/make_ico.py ${_logo_pngs}
    COMMENT "Windows icon from data/logo/png (BRD-06)"
    VERBATIM)
  add_custom_target(yantra_ico DEPENDS ${YANTRA_ICO})
endif()

function(yantra_windows_resources target)
  if(NOT WIN32)
    return()
  endif()
  cmake_parse_arguments(ARG "GUI" "DESCRIPTION" "" ${ARGN})
  set(_name ${target})
  get_target_property(_out ${target} OUTPUT_NAME)
  if(_out)
    set(_name ${_out})
  endif()
  set(_description "${ARG_DESCRIPTION}")
  set(_ico ${YANTRA_ICO})
  set(_rc ${CMAKE_CURRENT_BINARY_DIR}/${target}_resources.rc)
  configure_file(${PROJECT_SOURCE_DIR}/packaging/windows/version.rc.in ${_rc} @ONLY)
  target_sources(${target} PRIVATE ${_rc})
  set_source_files_properties(${_rc} PROPERTIES OBJECT_DEPENDS ${YANTRA_ICO})
  add_dependencies(${target} yantra_ico)
  if(ARG_GUI)
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE ON)
  endif()
endfunction()
