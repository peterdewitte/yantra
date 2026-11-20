# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Repository lint under ctest (TST-02 lint job, RAD-134, NFR-01): shell,
# Python and YAML syntax, the Rust crate's cargo-deny check and the FFI
# width/layout check of RAD-90 (tools/ci/lint.sh), and REUSE compliance,
# for the whole repository (the radio-parts-only build runs the same two
# from tests/radio/lint). The lint shares the cargo build folder of
# rust_cargo_test (cargo locks it, so both may run at the same time).
# Copied from yantra-radios fb194f5: tests/lint/CMakeLists.txt, adapted.
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

# The UI lint's source half (DOC-20, UI-12): every UI text has its NL, FR
# and DE translation with the same placeholders, no stale entries, and the
# manual sections it refers to exist (tools/ci/lint_ui.py; the run-time
# half is test_app_ui's "DOC-20 UI lint" cases).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ui_lint COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tools/ci/lint_ui.py ${PROJECT_SOURCE_DIR})
set_tests_properties(ui_lint PROPERTIES LABELS "fast;lint" TIMEOUT 120)

# Checks of the sources and the build machine's tools: build_host in a
# cross-compiled tree (top-level CMakeLists.txt).
foreach(_t repo_lint reuse_lint ui_lint)
  if(TEST ${_t})
    yantra_build_host_tests(${_t})
  endif()
endforeach()
