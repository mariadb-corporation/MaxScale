add_subdirectory(qc_mysqlembedded)
add_subdirectory(qc_sqlite)

if(BUILD_PP_POSTGRES)
  add_subdirectory(pp_postgres)
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
