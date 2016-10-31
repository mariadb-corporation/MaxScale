# This CMake file locates the SQLite3 development libraries
#
# The following variables are set:
# SQLITE_FOUND - If the SQLite library was found
# SQLITE_LIBRARIES - Path to the static library
# SQLITE_INCLUDE_DIR - Path to SQLite headers
# SQLITE_VERSION - Library version

find_path(SQLITE_INCLUDE_DIR sqlite3.h)
find_library(SQLITE_LIBRARIES NAMES libsqlite3.so)

if (SQLITE_INCLUDE_DIR AND SQLITE_LIBRARIES)

  execute_process(COMMAND grep ".*#define.*SQLITE_VERSION " ${SQLITE_INCLUDE_DIR}/sqlite3.h
    COMMAND sed "s/.*\"\\(.*\\)\".*/\\1/"
    OUTPUT_VARIABLE SQLITE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  message(STATUS "Found SQLite version ${SQLITE_VERSION}: ${SQLITE_LIBRARIES}")
  set(SQLITE_FOUND TRUE)
else()
  message(STATUS "Could not find SQLite")
endif()
