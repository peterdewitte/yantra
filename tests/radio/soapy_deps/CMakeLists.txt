# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The static SoapySDR build (RAD-132, RAD-136): built-in modules register
# their drivers, nothing is loaded from the system.
add_executable(test_soapy_deps test_soapy_deps.cpp)
target_link_libraries(test_soapy_deps PRIVATE yantra_radio_test_main yantra_warnings yantra::soapy_modules)
add_test(NAME test_soapy_deps COMMAND test_soapy_deps)
set_tests_properties(test_soapy_deps PROPERTIES LABELS fast TIMEOUT 120
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
