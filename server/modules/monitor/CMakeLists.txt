add_library(mysqlmon SHARED mysql_mon.c)
target_link_libraries(mysqlmon utils log_manager mysqld)
install(TARGETS mysqlmon DESTINATION modules)

add_library(galeramon SHARED galera_mon.c)
target_link_libraries(galeramon utils log_manager mysqld)
install(TARGETS galeramon DESTINATION modules)

add_library(ndbclustermon SHARED ndbcluster_mon.c)
target_link_libraries(ndbclustermon utils log_manager mysqld)
install(TARGETS ndbclustermon DESTINATION modules)