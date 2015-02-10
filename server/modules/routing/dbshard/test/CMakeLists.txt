if(MYSQLCLIENT_FOUND AND BUILD_TESTS)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/test.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/test.cmake @ONLY)
  add_executable(testdbshard testdbshard.c)
  target_link_libraries(testdbshard ${MYSQLCLIENT_LIBRARIES} ssl crypto dl z m rt pthread)
  add_executable(testdbshard2 testdbshard2.c)
  target_link_libraries(testdbshard2 ${MYSQLCLIENT_LIBRARIES} ssl crypto dl z m rt pthread)
  add_test(NAME TestDBShard COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/test.cmake)

endif()
