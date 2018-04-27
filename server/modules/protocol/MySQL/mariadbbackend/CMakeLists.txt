add_library(mariadbbackend SHARED mysql_backend.cc)
# TODO: Refactor mariadbbackend so that COM_CHANGE_USER processing is
# transparent to the protocol module. After this change, we don't need to
# link against mysqlauth.
target_link_libraries(mariadbbackend maxscale-common mysqlcommon mysqlauth)
set_target_properties(mariadbbackend PROPERTIES VERSION "2.0.0")
install_module(mariadbbackend core)
