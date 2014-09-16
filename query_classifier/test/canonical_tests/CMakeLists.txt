file(COPY ${ERRMSG} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
add_executable(canonizer canonizer.c)
target_link_libraries(canonizer pthread query_classifier z dl ssl aio crypt crypto rt m  ${EMBEDDED_LIB} fullcore stdc++)
add_test(NAME TestCanonicalQuery COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/canontest.sh 
  ${CMAKE_CURRENT_BINARY_DIR}/test.log
  ${CMAKE_CURRENT_SOURCE_DIR}/input.sql
  ${CMAKE_CURRENT_BINARY_DIR}/output.sql
  ${CMAKE_CURRENT_SOURCE_DIR}/expected.sql
  $<TARGET_FILE:canonizer>)
