# This CMake file tries to find the the MySQL configuration tool
# The following variables are set:
# MYSQLCONFIG_FOUND - System has MySQL and the tool was found
# MYSQLCONFIG_EXECUTABLE - The MySQL configuration tool executable
# MYSQL_VERSION - The MySQL version number
find_program(MYSQLCONFIG_EXECUTABLE mysql_config)
if(MYSQLCONFIG_EXECUTABLE MATCHES "MYSQLCONFIG_EXECUTABLE-NOTFOUND")
  message(FATAL_ERROR "Cannot find mysql_config.")
  set(MYSQLCONFIG_FOUND FALSE CACHE INTERNAL "")
  unset(MYSQLCONFIG_EXECUTABLE)
else()
  execute_process(COMMAND ${MYSQLCONFIG_EXECUTABLE} --version OUTPUT_VARIABLE MYSQL_VERSION)
  string(REPLACE "\n" "" MYSQL_VERSION ${MYSQL_VERSION})
  message(STATUS "mysql_config found: ${MYSQLCONFIG_EXECUTABLE}")
  message(STATUS "MySQL version: ${MYSQL_VERSION}")
  if(MYSQL_VERSION VERSION_LESS 5.5.40)
    message(WARNING "Required MySQL version is 5.5.40 or greater.")
  endif()
  set(MYSQLCONFIG_FOUND TRUE CACHE INTERNAL "")
endif()
