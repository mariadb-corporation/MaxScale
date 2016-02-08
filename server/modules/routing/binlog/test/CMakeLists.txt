if(BUILD_TESTS)
  add_executable(testbinlogrouter testbinlog.c ../blr.c ../blr_slave.c ../blr_master.c ../blr_file.c ../blr_cache.c)
  # testbinlogrouter refers to my_uuid_init and my_uuin. They are non-public functions and
  # should not be used. They are found only from the embedded lib.
  target_link_libraries(testbinlogrouter maxscale-common ${MYSQL_EMBEDDED_LIBRARIES})
  add_test(TestBinlogRouter ${CMAKE_CURRENT_BINARY_DIR}/testbinlogrouter)
endif()
