# This CMake file tries to find the the MySQL client library
# The following variables are set:
# MYSQLCLIENT_FOUND - System has MySQL client
# MYSQLCLIENT_STATIC_FOUND - System has statically linked MySQL client
# MARIADB_CONNECTOR_LIB - The MySQL client library
# MARIADB_CONNECTOR_STATIC_LIB - The static MySQL client library
# MYSQLCLIENT_HEADERS - The MySQL client headers

find_library(MARIADB_CONNECTOR_LIB NAMES mysqlclient PATH_SUFFIXES mysql mariadb)
if(${MARIADB_CONNECTOR_LIB} MATCHES "NOTFOUND")
  set(MYSQLCLIENT_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "Dynamic MySQL client library not found.")
  unset(MARIADB_CONNECTOR_LIB)
else()
  set(MYSQLCLIENT_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found dynamic MySQL client library: ${MARIADB_CONNECTOR_LIB}")
endif()

set(OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
find_library(MARIADB_CONNECTOR_STATIC_LIB NAMES mysqlclient PATH_SUFFIXES mysql mariadb)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})

if(${MARIADB_CONNECTOR_STATIC_LIB} MATCHES "NOTFOUND")
  set(MYSQLCLIENT_STATIC_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "Static MySQL client library not found.")
  unset(MARIADB_CONNECTOR_STATIC_LIB)
else()
  set(MYSQLCLIENT_STATIC_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found statc MySQL client library: ${MARIADB_CONNECTOR_STATIC_LIB}")
endif()

find_path(MYSQLCLIENT_HEADERS mysql.h PATH_SUFFIXES mysql mariadb)
