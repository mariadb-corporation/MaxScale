add_library(maxsql STATIC
  mariadb.cc
  queryresult.cc
  )

target_link_libraries(maxsql maxbase ${MARIADB_CONNECTOR_LIBRARIES} ssl dl)
set_target_properties(maxsql PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
add_dependencies(maxsql connector-c)
