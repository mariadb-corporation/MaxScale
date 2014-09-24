add_test(NAME ReadWriteSplitTest COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/rwsplit.sh testrwsplit.log ${TEST_HOST} ${TEST_PORT_RW} ${TEST_MASTER_ID} ${TEST_USER} ${TEST_PASSWORD} ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(ReadWriteSplitTest PROPERTIES DEPENDS RunExecutable)
add_subdirectory(test_hints)