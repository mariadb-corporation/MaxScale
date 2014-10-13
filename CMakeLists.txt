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

add_executable(readconnrouter_master readconnrouter_master.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(readconnrouter_master ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(readconnrouter_slave readconnrouter_slave.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(readconnrouter_slave ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(sql_queries sql_queries.cpp sql_t1.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(sql_queries ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(rw_galera_select_insert rw_galera_select_insert.cpp get_com_select_insert.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(rw_galera_select_insert ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(rwsplit_connect rwsplit_connect.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(rwsplit_connect ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(rw_select_insert rw_select_insert.cpp get_com_select_insert.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(rw_select_insert ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(server_weight server_weight.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(server_weight ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(slave_failover slave_failover.cpp testconnections.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(slave_failover ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(slave_lag slave_lag.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(slave_lag ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(bug359 bug359.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(bug359 ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(short_sessions short_sessions.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(short_sessions ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)

add_executable(connect_to_nonexisting_db connect_to_nonexisting_db.cpp testconnections.cpp sql_t1.cpp get_com_select_insert.cpp mariadb_nodes.cpp mariadb_func.cpp)
target_link_libraries(connect_to_nonexisting_db ${MYSQL_CLIENT} z crypt nsl m pthread ssl crypto dl rt)


install(TARGETS readconnrouter_master readconnrouter_slave sql_queries rw_galera_select_insert rwsplit_connect rw_select_insert server_weight slave_failover slave_lag bug359 short_sessions connect_to_nonexisting_db DESTINATION system-test)
install(FILES set_env_f.sh DESTINATION system-test)

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

