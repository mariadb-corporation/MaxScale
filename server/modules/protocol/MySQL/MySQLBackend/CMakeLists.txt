add_library(MySQLBackend SHARED mysql_backend.c)
# TODO: Refactor MySQLBackend so that COM_CHANGE_USER processing is
# transparent to the protocol module. After this change, we don't need to
# link against MySQLAuth.
target_link_libraries(MySQLBackend maxscale-common MySQLCommon MySQLAuth)
set_target_properties(MySQLBackend PROPERTIES VERSION "2.0.0")
install_module(MySQLBackend core)
