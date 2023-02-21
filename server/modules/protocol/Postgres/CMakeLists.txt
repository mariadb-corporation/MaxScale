add_library(postgresprotocol SHARED
  pgauthenticatormodule.cc
  pgclientconnection.cc
  pgconfiguration.cc
  pgprotocoldata.cc
  pgprotocolmodule.cc

  postgresprotocol.cc
  )

add_dependencies(postgresprotocol maxscale-common)
target_link_libraries(postgresprotocol
  maxscale-common
  )

set_target_properties(postgresprotocol PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(postgresprotocol core)
