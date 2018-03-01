add_library(MySQLCommon SHARED mysql_common.c)
target_link_libraries(MySQLCommon maxscale-common)
set_target_properties(MySQLCommon PROPERTIES VERSION "2.0.0")
install_module(MySQLCommon core)

add_subdirectory(MySQLBackend)
add_subdirectory(MySQLClient)
