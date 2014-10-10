project(maxscale_system_test)
cmake_minimum_required(VERSION 2.8)
include_directories("/usr/include/mysql/")
#LINK_DIRECTORIES("/usr/lib/x86_64-linux-gnu/")
aux_source_directory(. SRC_LIST)

add_executable(readconnrouter_master readconnrouter_master.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(readconnrouter_master mariadbclient z crypt nsl m pthread ssl crypto dl)

add_executable(readconnrouter_slave readconnrouter_slave.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(readconnrouter_slave mariadbclient z crypt nsl m pthread ssl crypto dl)

add_executable(sql_queries sql_queries.cpp sql_t1.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(sql_queries mariadbclient z crypt nsl m pthread ssl crypto dl)

add_executable(rw_galera_select_insert rw_galera_select_insert.cpp get_com_select_insert.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(rw_galera_select_insert mariadbclient z crypt nsl m pthread ssl crypto dl)

add_executable(rwsplit_connect rwsplit_connect.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(rwsplit_connect mariadbclient z crypt nsl m pthread ssl crypto dl)

add_executable(rw_select_insert rw_select_insert.cpp get_com_select_insert.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(rw_select_insert mariadbclient z crypt nsl m pthread ssl crypto dl)

add_executable(server_weight server_weight.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(server_weight mariadbclient z crypt nsl m pthread ssl crypto dl)

add_executable(slave_failover slave_failover.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(slave_failover mariadbclient z crypt nsl m pthread ssl crypto dl)

add_executable(slave_lag slave_lag.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(slave_lag mariadbclient z crypt nsl m pthread ssl crypto dl)
