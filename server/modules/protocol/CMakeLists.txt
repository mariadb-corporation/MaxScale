add_library(MySQLClient SHARED mysql_client.c mysql_common.c)
set_target_properties(MySQLClient PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(MySQLClient utils log_manager)
install(TARGETS MySQLClient DESTINATION modules)

add_library(MySQLBackend SHARED mysql_backend.c mysql_common.c)
set_target_properties(MySQLBackend PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(MySQLBackend utils log_manager)
install(TARGETS MySQLBackend DESTINATION modules)

add_library(telnetd SHARED telnetd.c)
set_target_properties(telnetd PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(telnetd utils log_manager)
install(TARGETS telnetd DESTINATION modules)

add_library(HTTPD SHARED httpd.c)
set_target_properties(HTTPD PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(HTTPD utils log_manager)
install(TARGETS HTTPD DESTINATION modules)

add_library(maxscaled SHARED maxscaled.c)
set_target_properties(maxscaled PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(maxscaled utils log_manager)
install(TARGETS maxscaled DESTINATION modules)





