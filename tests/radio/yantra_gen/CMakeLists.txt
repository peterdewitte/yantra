# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests of yantra-gen (RAD-71, RAD-93): command line, exit codes, every
# sample format, SigMF metadata, ground-truth CSV, cell CSV and record file
# (RAD-65) against the scenario and the engine, seeded golden digests
# (golden/digests.txt; rewrite on purpose only: test_yantra_gen --update or
# the build target update-golden-yantra-gen), and the command itself:
# streaming to stdout and an install-tree run. The osmo-tetra round trip of
# yantra-gen's IQ files is in tests/gen_osmo.
# Windows has golden files of its own (golden/windows): the C runtime's
# math functions (MinGW-w64 libm: log10, pow, exp, sin) round some results
# differently in the last bit from glibc's, which changes digits of the
# truth levels and, rarely, a quantised sample. The UCRT's CPU-dependent
# FMA3 math paths are off in every test main and in yantra-gen
# (gen/include/yantra/gen/portable_math.hpp), so the files hold for every
# Windows machine. Same seeds, same scenarios; Windows files are written
# from a run in the Windows test VM (the failing check prints the computed
# text in LastTest.log; never from Linux).
set(_golden ${CMAKE_CURRENT_SOURCE_DIR}/golden)
if(WIN32)
  string(APPEND _golden /windows)
endif()
yantra_radio_add_test_dir(yantra_gen LIBS yantra_gen_app yantra::gen yantra::radios_common yantra::json)
target_compile_definitions(test_yantra_gen PRIVATE
  YANTRA_GEN_CLI_GOLDEN_DIR="${_golden}"
  YANTRA_SCENARIO_DIR="${PROJECT_SOURCE_DIR}/data/scenarios")
add_custom_target(update-golden-yantra-gen
  COMMAND $<TARGET_FILE:test_yantra_gen> --update --test-suite=gen-cli-golden
  DEPENDS test_yantra_gen
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
  COMMENT "Rewriting the yantra-gen golden digests (tests/yantra_gen/golden, golden/windows in the Windows tree)"
  VERBATIM)

# The command itself (a process, not the library).
add_test(NAME yantra_gen_help COMMAND yantra-gen --help)
set_tests_properties(yantra_gen_help PROPERTIES LABELS fast
  PASS_REGULAR_EXPRESSION "usage: yantra-gen")
add_test(NAME yantra_gen_version COMMAND yantra-gen --version)
set_tests_properties(yantra_gen_version PROPERTIES LABELS fast
  PASS_REGULAR_EXPRESSION "^yantra-gen ${YANTRA_VERSION}\n$")
add_test(NAME yantra_gen_usage_error COMMAND yantra-gen --format cs4 --out x)
set_tests_properties(yantra_gen_usage_error PROPERTIES LABELS fast WILL_FAIL TRUE)
# The build tree finds its scenarios next to the binary (share/...).
add_test(NAME yantra_gen_list COMMAND yantra-gen --list)
set_tests_properties(yantra_gen_list PROPERTIES LABELS fast
  PASS_REGULAR_EXPRESSION "indoor_good_das\tIndoor, good DAS coverage\t\\(default\\)")
# Streaming to stdout: exact byte count of a finite run, and an endless
# run cut by the reader (tests/test_command.py, Linux and Windows; the
# writer's message about the closed pipe is dropped: on Linux SIGPIPE ends
# it silently, on Windows it reports the failed write and exits).
add_test(NAME yantra_gen_stdout
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/radio/test_command.py --count-stdout --
          $<TARGET_FILE:yantra-gen> --cell carrier=3600 --duration 0.1 --format cs8 --full-scale-dbm -50 --quiet --out -)
set_tests_properties(yantra_gen_stdout PROPERTIES LABELS fast PASS_REGULAR_EXPRESSION "^ *100000\n$")
add_test(NAME yantra_gen_stdout_endless
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/radio/test_command.py --count-stdout --limit 3000000 --quiet-stderr --
          $<TARGET_FILE:yantra-gen> --cell carrier=3600 --duration 0 --format cu8 --full-scale-dbm -50 --quiet --out -)
set_tests_properties(yantra_gen_stdout_endless PROPERTIES LABELS fast TIMEOUT 60
  PASS_REGULAR_EXPRESSION "^ *3000000\n$")
# Install rules: yantra-gen and the scenarios in a scratch prefix; the
# installed binary lists and runs a scenario by id.
add_test(NAME yantra_gen_install
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/install_check.py
          ${CMAKE_COMMAND} ${PROJECT_BINARY_DIR}/yantra_gen_install_test ${PROJECT_BINARY_DIR}
          $<TARGET_FILE_NAME:yantra-gen>)
set_tests_properties(yantra_gen_install PROPERTIES LABELS fast TIMEOUT 120)
