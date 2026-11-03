# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Repository lint under ctest (RAD-134 lint job, Yantra TST-02, NFR-01):
# shell, Python and YAML syntax (tools/ci/lint.sh) and REUSE compliance.
add_test(NAME repo_lint COMMAND sh ${PROJECT_SOURCE_DIR}/tools/ci/lint.sh ${PROJECT_SOURCE_DIR})
set_tests_properties(repo_lint PROPERTIES LABELS "fast;lint" TIMEOUT 300)

find_program(YANTRA_REUSE_EXECUTABLE reuse)
if(YANTRA_REUSE_EXECUTABLE)
  add_test(NAME reuse_lint COMMAND ${YANTRA_REUSE_EXECUTABLE} --root ${PROJECT_SOURCE_DIR} lint -q)
else()
  # Skipped, not passed: ctest reports it (exit code 77).
  add_test(NAME reuse_lint COMMAND sh -c "echo 'reuse not found (pip install reuse)'; exit 77")
endif()
set_tests_properties(reuse_lint PROPERTIES LABELS "fast;lint" TIMEOUT 300 SKIP_RETURN_CODE 77)
