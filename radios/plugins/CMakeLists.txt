# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# plugins/: one folder per radio plugin (RAD-02): hackrf, simulator, soapy,
# pei, example. Every folder with a CMakeLists.txt is added; plugins land
# in ${YANTRA_RADIO_PLUGIN_DIR}. tetra_common (the shared TETRA plugin glue,
# a static library) comes first: the TETRA plugins link it.
add_subdirectory(tetra_common)
add_subdirectory(soapy_common)
file(GLOB _children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(_child ${_children})
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_child}/CMakeLists.txt AND NOT _child STREQUAL "tetra_common"
     AND NOT _child STREQUAL "soapy_common")
    add_subdirectory(${_child})
  endif()
endforeach()
