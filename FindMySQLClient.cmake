# This CMake file tries to find the the MySQL client library
# The following variables are set:
# MYSQLCLIENT_FOUND - System has MySQL client
# MYSQLCLIENT_STATIC_FOUND - System has statically linked MySQL client
# MYSQLCLIENT_LIBRARIES - The MySQL client library
# MYSQLCLIENT_STATIC_LIBRARIES - The static MySQL client library
# MYSQLCLIENT_HEADERS - The MySQL client headers

find_library(MYSQLCLIENT_LIBRARIES NAMES mysqlclient PATH_SUFFIXES mysql mariadb)
if(${MYSQLCLIENT_LIBRARIES} MATCHES "NOTFOUND")
  set(MYSQLCLIENT_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "Dynamic MySQL client library not found.")
  unset(MYSQLCLIENT_LIBRARIES)
else()
  set(MYSQLCLIENT_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found dynamic MySQL client library: ${MYSQLCLIENT_LIBRARIES}")
endif()

set(OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
find_library(MYSQLCLIENT_STATIC_LIBRARIES NAMES mysqlclient PATH_SUFFIXES mysql mariadb)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})

if(${MYSQLCLIENT_STATIC_LIBRARIES} MATCHES "NOTFOUND")
  set(MYSQLCLIENT_STATIC_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "Static MySQL client library not found.")
  unset(MYSQLCLIENT_STATIC_LIBRARIES)
else()
  set(MYSQLCLIENT_STATIC_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found statc MySQL client library: ${MYSQLCLIENT_STATIC_LIBRARIES}")
endif()

find_path(MYSQLCLIENT_HEADERS mysql.h PATH_SUFFIXES mysql mariadb)