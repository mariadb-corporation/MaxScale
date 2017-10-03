add_library(MySQLCommon SHARED mysql_common.cc mariadb_client.cc)
target_link_libraries(MySQLCommon maxscale-common)
set_target_properties(MySQLCommon PROPERTIES VERSION "2.0.0")
install_module(MySQLCommon core)

add_subdirectory(MySQLBackend)
add_subdirectory(MySQLClient)
add_subdirectory(test)
