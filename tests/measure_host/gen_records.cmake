# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Record files of the radios' scenarios for the TST-10 tests, written in
# the build tree by the submodule's own tools (no IQ code in Yantra):
#   <id>.records.jsonl/.cells.csv/.truth.csv   yantra-gen ground truth
#       (dBm, SELF_CALIBRATED) with what each cell broadcasts and the true
#       level per 0.1 s, for every "<id>:<seconds>" of SCENARIOS
#   <DBFS>_dbfs.records.jsonl   yantra-gen I/Q of scenario DBFS (full scale
#       -20 dBm: the true calibration offset is -20 dB) decoded by
#       yantra-radio-host decode-iq --uncalibrated (dBFS, no C1, no
#       device); the I/Q files are removed again
# Inputs: GEN, HOST (tool paths), OUT (folder), SCENARIOS, DBFS.
file(MAKE_DIRECTORY ${OUT})
foreach(_s IN LISTS SCENARIOS)
  string(REPLACE ":" ";" _p "${_s}")
  list(GET _p 0 _id)
  list(GET _p 1 _seconds)
  execute_process(COMMAND ${GEN} --scenario ${_id} --duration ${_seconds} --out -
                          --records ${OUT}/${_id}.records.jsonl --cells ${OUT}/${_id}.cells.csv
                          --truth ${OUT}/${_id}.truth.csv --quiet
                  OUTPUT_FILE /dev/null RESULT_VARIABLE _rc ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "yantra-gen ${_id}: exit ${_rc}: ${_err}")
  endif()
endforeach()
if(DBFS)
  set(_iq ${OUT}/${DBFS}_iq.sigmf-data)
  execute_process(COMMAND ${GEN} --scenario ${DBFS} --duration 4 --format cs16 --full-scale-dbm -20
                          --out ${_iq} --quiet
                  RESULT_VARIABLE _rc ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "yantra-gen ${DBFS} I/Q: exit ${_rc}: ${_err}")
  endif()
  execute_process(COMMAND ${HOST} decode-iq --iq ${_iq} --format sigmf --serving --uncalibrated
                          --out ${OUT}/${DBFS}_dbfs.records.jsonl
                  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  file(GLOB _iq_files ${OUT}/${DBFS}_iq.*)
  file(REMOVE ${_iq_files})
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "decode-iq ${DBFS}: exit ${_rc}: ${_out}${_err}")
  endif()
endif()
