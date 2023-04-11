add_library(postgresprotocol SHARED
  pgauthenticatormodule.cc
  pgbackendconnection.cc
  pgclientconnection.cc
  pgconfiguration.cc
  pgparser.cc
  pgprotocoldata.cc
  pgprotocolmodule.cc
  pgusermanager.cc
  postgresprotocol.cc
  authenticators/common.cc
  authenticators/password.cc
  authenticators/scram-sha-256.cc
  authenticators/trust.cc
  )

add_dependencies(postgresprotocol maxscale-common)
target_link_libraries(postgresprotocol maxscale-common maxpgsql)
target_include_directories(postgresprotocol PRIVATE ${CMAKE_SOURCE_DIR}/maxutils/maxpgsql/include)

set_target_properties(postgresprotocol PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(postgresprotocol core)
