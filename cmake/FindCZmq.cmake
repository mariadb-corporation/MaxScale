# This CMake file tries to find the the czmq library
# The following variables are set:
# CZMQ_FOUND - System has czmq client
# CZMQ_LIBRARIES - The czmq client libraries
# CZMQ_HEADERS - The czmq client headers
include(CheckCSourceCompiles)
find_library(CZMQ_LIBRARIES NAMES czmq)
find_path(CZMQ_HEADERS czmq.h)

if(${CZMQ_LIBRARIES} MATCHES "NOTFOUND")
  set(CZMQ_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "czmq library not found.")
  unset(CZMQ_LIBRARIES)
else()
  set(CZMQ_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found czmq library: ${CZMQ_LIBRARIES}")
endif()

set(CMAKE_REQUIRED_INCLUDES ${CZMQ_HEADERS})