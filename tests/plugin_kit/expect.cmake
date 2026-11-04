# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs yantra-plugin-test and checks its exit code and output (the
# negative cases of PLG-13: a broken plugin must be refused with a clear
# message). ctest's PASS_REGULAR_EXPRESSION ignores the exit code, hence
# this script.
# Inputs: KIT, ARGS ("|"-separated), EXIT (expected exit code), MATCH
# (regular expression the output must contain, optional), REPORT (JSON
# report file the kit must write, optional), REQUIRE_FILE (skip, printing
# "SKIPPED: ", when this file is missing: prebuilt radio plugins).
if(REQUIRE_FILE AND NOT EXISTS "${REQUIRE_FILE}")
  message("SKIPPED: ${REQUIRE_FILE} does not exist")
  return()
endif()
string(REPLACE "|" ";" _args "${ARGS}")
if(REPORT AND NOT EXIT STREQUAL "2")
  file(REMOVE ${REPORT})
  list(APPEND _args --json ${REPORT})
endif()
execute_process(COMMAND ${KIT} ${_args} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out
                ERROR_VARIABLE _out)
message("${_out}")
if(NOT "${_rc}" STREQUAL "${EXIT}")
  message(FATAL_ERROR "exit code ${_rc}, expected ${EXIT}")
endif()
if(MATCH AND NOT _out MATCHES "${MATCH}")
  message(FATAL_ERROR "the output does not match '${MATCH}'")
endif()
if(REPORT AND NOT EXIT STREQUAL "2")
  if(NOT EXISTS ${REPORT})
    message(FATAL_ERROR "no report ${REPORT}")
  endif()
  file(READ ${REPORT} _json)
  string(JSON _result GET "${_json}" result)
  string(JSON _format GET "${_json}" format)
  if(NOT _format STREQUAL "yantra-plugin-test-report")
    message(FATAL_ERROR "the report has format '${_format}'")
  endif()
  if(EXIT STREQUAL "0" AND NOT _result STREQUAL "pass")
    message(FATAL_ERROR "exit 0 but the report says '${_result}'")
  endif()
  if(EXIT STREQUAL "1" AND NOT _result STREQUAL "fail")
    message(FATAL_ERROR "exit 1 but the report says '${_result}'")
  endif()
endif()
