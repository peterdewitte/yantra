# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Report engine (EXP-05, BRD-02, BRD-06, DEMO-07, DEMO-12;
# docs/REPORT.md): the PDF (libharu, embedded Inter and JetBrains Mono
# subsets) and HTML back ends, branding on the cover and every page,
# reproducible output, and plan credits with the rule that a
# licence-restricted plan is drawn only from the user's cached unaltered
# copy (tested with a Fraunces sheet standing in for a restricted one).
# One ctest per doctest case.
list(APPEND YANTRA_PER_CASE_TESTS report)
yantra_add_test_dir(report LIBS yantra::core yantra::json)
target_compile_definitions(test_report PRIVATE
  YANTRA_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")

# DEMO-06: the sample report of the Fraunces demo is made again from the
# built demo project and compared with the committed one in docs/samples
# within report::CompareTolerance (tools/yantra-demo --sample-report
# --compare; CI runs the same). An intended change: `ninja sample_report`
# and commit docs/samples.
if(TARGET demo_fraunces)
  add_test(NAME sample_report_compare
    COMMAND ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${YANTRA_SAMPLE_REPORT_EPOCH}
            $<TARGET_FILE:yantra-demo> --sample-report ${CMAKE_CURRENT_BINARY_DIR}/scratch/sample_report
            --work ${CMAKE_CURRENT_BINARY_DIR}/scratch/sample_report_work ${YANTRA_SAMPLE_REPORT_ARGS}
            --compare ${PROJECT_SOURCE_DIR}/docs/samples)
  set_tests_properties(sample_report_compare PROPERTIES LABELS "slow;demo" TIMEOUT 900)
endif()
