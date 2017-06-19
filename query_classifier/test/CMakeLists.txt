# Include the embedded library headers
if (BUILD_QC_MYSQLEMBEDDED)
  subdirs(MYSQL_INCLUDE_DIR_ALL ${MYSQL_EMBEDDED_INCLUDE_DIR})
  foreach(DIR ${MYSQL_INCLUDE_DIR_ALL})
    include_directories(${DIR})
  endforeach()
  include_directories(${MYSQL_EMBEDDED_INCLUDE_DIR}/..)

  if(${ERRMSG} MATCHES "ERRMSG-NOTFOUND")
    message(FATAL_ERROR "The errmsg.sys file was not found, please define the path with -DERRMSG=<path>")
  else()
    if(${CMAKE_VERSION} VERSION_LESS 2.8)
	  execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${ERRMSG} ${CMAKE_CURRENT_BINARY_DIR})
    else()
	  file(COPY ${ERRMSG} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
    endif()
  endif()

  add_executable(classify classify.c)
  target_link_libraries(classify maxscale-common)

  add_executable(compare compare.cc testreader.cc)
  target_link_libraries(compare maxscale-common)

  add_executable(version_sensitivity version_sensitivity.cc)
  target_link_libraries(version_sensitivity maxscale-common)

  add_executable(crash_qc_sqlite crash_qc_sqlite.c)
  target_link_libraries(crash_qc_sqlite maxscale-common)

  add_test(TestQC_Crash_qcsqlite crash_qc_sqlite)

  add_test(TestQC_MySQLEmbedded classify qc_mysqlembedded ${CMAKE_CURRENT_SOURCE_DIR}/input.sql ${CMAKE_CURRENT_SOURCE_DIR}/expected.sql)
  add_test(TestQC_SqLite classify qc_sqlite ${CMAKE_CURRENT_SOURCE_DIR}/input.sql ${CMAKE_CURRENT_SOURCE_DIR}/expected.sql)

  add_test(TestQC_CompareCreate compare -v 2 ${CMAKE_CURRENT_SOURCE_DIR}/create.test)
  add_test(TestQC_CompareDelete compare -v 2 ${CMAKE_CURRENT_SOURCE_DIR}/delete.test)
  add_test(TestQC_CompareInsert compare -v 2 ${CMAKE_CURRENT_SOURCE_DIR}/insert.test)
  add_test(TestQC_CompareJoin compare -v 2 ${CMAKE_CURRENT_SOURCE_DIR}/join.test)
  add_test(TestQC_CompareSelect compare -v 2 ${CMAKE_CURRENT_SOURCE_DIR}/select.test)
  add_test(TestQC_CompareSet compare -v 2 ${CMAKE_CURRENT_SOURCE_DIR}/set.test)
  add_test(TestQC_CompareUpdate compare -v 2 ${CMAKE_CURRENT_SOURCE_DIR}/update.test)
  add_test(TestQC_CompareMaxScale compare -v 2 ${CMAKE_CURRENT_SOURCE_DIR}/maxscale.test)
  add_test(TestQC_CompareWhiteSpace compare -v 2 -S -s "select user from mysql.user; ")

  add_test(TestQC_version_sensitivity version_sensitivity)
endif()

add_subdirectory(canonical_tests)
