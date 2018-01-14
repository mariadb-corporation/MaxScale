# Build the MariaDB Connector-C
#
# If the MariaDB connector is not found, the last option is to download it
# and build it from source. This file downloads and builds the connector and
# sets the variables set by FindMariaDBConnector.cmake so that it appears that
# the system has the connector.

set(MARIADB_CONNECTOR_C_REPO "https://github.com/MariaDB/mariadb-connector-c.git"
  CACHE STRING "MariaDB Connector-C Git repository")

# Release 2.3.3 (preliminary) of the Connector-C
set(MARIADB_CONNECTOR_C_TAG "master"
  CACHE STRING "MariaDB Connector-C Git tag")

ExternalProject_Add(connector-c
  GIT_REPOSITORY ${MARIADB_CONNECTOR_C_REPO}
  GIT_TAG ${MARIADB_CONNECTOR_C_TAG}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/connector-c/install -DWITH_UNITTEST=N -DWITH_CURL=N
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
