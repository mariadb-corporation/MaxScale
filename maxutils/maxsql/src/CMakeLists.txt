find_package(SQLite REQUIRED)
add_library(maxsql STATIC mariadb.cc mariadb_connector.cc odbc.cc odbc_helpers.cc sqlite_strlike.cc)

target_link_libraries(maxsql maxbase ${MARIADB_CONNECTOR_LIBRARIES} ${SQLITE_LIBRARIES} ssl dl odbc z)
set_target_properties(maxsql PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
add_dependencies(maxsql connector-c)

if (BUILD_TESTS)
    add_subdirectory(test)
endif()
