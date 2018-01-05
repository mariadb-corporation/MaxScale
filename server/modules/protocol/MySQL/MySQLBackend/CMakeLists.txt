add_library(mysqlbackend SHARED mysql_backend.c)
# TODO: Refactor MySQLBackend so that COM_CHANGE_USER processing is
# transparent to the protocol module. After this change, we don't need to
# link against MySQLAuth.
target_link_libraries(mysqlbackend maxscale-common mysqlcommon mysqlauth)
set_target_properties(mysqlbackend PROPERTIES VERSION "2.0.0")
install_module(mysqlbackend core)
