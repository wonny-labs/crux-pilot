# SPDX-License-Identifier: Apache-2.0
set(_copy "${CMAKE_BINARY_DIR}/settings-file-candidate")
# Re-stage whenever an input changes, also in an incremental build.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/prepare.py ${CMAKE_CURRENT_LIST_DIR}/settings_file.patch
  ${CMAKE_CURRENT_LIST_DIR}/settings_line.patch
  ${CMAKE_CURRENT_LIST_DIR}/../settings_error/settings_store.patch
  ${ZEPHYR_BASE}/subsys/settings/src/settings_file.c
  ${ZEPHYR_BASE}/subsys/settings/src/settings_line.c
  ${ZEPHYR_BASE}/subsys/settings/src/settings_store.c)
execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/prepare.py
  --zephyr ${ZEPHYR_BASE} --output ${_copy} RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Pinned Settings File copy preparation failed")
endif()
get_target_property(_sources zephyr SOURCES)
foreach(_name IN ITEMS settings_file.c settings_line.c settings_store.c)
  set(_original "${ZEPHYR_BASE}/subsys/settings/src/${_name}")
  set(_count 0)
  foreach(_source IN LISTS _sources)
    if(_source STREQUAL _original)
      math(EXPR _count "${_count} + 1")
    endif()
  endforeach()
  if(NOT _count EQUAL 1)
    message(FATAL_ERROR "Expected exactly one original source: ${_original}")
  endif()
  list(REMOVE_ITEM _sources "${_original}")
  list(APPEND _sources "${_copy}/${_name}")
endforeach()
set_property(TARGET zephyr PROPERTY SOURCES "${_sources}")
target_include_directories(zephyr PRIVATE ${ZEPHYR_BASE}/subsys/settings/src)
