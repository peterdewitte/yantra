# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Walk tests, measured points and coverage statistics (IND-20, IND-30,
# GEO-02, GEO-05, EXP-05, EXP-06, PP-02, TST-32, TST-34; docs/SURVEY.md).
# One ctest per doctest case (every case has its own scratch folder).

list(APPEND YANTRA_PER_CASE_TESTS survey)
yantra_add_test_dir(survey LIBS yantra::core yantra::json)
target_compile_definitions(test_survey PRIVATE
  YANTRA_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
