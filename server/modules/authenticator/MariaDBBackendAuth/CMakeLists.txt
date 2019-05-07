add_library(mariadbbackendauth SHARED mysql_backend_auth.cc)
target_link_libraries(mariadbbackendauth maxscale-common mysqlcommon)
set_target_properties(mariadbbackendauth PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(mariadbbackendauth core)
