# libssl-dev libmariadbclient-dev php5
project(maxscale_system_test)
cmake_minimum_required(VERSION 2.8)

include(macros.cmake)

set_maxscale_version()

# Installation directory

set(CMAKE_INSTALL_PREFIX "/usr/local/mariadb-maxscale" CACHE INTERNAL "Prefix prepended to install directories." FORCE)
set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/system-test)
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



add_library(testcore SHARED testconnections.cpp mariadb_nodes.cpp
  mariadb_func.cpp get_com_select_insert.cpp maxadmin_operations.cpp big_transaction.cpp
  sql_t1.cpp test_binlog_fnc.cpp get_my_ip.cpp big_load.cpp  get_com_select_insert.cpp)
target_link_libraries(testcore ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)
install(TARGETS testcore DESTINATION system-test)

# This functions adds a source file as an executable, links that file against
# the common test core and creates a test from it. The first parameter is the
# source file, the second is the name of the executable and the test and the
# last parameter is the template suffix of the test. The template should follow
# the following naming policy: `maxscale.cnf.template.<template name>` and the
# file should be located in the /cnf/ directory.
#
# Example: to add simple_test.cpp with maxscale.cnf.template.simple_config to the
# test set, the function should be called as follows:
#     add_test_executable(simple_test.cpp simple_test simple_config)
function(add_test_executable source name template)
  file(APPEND templates "${name} ${template}\n")
  add_executable(${name} ${source})
  target_link_libraries(${name} testcore)
  install(TARGETS ${name} DESTINATION system-test)
  add_test(${name} ${name})
endfunction()

# This function adds a script as a test with the specified name and template.
# The naming of the templates follow the same principles as add_test_executable.
function(add_test_script script suffix)
  file(APPEND templates "${name} ${template}\n")
  install(PROGRAMS ${name} DESTINATION system-test)
  add_test(${name} ${name})
endfunction()


add_executable(readconnrouter_master readconnrouter_master.cpp)
target_link_libraries(readconnrouter_master testcore)

add_executable(readconnrouter_slave readconnrouter_slave.cpp)
target_link_libraries(readconnrouter_slave testcore)

add_executable(rwsplit_conn_num rwsplit_conn_num.cpp)
target_link_libraries(rwsplit_conn_num testcore)

add_executable(sql_queries sql_queries.cpp)
target_link_libraries(sql_queries testcore)

add_executable(rw_galera_select_insert rw_galera_select_insert.cpp)
target_link_libraries(rw_galera_select_insert testcore)

add_executable(rwsplit_connect rwsplit_connect.cpp)
target_link_libraries(rwsplit_connect testcore)

add_executable(rw_select_insert rw_select_insert.cpp)
target_link_libraries(rw_select_insert testcore)

add_executable(server_weight server_weight.cpp)
target_link_libraries(server_weight testcore)

add_executable(slave_failover slave_failover.cpp)
target_link_libraries(slave_failover testcore)

add_executable(kill_master kill_master.cpp)
target_link_libraries(kill_master testcore)

add_executable(sysbench_kill_slave sysbench_kill_slave.cpp)
target_link_libraries(sysbench_kill_slave testcore)

add_executable(slave_lag slave_lag.cpp)
target_link_libraries(slave_lag testcore)

add_executable(bug359 bug359.cpp)
target_link_libraries(bug359 testcore)

add_executable(bug493 bug493.cpp)
target_link_libraries(bug493 testcore)

add_executable(bug495 bug495.cpp)
target_link_libraries(bug495 testcore)

add_executable(bug479 bug479.cpp)
target_link_libraries(bug479 testcore)

add_executable(bug572 bug572.cpp)
target_link_libraries(bug572 testcore)

add_executable(short_sessions short_sessions.cpp)
target_link_libraries(short_sessions testcore)

add_executable(connect_to_nonexisting_db connect_to_nonexisting_db.cpp)
target_link_libraries(connect_to_nonexisting_db testcore)

add_executable(temporal_tables temporal_tables.cpp)
target_link_libraries(temporal_tables testcore)

add_executable(change_master_during_session change_master_during_session.cpp)
target_link_libraries(change_master_during_session testcore)

add_executable(bug143 bug143.cpp)
target_link_libraries(bug143 testcore)

add_executable(bug488 bug488.cpp)
target_link_libraries(bug488 testcore)

add_executable(bug507 bug507.cpp)
target_link_libraries(bug507 testcore)

add_executable(bug509 bug509.cpp)
target_link_libraries(bug509 testcore)

add_executable(bug526 bug526.cpp)
target_link_libraries(bug526 testcore)

add_executable(change_master change_master.cpp)
target_link_libraries(change_master testcore)

add_executable(bug529 bug529.cpp)
target_link_libraries(bug529 testcore)

add_executable(bug547 bug547.cpp)
target_link_libraries(bug547 testcore)

add_executable(bug422 bug422.cpp)
target_link_libraries(bug422 testcore)

add_executable(bug571 bug571.cpp)
target_link_libraries(bug571 testcore)

add_executable(bug473 bug473.cpp)
target_link_libraries(bug473 testcore)

add_executable(bug587 bug587.cpp)
target_link_libraries(bug587 testcore)

add_executable(bug471 bug471.cpp)
target_link_libraries(bug471 testcore)

add_executable(bug475 bug475.cpp)
target_link_libraries(bug475 testcore)

add_executable(change_user change_user.cpp)
target_link_libraries(change_user testcore)

add_executable(bug565 bug565.cpp)
target_link_libraries(bug565 testcore)

add_executable(load_balancing load_balancing.cpp)
target_link_libraries(load_balancing testcore)

add_executable(load_balancing_galera load_balancing_galera.cpp)
target_link_libraries(load_balancing_galera testcore)

add_executable(crash_out_of_files crash_out_of_files.cpp)
target_link_libraries(crash_out_of_files testcore)

add_executable(crash_out_of_files_galera crash_out_of_files_galera.cpp)
target_link_libraries(crash_out_of_files_galera testcore)

add_executable(bug448 bug448.cpp)
target_link_libraries(bug448 testcore)

add_executable(prepared_statement prepared_statement.cpp)
target_link_libraries(prepared_statement testcore)

add_executable(bug592 bug592.cpp)
target_link_libraries(bug592 testcore)

add_executable(bug620 bug620.cpp)
target_link_libraries(bug620 testcore)

add_executable(bug634 bug634.cpp)
target_link_libraries(bug634 testcore)

add_executable(bug626 bug626.cpp)
target_link_libraries(bug626 testcore)

add_executable(bug643 bug643.cpp)
target_link_libraries(bug643 testcore)

add_executable(bug643_1 bug643_1.cpp)
target_link_libraries(bug643_1 testcore)

add_executable(bug645 bug645.cpp)
target_link_libraries(bug645 testcore)

add_executable(bug645_1 bug645_1.cpp)
target_link_libraries(bug645_1 testcore)

add_executable(bug601 bug601.cpp)
target_link_libraries(bug601 testcore)

add_executable(bug649 bug649.cpp)
target_link_libraries(bug649 testcore)

add_executable(bug650 bug650.cpp)
target_link_libraries(bug650 testcore)

add_executable(bug653 bug653.cpp)
target_link_libraries(bug653 testcore)

add_executable(bug657 bug657.cpp)
target_link_libraries(bug657 testcore)

add_executable(bug676 bug676.cpp)
target_link_libraries(bug676 testcore)

add_executable(generate_log_sql generate_log_sql.cpp)
target_link_libraries(generate_log_sql testcore)

add_executable(bug654 bug654.cpp)
target_link_libraries(bug654 testcore)

add_executable(bug658 bug658.cpp)
target_link_libraries(bug658 testcore)

add_executable(bug662 bug662.cpp)
target_link_libraries(bug662 testcore)

add_executable(bug664 bug664.cpp)
target_link_libraries(bug664 testcore)

add_executable(bug681 bug681.cpp)
target_link_libraries(bug681 testcore)

add_executable(bug673 bug673.cpp)
target_link_libraries(bug673 testcore)

add_executable(bug670 bug670.cpp)
target_link_libraries(bug670 testcore)

add_executable(bug539 bug539.cpp)
target_link_libraries(bug539 testcore)

add_executable(bug694 bug694.cpp)
target_link_libraries(bug694 testcore)

add_executable(bug699 bug699.cpp)
target_link_libraries(bug699 testcore)

add_executable(bug705 bug705.cpp)
target_link_libraries(bug705 testcore)

add_executable(setup_binlog setup_binlog.cpp)
target_link_libraries(setup_binlog testcore)

add_executable(setup_binlog_crc_none setup_binlog_crc_none.cpp)
target_link_libraries(setup_binlog_crc_none testcore)

add_executable(setup_binlog_crc_32 setup_binlog_crc_32.cpp)
target_link_libraries(setup_binlog_crc_32 testcore)

add_executable(binlog_incompl binlog_incompl.cpp)
target_link_libraries(binlog_incompl testcore)

add_executable(bug519 bug519.cpp)
target_link_libraries(bug519 testcore)

add_executable(bug729 bug729.cpp)
target_link_libraries(bug729 testcore)

add_executable(bug718 bug718.cpp)
target_link_libraries(bug718 testcore)

add_executable(bug730 bug730.cpp)
target_link_libraries(bug730 testcore)

add_executable(bug711 bug711.cpp)
target_link_libraries(bug711 testcore)

add_executable(bug656 bug656.cpp)
target_link_libraries(bug656 testcore)

add_executable(mxs47 mxs47.cpp)
target_link_libraries(mxs47 testcore)

add_executable(sharding sharding.cpp)
target_link_libraries(sharding testcore)

add_executable(schemarouter_duplicate_db schemarouter_duplicate_db.cpp)
target_link_libraries(schemarouter_duplicate_db testcore)

add_executable(mxs431 mxs431.cpp)
target_link_libraries(mxs431 testcore)

add_executable(fwf fwf.cpp)
target_link_libraries(fwf testcore)

add_executable(t t.cpp)
target_link_libraries(t testcore)

add_executable(mm mm.cpp)
target_link_libraries(mm testcore)

add_executable(mxs118 mxs118.cpp)
target_link_libraries(mxs118 testcore)

add_executable(mxs127 mxs127.cpp)
target_link_libraries(mxs127 testcore)

add_executable(transaction_test_wo_maxscale transaction_test_wo_maxscale.cpp)
target_link_libraries(transaction_test_wo_maxscale testcore)

add_executable(session_limits session_limits.cpp)
target_link_libraries(session_limits testcore)

add_executable(script script.cpp)
target_link_libraries(script testcore)

add_executable(config_reload config_reload.cpp)
target_link_libraries(config_reload testcore)

add_executable(config_reload_stress config_reload_stress.cpp)
target_link_libraries(config_reload_stress testcore)

add_executable(mxs314 mxs314.cpp)
target_link_libraries(mxs314 testcore)

add_executable(mxs321 mxs321.cpp)
target_link_libraries(mxs321 testcore)

add_executable(pers_01 pers_01.cpp)
target_link_libraries(pers_01 testcore)

add_executable(pers_02 pers_02.cpp)
target_link_libraries(pers_02 testcore)

add_executable(binlog_big_transaction binlog_big_transaction.cpp)
target_link_libraries(binlog_big_transaction testcore)

add_executable(bug469 bug469.cpp)
target_link_libraries(bug469 testcore)

add_executable(open_close_connections open_close_connections.cpp)
target_link_libraries(open_close_connections testcore)

add_executable(galera_priority galera_priority.cpp)
target_link_libraries(galera_priority testcore)

add_executable(maxadmin maxadmin.cpp)
target_link_libraries(maxadmin testcore)

add_test_executable(regexfilter1.cpp regexfilter1 regexfilter1)
add_test_executable(namedserverfilter.cpp namedserverfilter namedserverfilter)

add_test_executable(longblob.cpp longblob replication)

install(TARGETS readconnrouter_master readconnrouter_slave sql_queries rw_galera_select_insert rwsplit_connect rw_select_insert server_weight slave_failover kill_master DESTINATION system-test)
install(TARGETS sysbench_kill_slave slave_lag bug143 bug359 bug493 bug495 bug572 bug479 bug488 bug509 bug526 bug529 bug547 bug422 bug571 bug473 bug587 bug475 bug471 bug565 bug448 DESTINATION system-test)
install(TARGETS short_sessions connect_to_nonexisting_db rwsplit_conn_num temporal_tables change_master_during_session change_master change_user maxadmin load_balancing crash_out_of_files DESTINATION system-test)
install(TARGETS prepared_statement bug592 bug620 bug634 bug626 bug643 bug643_1 bug645 bug645_1 bug601 bug649 bug650 bug653 bug657 bug676 generate_log_sql bug654 bug658 bug662 DESTINATION system-test)
install(TARGETS bug664 bug681 bug673 bug670 bug539 bug694 bug507 bug699 bug705 bug519 setup_binlog setup_binlog_crc_none setup_binlog_crc_32 binlog_incompl bug729 bug718 bug730 bug711 bug656 DESTINATION system-test)
install(TARGETS mxs47 sharding schemarouter_duplicate_db fwf mm mxs118 mxs127 session_limits script config_reload config_reload_stress mxs314 mxs321 DESTINATION system-test)
install(TARGETS pers_01 pers_02 mxs431 binlog_big_transaction bug469 load_balancing_galera open_close_connections galera_priority DESTINATION system-test)

install(FILES set_env.sh get_logs.sh run_ctrl_c.sh long_insert.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES mariadb_tests_hartmut.sh configure_maxscale.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES mariadb_tests_hartmut_galera.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES bug567.sh bug516.sh bug561.sh bug562.sh bug585 bug587_1 reload_server.sh reload_service.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES run_session_hang.sh bug564.sh crash_out_of_files_galera DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES bug648 ssl ssl_load ssl_load_galera mxs361 set_env.sh copy_logs.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES short_sessions_ssl open_close_connections_ssl DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)

install(FILES templates DESTINATION system-test)
install(FILES bug729.php DESTINATION system-test)
install(DIRECTORY cnf DESTINATION system-test)
install(DIRECTORY fw DESTINATION system-test)
install(DIRECTORY test_ctrl_c DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY long_insert_sql DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY Hartmut_tests DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY session_hang DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY ssl-cert DESTINATION system-test USE_SOURCE_PERMISSIONS)

add_test(readconnrouter_master readconnrouter_master)
add_test(readconnrouter_slave readconnrouter_slave)
add_test(rwsplit_conn_num rwsplit_conn_num)

add_test(rw_galera_select_insert rw_galera_select_insert)
add_test(rwsplit_connect rwsplit_connect)
add_test(rw_select_insert rw_select_insert)
add_test(server_weight server_weight)
add_test(slave_failover slave_failover)
add_test(kill_master kill_master)
#add_test(slave_lag slave_lag)
add_test(bug359 bug359)
add_test(bug493 bug493)
add_test(bug495 bug495)
add_test(bug479 bug479)
add_test(bug572 bug572)
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
add_test(load_balancing_galera load_balancing_galera)
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
#add_test(bug699 bug699)
add_test(bug705 bug705)
add_test(setup_binlog_crc_none setup_binlog_crc_none)
add_test(setup_binlog_crc_32 setup_binlog_crc_32)
add_test(bug519 bug519)
add_test(bug729 bug729)
add_test(mariadb_tests_hartmut mariadb_tests_hartmut.sh)
add_test(mariadb_tests_hartmut_galera mariadb_tests_hartmut_galera.sh)
add_test(bug567 bug567.sh)
add_test(bug516 bug516.sh)
add_test(bug561 bug561.sh)
add_test(bug562 bug562.sh)
add_test(bug585 bug585)
add_test(bug587_1 bug587_1)
add_test(bug469 bug469)
add_test(run_session_hang run_session_hang.sh)
set_tests_properties(run_session_hang PROPERTIES TIMEOUT 3600)
add_test(bug564 bug564.sh)
add_test(bug730 bug730)
set_tests_properties(bug730 PROPERTIES TIMEOUT 3600)
add_test(bug711 bug711)
add_test(crash_out_of_files crash_out_of_files)
add_test(crash_out_of_files_galera crash_out_of_files_galera)
add_test(run_ctrl_c run_ctrl_c.sh)
add_test(binlog_incompl binlog_incompl)
add_test(mxs47 mxs47)
set_tests_properties(mxs47 PROPERTIES TIMEOUT 3600)
add_test(sharding sharding)
add_test(schemarouter_duplicate_db schemarouter_duplicate_db)
add_test(fwf fwf)
add_test(mxs118 mxs118)
add_test(mxs127 mxs127)
add_test(session_limits session_limits)
add_test(script script)
add_test(mm mm)
add_test(ssl_load ssl_load)
add_test(ssl_load_galera ssl_load_galera)
#add_test(config_reload config_reload)
#add_test(config_reload_stress config_reload_stress)
#add_test(reload_server reload_server.sh)
#add_test(reload_service reload_service.sh)
add_test(mxs314 mxs314)
#add_test(mxs321 mxs321)
set_tests_properties(mxs314 PROPERTIES TIMEOUT 3600)
add_test(sql_queries sql_queries)
set_tests_properties(sql_queries PROPERTIES TIMEOUT 3600)
add_test(ssl ssl)
set_tests_properties(ssl PROPERTIES TIMEOUT 3600)
add_test(short_sessions short_sessions)
set_tests_properties(short_sessions PROPERTIES TIMEOUT 3600)
add_test(short_sessions_ssl short_sessions_ssl)
set_tests_properties(short_sessions_ssl PROPERTIES TIMEOUT 3600)
add_test(bug694 bug694)
add_test(setup_binlog setup_binlog)
set_tests_properties(setup_binlog PROPERTIES TIMEOUT 3600)
add_test(bug648 bug648)
set_tests_properties(bug648 PROPERTIES TIMEOUT 3600)
add_test(sysbench_kill_slave sysbench_kill_slave)
set_tests_properties(sysbench_kill_slave PROPERTIES TIMEOUT 3600)
#set_tests_properties(config_reload config_reload_stress reload_server reload_service PROPERTIES LABELS "CONFIG_RELOAD")
add_test(pers_01 pers_01)
add_test(pers_02 pers_02)
add_test(mxs361 mxs361)
add_test(mxs431 mxs431)
add_test(open_close_connections open_close_connections)
add_test(open_close_connections_ssl open_close_connections_ssl)
add_test(galera_priority galera_priority)

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
