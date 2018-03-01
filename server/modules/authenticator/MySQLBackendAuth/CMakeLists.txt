add_library(MySQLBackendAuth SHARED mysql_backend_auth.c)
target_link_libraries(MySQLBackendAuth maxscale-common MySQLCommon)
set_target_properties(MySQLBackendAuth PROPERTIES VERSION "1.0.0")
install_module(MySQLBackendAuth core)
