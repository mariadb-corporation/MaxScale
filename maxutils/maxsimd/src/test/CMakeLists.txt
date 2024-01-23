# This test no longer requires the query classifier for canonicalization
add_executable(canonizer canonizer.cc)
target_link_libraries(canonizer maxsimd)
add_test(NAME test_canonical COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/input.sql
  ${CMAKE_CURRENT_BINARY_DIR}/output.sql
  ${CMAKE_CURRENT_SOURCE_DIR}/expected.sql
  $<TARGET_FILE:canonizer>)

add_test(NAME test_canonical_select COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test-select.log
  ${CMAKE_CURRENT_SOURCE_DIR}/select.sql
  ${CMAKE_CURRENT_BINARY_DIR}/select.output
  ${CMAKE_CURRENT_SOURCE_DIR}/select.expected
  $<TARGET_FILE:canonizer>)

add_test(NAME test_canonical_alter COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test-alter.log
  ${CMAKE_CURRENT_SOURCE_DIR}/alter.sql
  ${CMAKE_CURRENT_BINARY_DIR}/alter.output
  ${CMAKE_CURRENT_SOURCE_DIR}/alter.expected
  $<TARGET_FILE:canonizer>)

add_test(NAME test_canonical_comment COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test-comment.log
  ${CMAKE_CURRENT_SOURCE_DIR}/comment.sql
  ${CMAKE_CURRENT_BINARY_DIR}/comment.output
  ${CMAKE_CURRENT_SOURCE_DIR}/comment.expected
  $<TARGET_FILE:canonizer>)

add_test(NAME test_canonical_whitespace COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh
  ${CMAKE_CURRENT_BINARY_DIR}/test-whitespace.log
  ${CMAKE_CURRENT_SOURCE_DIR}/whitespace.sql
  ${CMAKE_CURRENT_BINARY_DIR}/whitespace.output
  ${CMAKE_CURRENT_SOURCE_DIR}/whitespace.expected
  $<TARGET_FILE:canonizer>)

add_executable(compare_canonicals compare_canonicals.cc)
target_link_libraries(compare_canonicals maxsimd)

# Use the tests included in the Postgres parser for validating that the arguments are recreated correctly
file(GLOB PG_SQL_TESTS "${CMAKE_SOURCE_DIR}/server/modules/parser_plugin/test/postgres/*.sql")

add_test(NAME test_compare_canonicals COMMAND compare_canonicals
  ${CMAKE_CURRENT_SOURCE_DIR}/input.sql
  ${CMAKE_CURRENT_SOURCE_DIR}/select.sql
  ${CMAKE_CURRENT_SOURCE_DIR}/alter.sql
  ${CMAKE_CURRENT_SOURCE_DIR}/comment.sql
  ${CMAKE_CURRENT_SOURCE_DIR}/whitespace.sql
  ${PG_SQL_TESTS}
)

add_executable(test_canonical_args test_canonical_args.cc)
target_link_libraries(test_canonical_args maxsimd)
add_test(NAME test_canonical_args COMMAND test_canonical_args)
