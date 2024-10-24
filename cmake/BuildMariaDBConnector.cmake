# Build the MariaDB Connector-C
#
# If the MariaDB connector is not found, the last option is to download it
# and build it from source. This file downloads and builds the connector and
# sets the variables set by FindMariaDBConnector.cmake so that it appears that
# the system has the connector.

ExternalProject_Add(connector-c
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/mariadb-connector-c/
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/connector-c/install -DWITH_UNIT_TESTS=N -DWITH_CURL=N -DWITH_EXTERNAL_ZLIB=Y
  BINARY_DIR ${CMAKE_BINARY_DIR}/connector-c
  INSTALL_DIR ${CMAKE_BINARY_DIR}/connector-c/install
  UPDATE_COMMAND ""
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

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

install_directory(${CMAKE_BINARY_DIR}/connector-c/install/lib/mariadb/plugin ${MAXSCALE_LIBDIR} core)
