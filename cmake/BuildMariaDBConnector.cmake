# Build the MariaDB Connector-C
#
# If the MariaDB connector is not found, the last option is to download it
# and build it from source. This file downloads and builds the connector and
# sets the variables set by FindMariaDBConnector.cmake so that it appears that
# the system has the connector.

include(ExternalProject)

ExternalProject_Add(connector-c
  GIT_REPOSITORY https://github.com/MariaDB/mariadb-connector-c.git
  GIT_TAG v2.2.1
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/connector-c/install
  BINARY_DIR ${CMAKE_BINARY_DIR}/connector-c
  INSTALL_DIR ${CMAKE_BINARY_DIR}/connector-c/install)

set(MARIADB_CONNECTOR_FOUND TRUE CACHE INTERNAL "" FORCE)
set(MARIADB_CONNECTOR_STATIC_FOUND TRUE CACHE INTERNAL "" FORCE)
set(MARIADB_CONNECTOR_INCLUDE_DIR
  ${CMAKE_BINARY_DIR}/connector-c/install/include/mariadb CACHE INTERNAL "" FORCE)
set(MARIADB_CONNECTOR_STATIC_LIBRARIES
  ${CMAKE_BINARY_DIR}/connector-c/install/lib/mariadb/libmariadbclient.a
  CACHE INTERNAL "" FORCE)
set(MARIADB_CONNECTOR_LIBRARIES
  ${CMAKE_BINARY_DIR}/connector-c/install/lib/mariadb/libmariadbclient.a
  CACHE INTERNAL "" FORCE)
