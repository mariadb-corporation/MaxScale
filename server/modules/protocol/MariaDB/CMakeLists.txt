
add_library(mariadbclient SHARED protocol_module.cc mariadb_client.cc mariadb_backend.cc mariadb_common.cc
            local_client.cc rwbackend.cc user_data.cc sqlite_strlike.cc packet_parser.cc resultset.cc)
target_link_libraries(mariadbclient maxscale-common)
set_target_properties(mariadbclient PROPERTIES VERSION "2.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(mariadbclient core)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()
