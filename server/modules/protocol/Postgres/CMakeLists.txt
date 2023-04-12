include(${CMAKE_SOURCE_DIR}/cmake/BuildLibidn.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BuildLibgsasl.cmake)

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

add_dependencies(postgresprotocol maxscale-common libgsasl libidn)
target_link_libraries(postgresprotocol maxscale-common maxpgsql ${LIBGSASL_LIBRARIES} ${LIBIDN_LIBRARIES})
target_include_directories(postgresprotocol PRIVATE ${CMAKE_SOURCE_DIR}/maxutils/maxpgsql/include)
include_directories(${LIBGSASL_INCLUDE_DIR} ${LIBIDN_INCLUDE_DIR})

set_target_properties(postgresprotocol PROPERTIES VERSION "1.0.0" LINK_FLAGS -Wl,-z,defs)
install_module(postgresprotocol core)
