add_library(MySQLBackend SHARED mysql_backend.c ../mysql_common.c)
target_link_libraries(MySQLBackend maxscale-common MySQLAuth)
set_target_properties(MySQLBackend PROPERTIES VERSION "2.0.0")
install_module(MySQLBackend core)
