add_library(mysqlmon SHARED mysql_mon.c)
target_link_libraries(mysqlmon log_manager utils)
install(TARGETS mysqlmon DESTINATION modules)

add_library(galeramon SHARED galera_mon.c)
target_link_libraries(galeramon log_manager utils)
install(TARGETS galeramon DESTINATION modules)

add_library(ndbclustermon SHARED ndbcluster_mon.c)
target_link_libraries(ndbclustermon log_manager utils)
install(TARGETS ndbclustermon DESTINATION modules)
