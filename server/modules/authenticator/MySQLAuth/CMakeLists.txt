add_library(MySQLAuth SHARED mysql_auth.c dbusers.c)
target_link_libraries(MySQLAuth maxscale-common MySQLCommon)
set_target_properties(MySQLAuth PROPERTIES VERSION "1.0.0")
install_module(MySQLAuth core)
