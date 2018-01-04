add_library(mysqlcommon SHARED mysql_common.cc mariadb_client.cc)
target_link_libraries(mysqlcommon maxscale-common)
set_target_properties(mysqlcommon PROPERTIES VERSION "2.0.0")
install_module(mysqlcommon core)

add_subdirectory(mariadbbackend)
add_subdirectory(mariadbclient)
add_subdirectory(test)
