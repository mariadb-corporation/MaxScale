add_subdirectory(qc_mysqlembedded)
add_subdirectory(qc_pp)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
