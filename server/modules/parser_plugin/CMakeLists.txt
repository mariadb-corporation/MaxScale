add_subdirectory(qc_sqlite)

if(BUILD_QC_MYSQLEMBEDDED)
  add_subdirectory(qc_mysqlembedded)
endif()

if(BUILD_PP_PG_QUERY)
  add_subdirectory(pp_pg_query)
endif()

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
