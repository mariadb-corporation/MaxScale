# This CMake file tries to find the the MariaDB Connector-C
# The following variables are set:
# MARIADB_CONNECTOR_FOUND - System has the connector
# MARIADB_CONNECTOR_STATIC_FOUND - System has static version of the connector library
# MARIADB_CONNECTOR_LIBRARIES - The dynamic connector libraries
# MARIADB_CONNECTOR_STATIC_LIBRARIES - The static connector libraries
# MARIADB_CONNECTOR_INCLUDE_DIR - The connector headers

find_library(MARIADB_CONNECTOR_LIBRARIES NAMES mysqlclient PATH_SUFFIXES mariadb mysql)
if(${MARIADB_CONNECTOR_LIBRARIES} MATCHES "NOTFOUND")
  set(MARIADB_CONNECTOR_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "Dynamic MySQL client library not found.")
  unset(MARIADB_CONNECTOR_LIBRARIES)
else()
  set(MARIADB_CONNECTOR_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found dynamic MySQL client library: ${MARIADB_CONNECTOR_LIBRARIES}")
endif()

set(OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
find_library(MARIADB_CONNECTOR_STATIC_LIBRARIES NAMES mysqlclient PATH_SUFFIXES mariadb mysql)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})

if(${MARIADB_CONNECTOR_STATIC_LIBRARIES} MATCHES "NOTFOUND")
  set(MARIADB_CONNECTOR_STATIC_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "Static MySQL client library not found.")
  unset(MARIADB_CONNECTOR_STATIC_LIBRARIES)
else()
  set(MARIADB_CONNECTOR_STATIC_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found statc MySQL client library: ${MARIADB_CONNECTOR_STATIC_LIBRARIES}")
endif()

find_path(MARIADB_CONNECTOR_INCLUDE_DIR mysql.h PATH_SUFFIXES mariadb mysql)

if(NOT (${MARIADB_CONNECTOR_INCLUDE_DIR}  MATCHES "NOTFOUND"))
  include(CheckSymbolExists)
  set(CMAKE_REQUIRED_INCLUDES ${MARIADB_CONNECTOR_INCLUDE_DIR})
  check_symbol_exists(LIBMARIADB mysql.h HAVE_MARIADB_CONNECTOR)
endif()

if(HAVE_MARIADB_CONNECTOR)
  message(STATUS "Found MariaDB Connector-C")
else()
  set(MARIADB_CONNECTOR_FOUND FALSE CACHE INTERNAL "" FORCE)
endif()
