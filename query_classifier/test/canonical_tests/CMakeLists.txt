if(${ERRMSG} MATCHES "ERRMSG-NOTFOUND")
  message(FATAL_ERROR "The errmsg.sys file was not found, please define the path with -DERRMSG=<path>")
else()
  if(${CMAKE_VERSION} VERSION_LESS 2.8)
	execute_process(COMMAND cp ${ERRMSG} ${CMAKE_CURRENT_BINARY_DIR})
  else()
	file(COPY ${ERRMSG} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  endif()
endif()
add_executable(canonizer canonizer.c)
target_link_libraries(canonizer maxscale-common)
add_test(NAME Internal-CanonicalQuery COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/input.sql
  ${CMAKE_CURRENT_BINARY_DIR}/output.sql
  ${CMAKE_CURRENT_SOURCE_DIR}/expected.sql
  $<TARGET_FILE:canonizer>)

add_test(NAME Internal-CanonicalQuerySelect COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/select.sql
  ${CMAKE_CURRENT_BINARY_DIR}/select.output
  ${CMAKE_CURRENT_SOURCE_DIR}/select.expected
  $<TARGET_FILE:canonizer>)

add_test(NAME Internal-CanonicalQueryAlter COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/alter.sql
  ${CMAKE_CURRENT_BINARY_DIR}/alter.output
  ${CMAKE_CURRENT_SOURCE_DIR}/alter.expected
  $<TARGET_FILE:canonizer>)

add_test(NAME Internal-CanonicalQueryComment COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/comment.sql
  ${CMAKE_CURRENT_BINARY_DIR}/comment.output
  ${CMAKE_CURRENT_SOURCE_DIR}/comment.expected
  $<TARGET_FILE:canonizer>)
