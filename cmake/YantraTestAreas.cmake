# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Test areas (PLG-29): every ctest carries the label of the part of the
# tree it tests, so `ctest -L <area>` runs one part: app, core,
# radio-api, plugin:<name>, radio-common, gen, docs, lint, packaging.
#
# yantra_add_area_subdirectory(<dir> <area>) adds a folder whose tests
# belong to <area>: the per-case doctest groups made in it get the label
# through YANTRA_TEST_AREA (cmake/YantraTests.cmake), and every other test
# of the folder and its subfolders gets it afterwards
# (set_property(TEST ... DIRECTORY), CMake 3.28; with an older CMake the
# folder's LABELS directory property is set instead). A test that names
# its own area keeps it.
# A macro, so that the folder's PARENT_SCOPE variables reach the caller.
macro(yantra_add_area_subdirectory _yaa_dir _yaa_area)
  set(_yaa_saved "${YANTRA_TEST_AREA}")
  set(YANTRA_TEST_AREA ${_yaa_area})
  add_subdirectory(${_yaa_dir})
  yantra_label_area_tests(${CMAKE_CURRENT_SOURCE_DIR}/${_yaa_dir} ${_yaa_area})
  set(YANTRA_TEST_AREA "${_yaa_saved}")
endmacro()

set(YANTRA_TEST_AREA_NAMES app core radio-api radio-common gen docs lint packaging)

function(_yantra_has_area labels var)
  set(_has FALSE)
  foreach(_l IN LISTS labels)
    if(_l IN_LIST YANTRA_TEST_AREA_NAMES OR _l MATCHES "^plugin:")
      set(_has TRUE)
    endif()
  endforeach()
  set(${var} ${_has} PARENT_SCOPE)
endfunction()

function(yantra_label_area_tests dir area)
  if(CMAKE_VERSION VERSION_LESS 3.28)
    set_property(DIRECTORY ${dir} PROPERTY LABELS ${area})
    return()
  endif()
  get_property(_tests DIRECTORY ${dir} PROPERTY TESTS)
  foreach(_t IN LISTS _tests)
    get_property(_labels TEST ${_t} DIRECTORY ${dir} PROPERTY LABELS)
    _yantra_has_area("${_labels}" _has)
    if(NOT _has)
      set_property(TEST ${_t} DIRECTORY ${dir} APPEND PROPERTY LABELS ${area})
    endif()
  endforeach()
  get_property(_subs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
  foreach(_s IN LISTS _subs)
    yantra_label_area_tests(${_s} ${area})
  endforeach()
endfunction()
