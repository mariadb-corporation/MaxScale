include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(harness_ui harness_ui.c harness_common.c)
add_executable(harness harness_util.c harness_common.c)
target_link_libraries(harness_ui fullcore log_manager utils)
target_link_libraries(harness fullcore)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${ERRMSG} ${CMAKE_CURRENT_BINARY_DIR})
execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/harness.cnf ${CMAKE_CURRENT_BINARY_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/testdriver.sh ${CMAKE_CURRENT_BINARY_DIR}/testdriver.sh @ONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/hintfilter/hint_testing.cnf ${CMAKE_CURRENT_BINARY_DIR}/hintfilter/hint_testing.cnf)
add_test(TestHintfilter testdriver.sh hintfilter/hint_testing.cnf hintfilter/hint_testing.input hintfilter/hint_testing.output hintfilter/hint_testing.expected)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/regexfilter/regextest.cnf ${CMAKE_CURRENT_BINARY_DIR}/regexfilter/regextest.cnf)
add_test(TestRegexfilter testdriver.sh regexfilter/regextest.cnf regexfilter/regextest.input regexfilter/regextest.output regexfilter/regextest.expected)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/fwfilter/fwtest.cnf.in ${CMAKE_CURRENT_BINARY_DIR}/fwfilter/fwtest.cnf)
add_test(TestFwfilter1 testdriver.sh fwfilter/fwtest.cnf fwfilter/fwtest.input fwfilter/fwtest.output fwfilter/fwtest.expected)
add_test(TestFwfilter2 testdriver.sh fwfilter/fwtest.cnf fwfilter/fwtest2.input fwfilter/fwtest2.output fwfilter/fwtest2.expected)

add_test(TestTeeRecursion ${CMAKE_CURRENT_SOURCE_DIR}/tee_recursion.sh
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}
  ${TEST_USER}
  ${TEST_PASSWORD}
  ${TEST_HOST}
  ${TEST_PORT})
