project(maxscale_system_test)
cmake_minimum_required(VERSION 2.8)

include(macros.cmake)

set_maxscale_version()

# Installation directory

set(CMAKE_INSTALL_PREFIX "/usr/local/mariadb-maxscale" CACHE INTERNAL "Prefix prepended to install directories." FORCE)
# Build type
set(CMAKE_BUILD_TYPE "None" CACHE STRING "Build type, possible values are:None, Debug, Optimized.")

check_deps()
check_dirs()

set(CTEST_BUILD_NAME "${BUILDNAME}")
enable_testing()
ENABLE_TESTING()
INCLUDE(CTest)

configure_file(${CMAKE_SOURCE_DIR}/cnf/maxscale.cnf.template.setup_binlog.in ${CMAKE_BINARY_DIR}/cnf/maxscale.cnf.template.setup_binlog @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/set_env.sh.in ${CMAKE_BINARY_DIR}/set_env.sh @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/test_ctrl_c/test_ctrl_c.sh.in ${CMAKE_BINARY_DIR}/test_ctrl_c/test_ctrl_c.sh @ONLY)

include_directories("/usr/include/mysql/")
#LINK_DIRECTORIES("/usr/lib/x86_64-linux-gnu/")
aux_source_directory(. SRC_LIST)

add_executable(readconnrouter_master readconnrouter_master.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(readconnrouter_master ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(readconnrouter_slave readconnrouter_slave.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(readconnrouter_slave ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(rwsplit_conn_num rwsplit_conn_num.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(rwsplit_conn_num ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(sql_queries sql_queries.cpp sql_t1.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(sql_queries ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(rw_galera_select_insert rw_galera_select_insert.cpp  get_com_select_insert.cpp maxadmin_operations.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(rw_galera_select_insert ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(rwsplit_connect rwsplit_connect.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(rwsplit_connect ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(rw_select_insert rw_select_insert.cpp get_com_select_insert.cpp maxadmin_operations.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(rw_select_insert ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(server_weight server_weight.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(server_weight ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(slave_failover slave_failover.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(slave_failover ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(kill_master kill_master.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(kill_master ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(sysbench_kill_slave sysbench_kill_slave.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(sysbench_kill_slave ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(slave_lag slave_lag.cpp testconnections.cpp sql_t1.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(slave_lag ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug359 bug359.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug359 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug493 bug493.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug493 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug495 bug495.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug495 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug479 bug479.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug479 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug572 bug572.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug572 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(short_sessions short_sessions.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(short_sessions ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(connect_to_nonexisting_db connect_to_nonexisting_db.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(connect_to_nonexisting_db ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(temporal_tables temporal_tables.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(temporal_tables ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(change_master_during_session change_master_during_session.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(change_master_during_session ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug143 bug143.cpp testconnections.cpp  mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug143 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug488 bug488.cpp testconnections.cpp  mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug488 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug507 bug507.cpp testconnections.cpp  mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug507 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug509 bug509.cpp testconnections.cpp  mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug509 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug526 bug526.cpp testconnections.cpp  mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug526 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(change_master change_master.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(change_master ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug529 bug529.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug529 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug547 bug547.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug547 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug422 bug422.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug422 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug571 bug571.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug571 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug473 bug473.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug473 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug587 bug587.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug587 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug471 bug471.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug471 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug475 bug475.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug475 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(change_user change_user.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(change_user ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug565 bug565.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug565 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(load_balancing load_balancing.cpp big_load.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(load_balancing ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(crash_out_of_files crash_out_of_files.cpp big_load.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(crash_out_of_files ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(crash_out_of_files_galera crash_out_of_files_galera.cpp big_load.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(crash_out_of_files_galera ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug448 bug448.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug448 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(prepared_statement prepared_statement.cpp sql_t1.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(prepared_statement ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug592 bug592.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug592 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug620 bug620.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug620 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug634 bug634.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug634 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug626 bug626.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug626 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug643 bug643.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug643 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug643_1 bug643_1.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug643_1 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug645 bug645.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug645 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug645_1 bug645_1.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug645_1 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug601 bug601.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug601 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug649 bug649.cpp testconnections.cpp sql_t1.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug649 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug650 bug650.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug650 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug653 bug653.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug653 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug657 bug657.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug657 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug676 bug676.cpp testconnections.cpp mariadb_nodes.cpp get_my_ip.cpp mariadb_func.cpp)
target_link_libraries(bug676 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(generate_log_sql generate_log_sql.cpp sql_t1.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(generate_log_sql ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug654 bug654.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug654 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug658 bug658.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug658 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug662 bug662.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug662 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug664 bug664.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug664 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug681 bug681.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug681 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug673 bug673.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug673 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug670 bug670.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug670 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug539 bug539.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug539 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug694 bug694.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug694 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug699 bug699.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug699 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug705 bug705.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug705 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(setup_binlog setup_binlog.cpp testconnections.cpp sql_t1.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(setup_binlog ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(setup_binlog_crc_none setup_binlog_crc_none.cpp testconnections.cpp sql_t1.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(setup_binlog_crc_none ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(setup_binlog_crc_32 setup_binlog_crc_32.cpp testconnections.cpp sql_t1.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(setup_binlog_crc_32 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(binlog_incompl binlog_incompl.cpp testconnections.cpp sql_t1.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(binlog_incompl ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug519 bug519.cpp sql_t1.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug519 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug729 bug729.cpp sql_t1.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug729 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug718 bug718.cpp sql_t1.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug718 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug730 bug730.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug730 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug711 bug711.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug711 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug656 bug656.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug656 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(mxs47 mxs47.cpp testconnections.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(mxs47 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(sharding sharding.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(sharding ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(fwf fwf.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(fwf ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(t t.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(t ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(mm mm.cpp testconnections.cpp sql_t1.cpp maxadmin_operations.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(mm ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(mxs118 mxs118.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(mxs118 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(mxs127 mxs127.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(mxs127 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(transaction_test_wo_maxscale transaction_test_wo_maxscale.cpp sql_t1.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(transaction_test_wo_maxscale ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(session_limits session_limits.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(session_limits ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(script script.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(script ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(maxadmin maxadmin.cpp maxadmin_operations.cpp)

install(TARGETS readconnrouter_master readconnrouter_slave sql_queries rw_galera_select_insert rwsplit_connect rw_select_insert server_weight slave_failover kill_master DESTINATION system-test)
install(TARGETS sysbench_kill_slave slave_lag bug143 bug359 bug493 bug495 bug572 bug479 bug488 bug509 bug526 bug529 bug547 bug422 bug571 bug473 bug587 bug475 bug471 bug565 bug448 DESTINATION system-test)
install(TARGETS short_sessions connect_to_nonexisting_db rwsplit_conn_num temporal_tables change_master_during_session change_master change_user maxadmin load_balancing crash_out_of_files DESTINATION system-test)
install(TARGETS prepared_statement bug592 bug620 bug634 bug626 bug643 bug643_1 bug645 bug645_1 bug601 bug649 bug650 bug653 bug657 bug676 generate_log_sql bug654 bug658 bug662 DESTINATION system-test)
install(TARGETS bug664 bug681 bug673 bug670 bug539 bug694 bug507 bug699 bug705 bug519 setup_binlog setup_binlog_crc_none setup_binlog_crc_32 binlog_incompl bug729 bug718 bug730 bug711 bug656 DESTINATION system-test)
install(TARGETS mxs47 sharding fwf mm mxs118 mxs127 session_limits script DESTINATION system-test)

install(FILES set_env.sh get_logs.sh run_ctrl_c.sh long_insert.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES mariadb_tests_hartmut.sh start_without_root.sh configure_maxscale.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES mariadb_tests_hartmut_galera.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES bug567.sh bug516.sh bug561.sh bug562.sh bug585 bug587_1 bug469.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES run_session_hang.sh bug564.sh crash_out_of_files_galera DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES bug648 copy_logs.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)

install(FILES templates DESTINATION system-test)
install(FILES script_output_expected DESTINATION system-test)
install(FILES script_output_expected_galera DESTINATION system-test)
install(FILES bug729.php DESTINATION system-test)
install(DIRECTORY cnf DESTINATION system-test)
install(DIRECTORY fw DESTINATION system-test)
install(DIRECTORY test_ctrl_c DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY long_insert_sql DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY Hartmut_tests DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY session_hang DESTINATION system-test USE_SOURCE_PERMISSIONS)

add_test(readconnrouter_master readconnrouter_master)
add_test(readconnrouter_slave readconnrouter_slave)
add_test(rwsplit_conn_num rwsplit_conn_num)
add_test(sql_queries sql_queries)
set_tests_properties(sql_queries PROPERTIES TIMEOUT 3600)
add_test(rw_galera_select_insert rw_galera_select_insert)
add_test(rwsplit_connect rwsplit_connect)
add_test(rw_select_insert rw_select_insert)
add_test(server_weight server_weight)
add_test(slave_failover slave_failover)
add_test(kill_master kill_master)
add_test(slave_lag slave_lag)
add_test(bug359 bug359)
add_test(bug493 bug493)
add_test(bug495 bug495)
add_test(bug479 bug479)
add_test(bug572 bug572)
add_test(short_sessions short_sessions)
set_tests_properties(short_sessions PROPERTIES TIMEOUT 3600)
add_test(connect_to_nonexisting_db connect_to_nonexisting_db)
add_test(temporal_tables temporal_tables)
add_test(change_master_during_session change_master_during_session)
add_test(bug143 bug143)
add_test(bug488 bug488)
add_test(bug507 bug507)
add_test(bug509 bug509)
add_test(bug526 bug526)
add_test(bug529 bug529)
#add_test(bug547 bug547)
add_test(bug422 bug422)
add_test(bug571 bug571)
add_test(bug473 bug473)
add_test(bug587 bug587)
add_test(bug471 bug471)
add_test(bug475 bug475)
add_test(change_user change_user)
add_test(bug565 bug565)
add_test(load_balancing load_balancing)
add_test(bug448 bug448)
add_test(prepared_statement prepared_statement)
add_test(bug592 bug592)
add_test(bug620 bug620)
add_test(bug634 bug634)
add_test(bug626 bug626)
add_test(bug643 bug643)
add_test(bug643_1 bug643_1)
add_test(bug645 bug645)
add_test(bug645_1 bug645_1)
add_test(bug601 bug601)
set_tests_properties(bug601 PROPERTIES TIMEOUT 3600)
#add_test(bug649 bug649)
add_test(bug650 bug650)
add_test(bug653 bug653)
add_test(bug657 bug657)
add_test(bug676 bug676)
add_test(bug654 bug654)
add_test(bug658 bug658)
add_test(bug662 bug662)
add_test(bug664 bug664)
add_test(bug681 bug681)
add_test(bug673 bug673)
#add_test(bug670 bug670)
#add_test(bug539 bug539)
add_test(bug694 bug694)
#add_test(bug699 bug699)
add_test(bug705 bug705)
add_test(setup_binlog setup_binlog)
add_test(setup_binlog_crc_none setup_binlog_crc_none)
add_test(setup_binlog_crc_32 setup_binlog_crc_32)
add_test(bug519 bug519)
add_test(bug729 bug729)
add_test(mariadb_tests_hartmut mariadb_tests_hartmut.sh)
add_test(mariadb_tests_hartmut_galera mariadb_tests_hartmut_galera.sh)
add_test(start_without_root start_without_root.sh)
add_test(bug567 bug567.sh)
add_test(bug516 bug516.sh)
add_test(bug561 bug561.sh)
add_test(bug562 bug562.sh)
add_test(bug585 bug585)
add_test(bug587_1 bug587_1)
add_test(bug469 bug469.sh)
add_test(run_session_hang run_session_hang.sh)
set_tests_properties(run_session_hang PROPERTIES TIMEOUT 3600)
add_test(bug564 bug564.sh)
add_test(bug648 bug648)
set_tests_properties(bug648 PROPERTIES TIMEOUT 3600)
add_test(bug730 bug730)
set_tests_properties(bug730 PROPERTIES TIMEOUT 3600)
add_test(bug711 bug711)
add_test(crash_out_of_files crash_out_of_files)
add_test(crash_out_of_files_galera crash_out_of_files_galera)
add_test(run_ctrl_c run_ctrl_c.sh)
add_test(sysbench_kill_slave sysbench_kill_slave)
set_tests_properties(sysbench_kill_slave PROPERTIES TIMEOUT 3600)
add_test(binlog_incompl binlog_incompl)
add_test(mxs47 mxs47)
set_tests_properties(mxs47 PROPERTIES TIMEOUT 1000)
add_test(sharding sharding)
add_test(fwf fwf)
add_test(mxs118 mxs118)
add_test(mxs127 mxs127)
add_test(session_limits session_limits)
add_test(script script)
add_test(mm mm)

# See if we are on a RPM-capable or DEB-capable system
find_program(RPMBUILD rpmbuild)
find_program(DEBBUILD dpkg-buildpackage)

if(NOT ( ${RPMBUILD} STREQUAL "RPMBUILD-NOTFOUND" ) )
  message(STATUS "Generating RPM packages")
  set(CPACK_GENERATOR "${CPACK_GENERATOR};RPM;TGZ")
endif()

if(NOT ( ${DEBBUILD} STREQUAL "DEBBUILD-NOTFOUND" ) )
  set(CPACK_GENERATOR "${CPACK_GENERATOR};DEB;TGZ")
  execute_process(COMMAND dpgk --print-architecture OUTPUT_VARIABLE DEB_ARCHITECTURE)
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${DEB_ARCHITECTURE})
  set (CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  message(STATUS "Generating DEB packages for ${DEB_ARCHITECTURE}")
endif()

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MaxScale-system-test")
set(CPACK_PACKAGE_VERSION_MAJOR "${MAXSCALE_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${MAXSCALE_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${MAXSCALE_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "MariaDB Corporation Ab")
set(CPACK_PACKAGE_FILE_NAME "maxscale-system-test-${MAXSCALE_VERSION}")
set(CPACK_PACKAGE_NAME "maxscale-system-test")
set(CPACK_PACKAGE_VENDOR "MariaDB Corporation Ab")
set(CPACK_PACKAGE_DESCRIPTION_FILE ${CMAKE_SOURCE_DIR}/README)
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(CPACK_RPM_PACKAGE_NAME "maxscale-system-test")
set(CPACK_RPM_PACKAGE_VENDOR "MariaDB Corporation Ab")
set(CPACK_RPM_PACKAGE_LICENSE "GPLv2")
include(CPack)
