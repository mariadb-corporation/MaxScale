# This CMake file tries to find the the MySQL configuration tool
# The following variables are set:
# MYSQLCONFIG_FOUND - System has MySQL and the tool was found
# MYSQLCONFIG_EXECUTABLE - The MySQL configuration tool executable
find_program(MYSQLCONFIG_EXECUTABLE mysql_config)
if(MYSQLCONFIG_EXECUTABLE MATCHES "MYSQLCONFIG_EXECUTABLE-NOTFOUND")
  message(FATAL_ERROR "Cannot find mysql_config.")
  set(MYSQLCONFIG_FOUND FALSE CACHE INTERNAL "")
  unset(MYSQLCONFIG_EXECUTABLE)
else()
  message(STATUS "mysql_config found: ${MYSQLCONFIG_EXECUTABLE}")
  set(MYSQLCONFIG_FOUND TRUE CACHE INTERNAL "")
endif()
