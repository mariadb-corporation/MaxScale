# This CMake file tries to find the the mysql_version.h header
# and to parse it for version and provider strings
# The following variables are set:
# MYSQL_VERSION - The MySQL version number
# MYSQL_PROVIDER - The MySQL provider e.g. MariaDB


find_file(MYSQL_VERSION_H mysql_version.h PATH_SUFFIXES mysql)
if(MYSQL_VERSION_H MATCHES "MYSQL_VERSION_H-NOTFOUND")
  message(FATAL_ERROR "Cannot find the mysql_version.h header")
else()
  message(STATUS "Found mysql_version.h: ${MYSQL_VERSION_H}")
endif()


file(READ ${MYSQL_VERSION_H} MYSQL_VERSION_CONTENTS)
string(REGEX REPLACE ".*MYSQL_SERVER_VERSION[^0-9.]+([0-9.]+).*" "\\1" MYSQL_VERSION ${MYSQL_VERSION_CONTENTS})
string(REGEX REPLACE ".*MYSQL_COMPILATION_COMMENT.+\"(.+)\".*" "\\1" MYSQL_PROVIDER ${MYSQL_VERSION_CONTENTS})
string(TOLOWER ${MYSQL_PROVIDER} MYSQL_PROVIDER)
if(MYSQL_PROVIDER MATCHES "mariadb")
  set(MYSQL_PROVIDER "MariaDB" CACHE INTERNAL "The MySQL provider")
elseif(MYSQL_PROVIDER MATCHES "mysql")
  set(MYSQL_PROVIDER "MySQL" CACHE INTERNAL "The MySQL provider")
elseif(MYSQL_PROVIDER MATCHES "percona")
  set(MYSQL_PROVIDER "Percona" CACHE INTERNAL "The MySQL provider")
else()
  set(MYSQL_PROVIDER "Unknown" CACHE INTERNAL "The MySQL provider")
endif()
message(STATUS "MySQL version: ${MYSQL_VERSION}")
message(STATUS "MySQL provider: ${MYSQL_PROVIDER}")

if(NOT MYSQL_PROVIDER STREQUAL "MariaDB")
message(WARNING "Not using MariaDB server.")
endif()
if(MYSQL_VERSION VERSION_LESS 5.5.41)
message(WARNING "MySQL version is ${MYSQL_VERSION}. Minimum supported version is 5.5.41")
endif()
