# libssl-dev libmariadbclient-dev php5 perl coreutils realpath
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

# Enable Java
find_package(Java)
if(EXISTS ${Java_JAVA_EXECUTABLE} ${JAVA_JAVAC_EXECUTABLE} ${JAVA_JAR_EXECUTABLE})
  include(UseJava)
else()
  message(WARNING "Java not found, Java based tests are not run.")
endif()

configure_file(${CMAKE_SOURCE_DIR}/cnf/maxscale.cnf.template.setup_binlog.in ${CMAKE_BINARY_DIR}/cnf/maxscale.cnf.template.setup_binlog @ONLY)
#configure_file(${CMAKE_SOURCE_DIR}/set_env.sh.in ${CMAKE_BINARY_DIR}/set_env.sh @ONLY)
#configure_file(${CMAKE_SOURCE_DIR}/test_ctrl_c/test_ctrl_c.sh.in ${CMAKE_BINARY_DIR}/test_ctrl_c/test_ctrl_c.sh @ONLY)

include_directories("/usr/include/mysql/")
#LINK_DIRECTORIES("/usr/lib/x86_64-linux-gnu/")
aux_source_directory(. SRC_LIST)



add_library(testcore SHARED testconnections.cpp mariadb_nodes.cpp config_check.cpp
  mariadb_func.cpp get_com_select_insert.cpp maxadmin_operations.cpp big_transaction.cpp
  sql_t1.cpp test_binlog_fnc.cpp get_my_ip.cpp big_load.cpp  get_com_select_insert.cpp
  different_size.cpp fw_copy_rules)
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

# Same as add_test_executable, but do not add executable into tests list
function(add_test_executable_notest source name template)
  file(APPEND templates "${name} ${template}\n")
  add_executable(${name} ${source})
  target_link_libraries(${name} testcore)
  install(TARGETS ${name} DESTINATION system-test)
endfunction()

# This function adds a script as a test with the specified name and template.
# The naming of the templates follow the same principles as add_test_executable.
# also suitable for symlinks
function(add_test_script name template)
  file(APPEND templates "${name} ${template}\n")
  install(PROGRAMS ${name} DESTINATION system-test)
  add_test(${name} ${name})
endfunction()

add_test_executable_notest(t.cpp t replication)
add_test_executable_notest(non_native_setup.cpp non_native_setup replication)

add_test_executable_notest(binlog_big_transaction.cpp binlog_big_transaction setup_binlog2)

if(Java_FOUND)
  add_subdirectory(maxscale/java/)
endif()

install(FILES set_env.sh get_logs.sh long_insert.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)

install(FILES configure_maxscale.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES copy_logs.sh DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)

install(FILES bug729.php DESTINATION system-test)
install(DIRECTORY cnf DESTINATION system-test)
install(DIRECTORY fw DESTINATION system-test)
install(DIRECTORY test_ctrl_c DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY long_insert_sql DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY Hartmut_tests DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY session_hang DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY ssl-cert DESTINATION system-test USE_SOURCE_PERMISSIONS)

add_test_executable(regexfilter1.cpp regexfilter1 regexfilter1)
add_test_executable(namedserverfilter.cpp namedserverfilter namedserverfilter)
add_test_executable(mxs365.cpp mxs365 replication)
# ReadConn cases are commented out now due to Maxscale failures
add_test_executable(longblob.cpp longblob longblob)
set_tests_properties(longblob PROPERTIES TIMEOUT 7200)
add_test_executable(maxscale_process_user.cpp maxscale_process_user replication)
add_test_executable(mxs244_prepared_stmt_loop.cpp mxs244_prepared_stmt_loop galera)
add_test_executable(mxs280_select_outfile.cpp mxs280_select_outfile replication)
add_test_executable(mxs501_tee_usedb.cpp mxs501_tee_usedb mxs501)
add_test_executable(readconnrouter_master.cpp readconnrouter_master replication)
add_test_executable(readconnrouter_slave.cpp readconnrouter_slave replication)
add_test_executable(rwsplit_conn_num.cpp rwsplit_conn_num repl_lgc)
add_test_executable(rw_galera_select_insert.cpp rw_galera_select_insert galera)
add_test_executable(rwsplit_connect.cpp rwsplit_connect replication)
add_test_executable(rw_select_insert.cpp rw_select_insert replication)
add_test_executable(server_weight.cpp server_weight galera.weight)
add_test_executable(slave_failover.cpp slave_failover replication.one_slave)
add_test_executable(kill_master.cpp kill_master replication)
# it is not possible to create laq in easy reliable way
#add_test_executable(slave_lag.cpp slave_lag lag)
add_test_executable(bug359.cpp bug359 bug359)
add_test_executable(bug493.cpp bug493 bug493)
add_test_executable(bug495.cpp bug495 bug495)
add_test_executable(bug479.cpp bug479 bug479)
add_test_executable(bug572.cpp bug572 replication)
add_test_executable(connect_to_nonexisting_db.cpp connect_to_nonexisting_db replication)
add_test_executable(temporal_tables.cpp temporal_tables replication)
add_test_executable(change_master_during_session.cpp change_master_during_session replication)
add_test_executable(bug143.cpp bug143 replication)
add_test_executable(bug488.cpp bug488 galera)
add_test_executable(bug507.cpp bug507 replication)
add_test_executable(bug509.cpp bug509 galera)
add_test_executable(bug526.cpp bug526 bug526)
add_test_executable(bug529.cpp bug529 replication)
add_test_executable(bug547.cpp bug547 replication)
add_test_executable(bug422.cpp bug422 replication)
add_test_executable(bug571.cpp bug571 bug571)
add_test_executable(bug473.cpp bug473 hints)
add_test_executable(bug587.cpp bug587 bug587)
add_test_executable(bug471.cpp bug471 bug471)
add_test_executable(bug475.cpp bug475 hints)
add_test_executable(change_user.cpp change_user replication)
add_test_executable(bug565.cpp bug565 replication)
add_test_executable(load_balancing.cpp load_balancing load)
add_test_executable(load_balancing_galera.cpp load_balancing_galera load_galera)
add_test_executable(bug448.cpp bug448 replication)
add_test_executable(prepared_statement.cpp prepared_statement replication)
add_test_executable(bug592.cpp bug592 replication)
add_test_executable(bug620.cpp bug620 bug620)
add_test_executable(bug634.cpp bug634 replication)
add_test_executable(bug626.cpp bug626 replication)
add_test_executable(bug643.cpp bug643 bug643)
add_test_executable(bug643_1.cpp bug643_1 bug643_1)
add_test_executable(bug645.cpp bug645 bug645)
add_test_executable(bug645_1.cpp bug645_1 bug645_1)
add_test_executable(bug601.cpp bug601 bug601)
set_tests_properties(bug601 PROPERTIES TIMEOUT 3600)
add_test_executable(bug649.cpp bug649 bug645)
add_test_executable(bug650.cpp bug650 bug650)
add_test_executable(bug653.cpp bug653 replication)
add_test_executable(bug657.cpp bug657 bug657)
add_test_executable(bug676.cpp bug676 galera.bug676)
add_test_executable(bug654.cpp bug654 replication)
add_test_executable(bug658.cpp bug658 replication)
add_test_executable(bug662.cpp bug662 replication)
add_test_executable(bug664.cpp bug664 bug664)
add_test_executable(bug681.cpp bug681 galera.bug681)
add_test_executable(bug673.cpp bug673 bug673)

# Tee-filter related, fails:  query got stuck, log says  error  : Slave server 'server2': response differs from master's response. Closing connection due to inconsistent session state.
# ReadConn router in master mode with TEE filter
add_test_executable(bug670.cpp bug670 bug670)

# FAKE_CODE assert, sending 'fail clientfd' maxadmin command (should it be removed from Maxscale?)
#add_test_executable(bug539.cpp bug539 bug539)

# valid case, problem is not resolved, tracked by https://mariadb.atlassian.net/browse/MXS-15
add_test_executable(bug699.cpp bug699 galera)
add_test_executable(bug705.cpp bug705 bug705)
add_test_executable(setup_binlog_crc_none.cpp setup_binlog_crc_none setup_binlog)
add_test_executable(setup_binlog_crc_32.cpp setup_binlog_crc_32 setup_binlog)
add_test_executable(bug519.cpp bug519 replication)
add_test_executable(bug729.cpp bug729 replication)
add_test_script(mariadb_tests_hartmut.sh replication)
add_test_script(mariadb_tests_hartmut_galera.sh replication)
add_test_script(bug567.sh replication)
add_test_script(bug516.sh replication)
add_test_script(bug561.sh replication)
add_test_script(bug562.sh replication)
add_test_script(bug585 bug585)
add_test_script(bug587_1 bug587_1)
add_test_executable(bug469.cpp bug469 replication)
add_test_script(run_session_hang.sh replication)
set_tests_properties(run_session_hang.sh PROPERTIES TIMEOUT 3600)
add_test_script(bug564.sh replication)
add_test_executable(bug730.cpp bug730 bug730)
set_tests_properties(bug730 PROPERTIES TIMEOUT 3600)
add_test_executable(bug711.cpp bug711 bug711)
add_test_executable(crash_out_of_files.cpp crash_out_of_files load)
add_test_executable(crash_out_of_files_galera.cpp crash_out_of_files_galera galera)
add_test_script(run_ctrl_c.sh replication)
add_test_executable(binlog_incompl.cpp binlog_incompl binlog_incompl)
add_test_executable(mxs47.cpp mxs47 replication)
set_tests_properties(mxs47 PROPERTIES TIMEOUT 3600)
add_test_executable(sharding.cpp sharding sharding)
add_test_executable(schemarouter_duplicate_db.cpp schemarouter_duplicate_db schemarouter_duplicate_db)
add_test_executable(fwf.cpp fwf fwf)
add_test_executable(mxs118.cpp mxs118 mxs118)
add_test_executable(mxs127.cpp mxs127 mxs127)
add_test_executable(session_limits.cpp session_limits session_limits)
add_test_executable(script.cpp script script)
add_test_executable(mm.cpp mm mm)
add_test_script(ssl_load ssl_load)
add_test_script(ssl_load_galera ssl_load_galera)
#add_test_executable(config_reload.cpp config_reload config_reload)
#add_test_executable(config_reload_stress.cpp config_reload_stress config_reload)
#add_test_script(reload_server.cpp reload_server.sh add_server)
#add_test_script(reload_service.cpp reload_service.sh add_service)
add_test_executable(mxs314.cpp mxs314 galera)
add_test_executable(mxs321.cpp mxs321 replication)
set_tests_properties(mxs314 PROPERTIES TIMEOUT 3600)
add_test_executable(sql_queries.cpp sql_queries replication)
set_tests_properties(sql_queries PROPERTIES TIMEOUT 3600)
add_test_script(ssl ssl)
set_tests_properties(ssl PROPERTIES TIMEOUT 3600)
add_test_executable(short_sessions.cpp short_sessions replication)
set_tests_properties(short_sessions PROPERTIES TIMEOUT 3600)
add_test_script(short_sessions_ssl ssl)
set_tests_properties(short_sessions_ssl PROPERTIES TIMEOUT 3600)
add_test_executable(bug694.cpp bug694 bug694)
add_test_executable(setup_binlog.cpp setup_binlog setup_binlog)
set_tests_properties(setup_binlog PROPERTIES TIMEOUT 3600)
add_test_script(bug648 bug648)
set_tests_properties(bug648 PROPERTIES TIMEOUT 3600)
add_test_executable(sysbench_kill_slave.cpp sysbench_kill_slave replication)
set_tests_properties(sysbench_kill_slave PROPERTIES TIMEOUT 3600)
#set_tests_properties(config_reload config_reload_stress reload_server reload_service PROPERTIES LABELS "CONFIG_RELOAD")
add_test_executable(pers_01.cpp pers_01 pers_01)
add_test_executable(pers_02.cpp pers_02 pers_01)
add_test_script(mxs361 mxs361)
add_test_executable(mxs431.cpp mxs431 sharding)
# curretly does 'USE test' in every session, should be removed when 'no all backens are authentificated' issue is resolved
add_test_executable(open_close_connections.cpp open_close_connections replication)
add_test_script(open_close_connections_ssl ssl)
add_test_executable(galera_priority.cpp galera_priority galera_priority)
add_test_executable(mxs548_short_session_change_user.cpp mxs548_short_session_change_user mxs548)
add_test_executable_notest(mxs559_block_master.cpp mxs559_block_master mxs559)
add_test_script(sql_queries_pers1 sql_queries_pers1)
add_test_script(sql_queries_pers10 sql_queries_pers10)
set_tests_properties(sql_queries_pers1 PROPERTIES TIMEOUT 3600)
set_tests_properties(sql_queries_pers10 PROPERTIES TIMEOUT 3600)
add_test_script(sql_queries_pers10 sql_queries_pers10)
add_test_script(load_balancing_pers1 load_pers1)
add_test_script(load_balancing_pers10 load_pers10)
add_test_script(load_balancing_galera_pers1 load_galera_pers1)
add_test_script(load_balancing_galera_pers10 load_galera_pers10)
add_test_executable(fwf_logging.cpp fwf_logging fwf_logging)
add_test_executable(fwf_actions.cpp fwf_actions fwf_action)

# https://mariadb.atlassian.net/browse/MXS-576 - it is possible to set negative value for
# 'persistpoolmax' without any warning
#add_test_executable(bad_pers.cpp bad_pers bad_pers)
add_test_executable(mxs564_big_dump.cpp mxs564_big_dump galera_mxs564)
add_test_executable_notest(long_sysbench.cpp long_sysbench replication)
add_test_executable_notest(check_backend.cpp check_backend check_backend)
add_test_executable(config_test.cpp config_test replication)
add_test_executable(fwf_syntax.cpp fwf_syntax fwf_syntax)

# enable after fixing MXS-419
# add_test_executable(mxs419_lots_of_connections.cpp mxs419_lots_of_connections replication)
add_test_executable(mxs37_table_privilege.cpp mxs37_table_privilege replication)

add_test_executable(different_size_rwsplit.cpp different_size_rwsplit replication)
set_tests_properties(different_size_rwsplit PROPERTIES TIMEOUT 3600)
add_test_executable(different_size_binlog.cpp different_size_binlog setup_binlog)
set_tests_properties(different_size_binlog PROPERTIES TIMEOUT 3600)
add_test_executable(max_connections.cpp max_connections replication)
set_tests_properties(max_connections PROPERTIES TIMEOUT 3600)
add_test_executable(lots_of_rows.cpp lots_of_rows galera)

# Python based tests
add_test_script(mxs585.py replication)

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
