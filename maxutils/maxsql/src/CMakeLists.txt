add_library(maxsql STATIC
  mariadb.cc
  )

target_link_libraries(maxsql maxbase ${MARIADB_CONNECTOR_LIBRARIES})
set_target_properties(maxsql PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)

