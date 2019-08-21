
add_library(mariadbclient SHARED mysql_common.cc mariadb_client.cc mysql_backend.cc rwbackend.cc
        mysql_client.cc )
target_link_libraries(mariadbclient maxscale-common)
set_target_properties(mariadbclient PROPERTIES VERSION "2.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(mariadbclient core)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()
