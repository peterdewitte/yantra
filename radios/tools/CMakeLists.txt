# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/: yantra-gen (RAD-71), yantra-radio-host (RAD-91) and yantra-hackrf
# (RAD-66), one folder each; tools/deps/ holds the static dependency
# builds (RAD-136; its own CMake project, included by the top level as
# tools/deps/deps.cmake) and tools/ci/ the CI scripts (RAD-134).
file(GLOB _children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
# yantra-hackrf uses the test host's libraries: the host first.
list(REMOVE_ITEM _children yantra-radio-host)
list(PREPEND _children yantra-radio-host)
foreach(_child ${_children})
  # deps/ is its own project (build_deps.sh); the main build includes
  # tools/deps/deps.cmake instead.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_child}/CMakeLists.txt AND NOT _child STREQUAL "deps")
    add_subdirectory(${_child})
  endif()
endforeach()
