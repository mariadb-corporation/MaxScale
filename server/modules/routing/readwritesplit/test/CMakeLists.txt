add_test(NAME ReadWriteSplitTest COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/rwsplit.sh testrwsplit.log ${TEST_HOST} ${TEST_PORT_RW} ${TEST_MASTER_ID} ${TEST_USER} ${TEST_PASSWORD} ${CMAKE_CURRENT_SOURCE_DIR})

#find_library(MYSQLCLIENT_LIBARIES NAMES mysqlclient PATH_SUFFIXES mysql mariadb)
#if( NOT (MYSQLCLIENT_LIBRARIES MATCHES "NOTFOUND"))
#  add_test(NAME ReadWriteSplitLoginTest COMMAND $<TARGET_FILE:testconnect> 10000 ${TEST_HOST} ${MASTER_PORT} ${TEST_HOST} ${TEST_PORT_RW} 1.10)
#endif()


add_subdirectory(test_hints)