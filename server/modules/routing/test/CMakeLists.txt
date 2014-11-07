add_executable(testconnect testconnect.c)
target_link_libraries(testconnect mysql)
add_test(NAME ReadConnRouterLoginTest COMMAND $<TARGET_FILE:testconnect> 10000 ${TEST_HOST} ${MASTER_PORT} ${TEST_HOST} ${TEST_PORT})