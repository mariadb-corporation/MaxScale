add_library(maxsql STATIC mariadb.cc queryresult.cc sqlite.cc mariadb_connector.cc)

target_link_libraries(maxsql maxbase ${MARIADB_CONNECTOR_LIBRARIES} ${SQLITE_LIBRARIES} ssl dl z)
set_target_properties(maxsql PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
add_dependencies(maxsql connector-c)
