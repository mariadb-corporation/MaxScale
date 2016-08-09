add_subdirectory(qc_mysqlembedded)
add_subdirectory(qc_sqlite)
add_subdirectory(qc_dummy)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
