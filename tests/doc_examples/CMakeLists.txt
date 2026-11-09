# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# DOC-21: the code examples of the documentation are run. A Markdown code
# block right after a line `<!-- example: <kind> -->` is a checked example:
# - doc_examples (test_doc_examples.cpp) reads every marked block of the
#   repository's Markdown files with the reader of its format (the
#   file-format pages, DOC-12) or as JSON (API replies, DOC-13); an
#   unknown kind fails, so no mark goes unchecked;
# - doc_api_examples runs the `api-shell` blocks of docs/API.md against
#   yantra-probe --demo with the probe plugin on loopback (a free port).
add_executable(test_doc_examples test_doc_examples.cpp)
set(_libs yantra::core yantra::json)
if(TARGET yantra_app_model)
  list(APPEND _libs yantra::app_model)
  target_compile_definitions(test_doc_examples PRIVATE YANTRA_DOC_EXAMPLES_APP=1)
endif()
target_link_libraries(test_doc_examples PRIVATE yantra_test_main yantra_warnings ${_libs})
target_compile_definitions(test_doc_examples PRIVATE YANTRA_DOC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME doc_examples COMMAND test_doc_examples)
set_tests_properties(doc_examples PROPERTIES LABELS "fast;docs" TIMEOUT 300
                     WORKING_DIRECTORY ${PROJECT_BINARY_DIR})

if(TARGET yantra_probe)
  add_test(NAME doc_api_examples
           COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/doc_api_examples.py
                   $<TARGET_FILE:yantra-probe> ${PROJECT_BINARY_DIR}/lib/yantra/plugins
                   ${CMAKE_CURRENT_BINARY_DIR}/api ${PROJECT_SOURCE_DIR}/docs/API.md)
  set_tests_properties(doc_api_examples PROPERTIES LABELS "fast;docs" TIMEOUT 300 PROCESSORS 2
                       SKIP_RETURN_CODE 77 ENVIRONMENT "YANTRA_TEST_TIME_SCALE=${YANTRA_TEST_TIME_SCALE}")
endif()
