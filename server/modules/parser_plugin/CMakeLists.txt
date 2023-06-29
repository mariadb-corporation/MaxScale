add_subdirectory(pp_sqlite)

if(BUILD_PP_MYSQLEMBEDDED)
  add_subdirectory(pp_mysqlembedded)
endif()

if(BUILD_POSTGRES)
  add_subdirectory(pp_pg_query)
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
