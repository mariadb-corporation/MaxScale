add_library(MySQLClient SHARED mysql_client.c)
target_link_libraries(MySQLClient maxscale-common  MySQLCommon)
set_target_properties(MySQLClient PROPERTIES VERSION "1.0.0")
install_module(MySQLClient core)
