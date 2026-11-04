# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Command-line tools (OA-01), one sub-folder each with its own
# CMakeLists.txt, picked up automatically. tools/ci/ holds CI scripts only.

file(GLOB _yantra_tool_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(_dir IN LISTS _yantra_tool_dirs)
  if(IS_DIRECTORY ${_dir} AND EXISTS ${_dir}/CMakeLists.txt)
    add_subdirectory(${_dir})
  endif()
endforeach()
