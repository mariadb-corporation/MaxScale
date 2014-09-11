add_library(mysqlmon SHARED mysql_mon.c)
set_target_properties(mysqlmon PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(mysqlmon utils log_manager mysqld)
install(TARGETS mysqlmon DESTINATION modules)

add_library(galeramon SHARED galera_mon.c)
set_target_properties(galeramon PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(galeramon utils log_manager mysqld)
install(TARGETS galeramon DESTINATION modules)

add_library(ndbclustermon SHARED ndbcluster_mon.c)
set_target_properties(ndbclustermon PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_RPATH}:${CMAKE_INSTALL_PREFIX}/lib)
target_link_libraries(ndbclustermon utils log_manager mysqld)
install(TARGETS ndbclustermon DESTINATION modules)