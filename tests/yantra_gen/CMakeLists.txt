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
yantra_add_test_dir(yantra_gen LIBS yantra_gen_app yantra::gen yantra::radios_common yantra::json)
target_compile_definitions(test_yantra_gen PRIVATE
  YANTRA_GEN_CLI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
  YANTRA_SCENARIO_DIR="${CMAKE_SOURCE_DIR}/data/scenarios")
add_custom_target(update-golden-yantra-gen
  COMMAND $<TARGET_FILE:test_yantra_gen> --update --test-suite=gen-cli-golden
  DEPENDS test_yantra_gen
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Rewriting the yantra-gen golden digests (tests/yantra_gen/golden)"
  VERBATIM)

# The command itself (a process, not the library).
add_test(NAME yantra_gen_help COMMAND yantra-gen --help)
set_tests_properties(yantra_gen_help PROPERTIES LABELS fast
  PASS_REGULAR_EXPRESSION "usage: yantra-gen")
add_test(NAME yantra_gen_version COMMAND yantra-gen --version)
set_tests_properties(yantra_gen_version PROPERTIES LABELS fast
  PASS_REGULAR_EXPRESSION "^yantra-gen ${PROJECT_VERSION}\n$")
add_test(NAME yantra_gen_usage_error COMMAND yantra-gen --format cs4 --out x)
set_tests_properties(yantra_gen_usage_error PROPERTIES LABELS fast WILL_FAIL TRUE)
# The build tree finds its scenarios next to the binary (share/...).
add_test(NAME yantra_gen_list COMMAND yantra-gen --list)
set_tests_properties(yantra_gen_list PROPERTIES LABELS fast
  PASS_REGULAR_EXPRESSION "indoor_good_das\tIndoor, good DAS coverage\t\\(default\\)")
# Streaming to stdout: exact byte count of a finite run, and an endless
# run cut by the reader.
add_test(NAME yantra_gen_stdout
  COMMAND sh -c "\"$0\" --cell carrier=3600 --duration 0.1 --format cs8 --full-scale-dbm -50 --quiet --out - | wc -c"
          $<TARGET_FILE:yantra-gen>)
set_tests_properties(yantra_gen_stdout PROPERTIES LABELS fast PASS_REGULAR_EXPRESSION "^ *100000\n$")
add_test(NAME yantra_gen_stdout_endless
  COMMAND sh -c "\"$0\" --cell carrier=3600 --duration 0 --format cu8 --full-scale-dbm -50 --quiet --out - | head -c 3000000 | wc -c"
          $<TARGET_FILE:yantra-gen>)
set_tests_properties(yantra_gen_stdout_endless PROPERTIES LABELS fast TIMEOUT 60
  PASS_REGULAR_EXPRESSION "^ *3000000\n$")
# Install rules: yantra-gen and the scenarios in a scratch prefix; the
# installed binary lists and runs a scenario by id.
add_test(NAME yantra_gen_install
  COMMAND sh -c "rm -rf \"$1\" && \"$0\" --install \"$2\" --prefix \"$1\" >/dev/null && \"$1/bin/yantra-gen\" --list | grep -q '^fraunces' && \"$1/bin/yantra-gen\" --scenario fraunces --duration 0.05 --quiet --out \"$1/run.cs8\" --format cs8 && test -s \"$1/run.records.jsonl\" && test -f \"$1/share/yantra-radios/scenarios/scenes/demo_office_f0.json\"; rc=$?; rm -rf \"$1\"; exit $rc"
          ${CMAKE_COMMAND} ${CMAKE_BINARY_DIR}/yantra_gen_install_test ${CMAKE_BINARY_DIR})
set_tests_properties(yantra_gen_install PROPERTIES LABELS fast TIMEOUT 120)
