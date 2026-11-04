# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The generic SoapySDR plugin against the fake module (RAD-62, RAD-92):
# per-case doctests, the conformance kit with unplug/replug, and the
# completeness check of the test build.
if(NOT TARGET soapysdr_fake)
  message(STATUS "test_soapysdr: left out (no soapysdr plugin)")
  return()
endif()
add_executable(test_soapysdr test_soapysdr.cpp)
target_link_libraries(test_soapysdr PRIVATE yantra_test_main yantra_warnings
  yantra::radio_host yantra::gen)
target_compile_definitions(test_soapysdr PRIVATE SOAPYSDR_FAKE_PLUGIN="$<TARGET_FILE:soapysdr_fake>")
add_dependencies(test_soapysdr soapysdr_fake)
yantra_doctest_tests(test_soapysdr GROUP test_soapysdr LABELS fast TIMEOUT 300
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_allow "")
if(NOT YANTRA_SANITIZER STREQUAL "")
  set(_allow --allow "lib(asan|ubsan|tsan|lsan)\\.so\\.[0-9]+" --allow "libstdc\\+\\+\\.so\\.6"
             --allow "libgcc_s\\.so\\.1")
endif()
add_test(NAME completeness_soapysdr_fake
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/ci/check_completeness.py
                 ${_allow} $<TARGET_FILE:soapysdr_fake>)
set_tests_properties(completeness_soapysdr_fake PROPERTIES LABELS fast)

# Conformance with a fake HackRF-like device, unplugged and replugged by
# a control file (RAD-35).
set(_ctl ${CMAKE_BINARY_DIR}/tests/soapysdr_control/conformance)
set(_serial 0000000000000000a1b2c3d4e5f60001)
set(_fake "{\"control\":\"${_ctl}\",\"devices\":[{\"serial\":\"${_serial}\",\"scenario\":\"${PROJECT_SOURCE_DIR}/tests/fixtures/soapy/fake_two_cells.json\"}]}")
add_test(NAME conformance_soapysdr_fake
         COMMAND sh -c "mkdir -p \"$1\" && rm -f \"$1/$2.unplugged\" && shift 2 && exec \"$@\""
                 conformance ${_ctl} ${_serial}
                 $<TARGET_FILE:yantra-radio-host> conformance --plugin $<TARGET_FILE:soapysdr_fake>
                 --report ${CMAKE_BINARY_DIR}/conformance/soapysdr_fake.json
                 --unplug "touch ${_ctl}/${_serial}.unplugged"
                 --replug "rm -f ${_ctl}/${_serial}.unplugged")
set_tests_properties(conformance_soapysdr_fake PROPERTIES LABELS "fast;timing" TIMEOUT 300
  ENVIRONMENT "YANTRA_FAKE_SOAPY=${_fake}")
