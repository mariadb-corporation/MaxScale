add_library(MySQLClient SHARED mysql_client.c mysql_common.c)
target_link_libraries(MySQLClient log_manager utils)
install(TARGETS MySQLClient DESTINATION modules)

add_library(MySQLBackend SHARED mysql_backend.c mysql_common.c)
target_link_libraries(MySQLBackend log_manager utils)
install(TARGETS MySQLBackend DESTINATION modules)

add_library(telnetd SHARED telnetd.c)
target_link_libraries(telnetd log_manager utils)
install(TARGETS telnetd DESTINATION modules)

add_library(HTTPD SHARED httpd.c)
target_link_libraries(HTTPD log_manager utils)
install(TARGETS HTTPD DESTINATION modules)

if(BUILD_TESTS)
  add_library(testprotocol SHARED testprotocol.c)
  install(TARGETS testprotocol DESTINATION modules)
endif()

add_library(maxscaled SHARED maxscaled.c)
target_link_libraries(maxscaled log_manager utils)
install(TARGETS maxscaled DESTINATION modules)





