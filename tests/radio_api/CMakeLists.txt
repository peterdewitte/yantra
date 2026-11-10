# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Radio API tests (RAD-10, RAD-11): C11 and C++20 translation units with
# the ABI layout asserts and the helper tests, always with -Werror; and a
# syntax check of the public headers in the oldest language modes a
# plugin may use (C99 for radio.h, C++20 for radio.hpp) with -pedantic.
yantra_add_test_dir(radio_api LIBS yantra::radio_api)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_radio_api PRIVATE -Wall -Wextra -Werror)
  set(_inc ${CMAKE_SOURCE_DIR}/include)
  set(_dir ${CMAKE_CURRENT_SOURCE_DIR}/syntax)
  add_test(NAME radio_api_header_c99
    COMMAND ${YANTRA_BUILD_HOST_TEST} ${CMAKE_C_COMPILER} -std=c99 -pedantic -Wall -Wextra -Werror -Wstrict-prototypes
            -Wmissing-prototypes -Wundef -fsyntax-only -I${_inc} -x c ${_dir}/radio_h.c)
  add_test(NAME radio_api_header_cxx11
    COMMAND ${YANTRA_BUILD_HOST_TEST} ${CMAKE_CXX_COMPILER} -std=c++11 -pedantic -Wall -Wextra -Werror -Wundef
            -fsyntax-only -I${_inc} -x c++ ${_dir}/radio_h.c)
  add_test(NAME radio_api_header_hpp
    COMMAND ${YANTRA_BUILD_HOST_TEST} ${CMAKE_CXX_COMPILER} -std=c++20 -pedantic -Wall -Wextra -Werror -Wundef -Wshadow
            -Wconversion -fsyntax-only -I${_inc} -x c++ ${_dir}/radio_hpp.cpp)
  set_tests_properties(radio_api_header_c99 radio_api_header_cxx11 radio_api_header_hpp
    PROPERTIES LABELS fast)
  # The compiler of the build machine (syntax only): a build-host check.
  yantra_build_host_tests(radio_api_header_c99 radio_api_header_cxx11 radio_api_header_hpp)
endif()
