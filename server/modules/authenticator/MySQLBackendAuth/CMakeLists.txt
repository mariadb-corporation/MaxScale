add_library(mysqlbackendauth SHARED mysql_backend_auth.c)
target_link_libraries(mysqlbackendauth maxscale-common mysqlcommon)
set_target_properties(mysqlbackendauth PROPERTIES VERSION "1.0.0")
install_module(mysqlbackendauth core)
