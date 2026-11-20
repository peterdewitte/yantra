# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Writes the git tree id of the radio parts (radios/ at HEAD, PLG-29) to
# OUT, only when it changed, so build steps that depend on OUT run again
# only after a change in radios/. Without git (a source package): FALLBACK.
# Usage: cmake -DDIR=<radios> -DOUT=<file> -DFALLBACK=<id> -P radios_stamp.cmake
execute_process(COMMAND git -C ${DIR} rev-parse HEAD:./
                OUTPUT_VARIABLE _id OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _rc ERROR_QUIET)
if(NOT _rc EQUAL 0 OR _id STREQUAL "")
  set(_id ${FALLBACK})
endif()
set(_old "")
if(EXISTS ${OUT})
  file(READ ${OUT} _old)
endif()
if(NOT _old STREQUAL "${_id}\n")
  file(WRITE ${OUT} "${_id}\n")
endif()
