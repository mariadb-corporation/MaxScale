# This CMake file tries to find the the hiredis library
# The following variables are set:
# HIREDIS_FOUND - System has HIREDIS client
# HIREDIS_LIBRARIES - The HIREDIS client library
# HIREDIS_HEADERS - The HIREDIS client headers
include(CheckCSourceCompiles)
find_library(HIREDIS_LIBRARIES NAMES hiredis)
find_path(HIREDIS_HEADERS hiredis.h PATH_SUFFIXES hiredis)

if(${HIREDIS_LIBRARIES} MATCHES "NOTFOUND")
  set(HIREDIS_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "HIREDIS library not found.")
  unset(HIREDIS_LIBRARIES)
else()
  set(HIREDIS_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found hiredis library: ${HIREDIS_LIBRARIES}")
endif()

set(CMAKE_REQUIRED_INCLUDES ${HIREDIS_HEADERS})