# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Repository lint under ctest (RAD-134 lint job, Yantra TST-02, NFR-01):
# shell, Python and YAML syntax, the Rust crate's cargo-deny check and the
# FFI width/layout check of RAD-90 (tools/ci/lint.sh), and REUSE
# compliance. The lint shares the cargo build folder of rust_cargo_test
# (cargo locks it, so both may run at the same time).
add_test(NAME repo_lint COMMAND ${YANTRA_BUILD_HOST_TEST} sh ${PROJECT_SOURCE_DIR}/tools/ci/lint.sh ${PROJECT_SOURCE_DIR})
set(_lint_env "YANTRA_LINT_CARGO_TARGET_DIR=${PROJECT_BINARY_DIR}/cargo" "CC=${CMAKE_C_COMPILER}" ${YANTRA_CARGO_ENV})
if(YANTRA_CARGO_EXECUTABLE)
  list(APPEND _lint_env "YANTRA_LINT_CARGO=${YANTRA_CARGO_EXECUTABLE}")
endif()
set_tests_properties(repo_lint PROPERTIES LABELS "fast;lint" TIMEOUT 300 ENVIRONMENT "${_lint_env}")

find_program(YANTRA_REUSE_EXECUTABLE reuse)
if(YANTRA_REUSE_EXECUTABLE)
  add_test(NAME reuse_lint COMMAND ${YANTRA_BUILD_HOST_TEST} ${YANTRA_REUSE_EXECUTABLE} --root ${PROJECT_SOURCE_DIR} lint -q)
else()
  # Skipped, not passed: ctest reports it (exit code 77).
  add_test(NAME reuse_lint COMMAND ${YANTRA_BUILD_HOST_TEST} sh -c "echo 'reuse not found (pip install reuse)'; exit 77")
endif()
set_tests_properties(reuse_lint PROPERTIES LABELS "fast;lint" TIMEOUT 300 SKIP_RETURN_CODE 77)
yantra_build_host_tests(repo_lint reuse_lint)
