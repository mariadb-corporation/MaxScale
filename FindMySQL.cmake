# This CMake file tries to find the the mysqld binary
# The following variables are set:
# MYSQLD_FOUND - System has the mysqld executable
# MYSQLD_EXECUTABLE - The mysqld executable
# MYSQLD_VERSION - The MySQL version number
# MYSQLD_PROVIDER - The MySQL provider e.g. MariaDB

find_program(MYSQLD_EXECUTABLE mysqld)

if(MYSQLD_EXECUTABLE MATCHES "MYSQLD_EXECUTABLE-NOTFOUND")
  message(FATAL_ERROR "Cannot find the mysqld executable.")
  set(MYSQLD_FOUND FALSE CACHE INTERNAL "")
  unset(MYSQLD_EXECUTABLE)
else()
  execute_process(COMMAND ${MYSQLD_EXECUTABLE} --version OUTPUT_VARIABLE MYSQLD_VERSION)
  string(REPLACE "\n" "" MYSQLD_VERSION ${MYSQLD_VERSION})
  string(TOLOWER ${MYSQLD_VERSION} MYSQLD_VERSION)
  
  if(MYSQLD_VERSION MATCHES "mariadb")
    set(MYSQLD_PROVIDER "MariaDB" CACHE INTERNAL "The mysqld provider")
  elseif(MYSQLD_VERSION MATCHES "mysql")
    set(MYSQLD_PROVIDER "MySQL" CACHE INTERNAL "The mysqld provider")
  else()
    set(MYSQLD_PROVIDER "Unknown" CACHE INTERNAL "The mysqld provider")
  endif()

  string(REGEX REPLACE "[^0-9.]+([0-9.]+).+$" "\\1" MYSQLD_VERSION ${MYSQLD_VERSION})
  
  message(STATUS "MySQL version: ${MYSQLD_VERSION}")
  message(STATUS "MySQL provider: ${MYSQLD_PROVIDER}")
  if(MYSQLD_VERSION VERSION_LESS 5.5.40)
    message(WARNING "Required MySQL version is 5.5.40 or greater.")
  endif()
  if(NOT MYSQLD_PROVIDER MATCHES "MariaDB")
    message(WARNING "Using non-MariaDB MySQL server.")
  endif()
  set(MYSQLD_FOUND TRUE CACHE INTERNAL "")
endif()
