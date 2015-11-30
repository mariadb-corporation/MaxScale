add_library(MySQLClient SHARED mysql_client.c mysql_common.c)
target_link_libraries(MySQLClient log_manager)
set_target_properties(MySQLClient PROPERTIES VERSION "1.0.0")
install(TARGETS MySQLClient DESTINATION ${MAXSCALE_LIBDIR})

add_library(MySQLBackend SHARED mysql_backend.c mysql_common.c)
target_link_libraries(MySQLBackend log_manager)
set_target_properties(MySQLBackend PROPERTIES VERSION "2.0.0")
install(TARGETS MySQLBackend DESTINATION ${MAXSCALE_LIBDIR})

add_library(telnetd SHARED telnetd.c)
target_link_libraries(telnetd log_manager)
set_target_properties(telnetd PROPERTIES VERSION "1.0.1")
install(TARGETS telnetd DESTINATION ${MAXSCALE_LIBDIR})

add_library(HTTPD SHARED httpd.c)
target_link_libraries(HTTPD log_manager)
set_target_properties(HTTPD PROPERTIES VERSION "1.0.1")
install(TARGETS HTTPD DESTINATION ${MAXSCALE_LIBDIR})

if(BUILD_TESTS)
  add_library(testprotocol SHARED testprotocol.c)
  set_target_properties(testprotocol PROPERTIES VERSION "1.0.0")
  install(TARGETS testprotocol DESTINATION ${MAXSCALE_LIBDIR})
  add_subdirectory(test)
endif()

add_library(maxscaled SHARED maxscaled.c)
target_link_libraries(maxscaled log_manager)
set_target_properties(maxscaled PROPERTIES VERSION "1.0.0")
install(TARGETS maxscaled DESTINATION ${MAXSCALE_LIBDIR})





