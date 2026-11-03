# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/: yantra-gen (RAD-71), yantra-radio-host (RAD-91) and yantra-hackrf
# (RAD-66), one folder each; tools/deps/ holds the static dependency
# builds (RAD-136) and tools/ci/ the CI scripts (RAD-134), neither is a
# CMake target.
file(GLOB _children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(_child ${_children})
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_child}/CMakeLists.txt)
    add_subdirectory(${_child})
  endif()
endforeach()
