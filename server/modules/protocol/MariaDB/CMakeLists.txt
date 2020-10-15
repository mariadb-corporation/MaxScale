
add_library(mariadbprotocol SHARED
  local_client.cc mariadb_backend.cc  mariadb_client.cc mariadb_common.cc
  packet_parser.cc protocol_module.cc queryclassifier.cc resultset.cc
  rwbackend.cc sqlite_strlike.cc user_data.cc)
target_link_libraries(mariadbprotocol maxscale-common)
set_target_properties(mariadbprotocol PROPERTIES VERSION "2.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(mariadbprotocol core)

if (BUILD_TESTS)
  add_subdirectory(test)
endif()
