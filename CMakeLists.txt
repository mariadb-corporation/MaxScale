project(maxscale_system_test)
cmake_minimum_required(VERSION 2.8)

include(macros.cmake)

set_variables()
set_maxscale_version()

# Installation directory
set(INSTALL_DIR "/usr/local/skysql/maxscale/" CACHE PATH "MaxScale tests installation directory.")
set(CMAKE_INSTALL_PREFIX "${INSTALL_DIR}" CACHE INTERNAL "Prefix prepended to install directories." FORCE)

check_deps()
check_dirs()


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

add_executable(rw_galera_select_insert rw_galera_select_insert.cpp get_com_select_insert.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(rw_galera_select_insert ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(rwsplit_connect rwsplit_connect.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(rwsplit_connect ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(rw_select_insert rw_select_insert.cpp get_com_select_insert.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(rw_select_insert ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(server_weight server_weight.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(server_weight ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(slave_failover slave_failover.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(slave_failover ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(sysbench_kill_slave sysbench_kill_slave.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(sysbench_kill_slave ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(slave_lag slave_lag.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp)
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

add_executable(bug475 bug475.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp) ##
target_link_libraries(bug475 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)


install(TARGETS readconnrouter_master readconnrouter_slave sql_queries rw_galera_select_insert rwsplit_connect rw_select_insert server_weight slave_failover sysbench_kill_slave slave_lag bug143 bug359 bug493 bug495 bug572 bug479 bug488 bug509 bug526 bug529 bug547 bug422 bug571 bug473 bug587 bug475 short_sessions connect_to_nonexisting_db rwsplit_conn_num temporal_tables change_master_during_session change_master DESTINATION system-test)
install(FILES set_env_f.sh set_env.sh run_ctrl_c long_insert mariadb_tests_hartmut start_without_root configure_maxscale.sh bug567 bug539 bug516 bug561 bug562 bug585 bug587_1 bug469 run_session_hang DESTINATION system-test PERMISSIONS WORLD_READ WORLD_EXECUTE OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
install(FILES templates DESTINATION system-test)
install(DIRECTORY cnf DESTINATION system-test )
install(DIRECTORY test_ctrl_c DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY long_insert_sql DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY Hartmut_tests DESTINATION system-test USE_SOURCE_PERMISSIONS)
install(DIRECTORY session_hang DESTINATION system-test USE_SOURCE_PERMISSIONS)

# See if we are on a RPM-capable or DEB-capable system
find_program(RPMBUILD rpmbuild)
find_program(DEBBUILD dpkg-buildpackage)

if(NOT ( ${RPMBUILD} STREQUAL "RPMBUILD-NOTFOUND" ) )
  message(STATUS "Generating RPM packages")
  set(CPACK_GENERATOR "${CPACK_GENERATOR};RPM")
endif()

if(NOT ( ${DEBBUILD} STREQUAL "DEBBUILD-NOTFOUND" ) )
  set(CPACK_GENERATOR "${CPACK_GENERATOR};DEB")
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

