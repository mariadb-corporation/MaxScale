# Build the MariaDB Connector-C
#
# If the MariaDB connector is not found, the last option is to download it
# and build it from source. This file downloads and builds the connector and
# sets the variables set by FindMariaDBConnector.cmake so that it appears that
# the system has the connector.

include(ExternalProject)

set(MARIADB_CONNECTOR_C_REPO "https://github.com/MariaDB/mariadb-connector-c.git"
  CACHE STRING "MariaDB Connector-C Git repository")

# Points to release 2.2.1 of the Connector-C
set(MARIADB_CONNECTOR_C_TAG "7fd72dfe3e5b889b974453b69f99c2e6fd4217c6"
  CACHE STRING "MariaDB Connector-C Git tag")

ExternalProject_Add(connector-c
  GIT_REPOSITORY ${MARIADB_CONNECTOR_C_REPO}
  GIT_TAG ${MARIADB_CONNECTOR_C_TAG}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/connector-c/install
  BINARY_DIR ${CMAKE_BINARY_DIR}/connector-c
  INSTALL_DIR ${CMAKE_BINARY_DIR}/connector-c/install
  UPDATE_COMMAND "")

set(MARIADB_CONNECTOR_FOUND TRUE CACHE INTERNAL "")
set(MARIADB_CONNECTOR_STATIC_FOUND TRUE CACHE INTERNAL "")
set(MARIADB_CONNECTOR_INCLUDE_DIR
  ${CMAKE_BINARY_DIR}/connector-c/install/include/mariadb CACHE INTERNAL "")
set(MARIADB_CONNECTOR_STATIC_LIBRARIES
  ${CMAKE_BINARY_DIR}/connector-c/install/lib/mariadb/libmariadbclient.a
  CACHE INTERNAL "")
set(MARIADB_CONNECTOR_LIBRARIES
  ${CMAKE_BINARY_DIR}/connector-c/install/lib/mariadb/libmariadbclient.a
  CACHE INTERNAL "")
