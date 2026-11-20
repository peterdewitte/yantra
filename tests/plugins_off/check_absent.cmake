# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# cmake -DABSENT=<file>|<file> -DFOLDERS=<plugin folder> -P check_absent.cmake
# Fails when a left-out plugin is in the build's plugin folders; lists the
# plugins that are there.
string(REPLACE "|" ";" ABSENT "${ABSENT}")
set(_bad 0)
foreach(_f IN LISTS ABSENT)
  if(EXISTS ${_f})
    message(SEND_ERROR "left out, but present: ${_f}")
    set(_bad 1)
  endif()
endforeach()
file(GLOB_RECURSE _present RELATIVE ${FOLDERS} ${FOLDERS}/*.so)
list(SORT _present)
message(STATUS "plugins in ${FOLDERS}: ${_present}")
if(_bad)
  message(FATAL_ERROR "the PLG-01/PLG-30 check build holds a left-out part")
endif()
