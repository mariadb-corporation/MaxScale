# This test no longer requires the query classifier for canonicalization
add_executable(canonizer canonizer.c)
target_link_libraries(canonizer maxscale-common)
add_test(NAME CanonicalQuery COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/input.sql
  ${CMAKE_CURRENT_BINARY_DIR}/output.sql
  ${CMAKE_CURRENT_SOURCE_DIR}/expected.sql
  $<TARGET_FILE:canonizer>)

add_test(NAME CanonicalQuerySelect COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/select.sql
  ${CMAKE_CURRENT_BINARY_DIR}/select.output
  ${CMAKE_CURRENT_SOURCE_DIR}/select.expected
  $<TARGET_FILE:canonizer>)

add_test(NAME CanonicalQueryAlter COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/alter.sql
  ${CMAKE_CURRENT_BINARY_DIR}/alter.output
  ${CMAKE_CURRENT_SOURCE_DIR}/alter.expected
  $<TARGET_FILE:canonizer>)

add_test(NAME CanonicalQueryComment COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/comment.sql
  ${CMAKE_CURRENT_BINARY_DIR}/comment.output
  ${CMAKE_CURRENT_SOURCE_DIR}/comment.expected
  $<TARGET_FILE:canonizer>)

add_test(NAME CanonicalQueryWhitespace COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/whitespace.sql
  ${CMAKE_CURRENT_BINARY_DIR}/whitespace.output
  ${CMAKE_CURRENT_SOURCE_DIR}/whitespace.expected
  $<TARGET_FILE:canonizer>)
