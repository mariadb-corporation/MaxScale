# This CMake file tries to find the the zmq library
# The following variables are set:
# ZMQ_FOUND - System has zmq client
# ZMQ_LIBRARIES - The zmq client libraries
include(CheckCSourceCompiles)
find_library(ZMQ_LIBRARIES NAMES zmq)

if(${ZMQ_LIBRARIES} MATCHES "NOTFOUND")
  set(ZMQ_FOUND FALSE CACHE INTERNAL "")
  message(STATUS "ZMQ library not found.")
  unset(ZMQ_LIBRARIES)
else()
  set(ZMQ_FOUND TRUE CACHE INTERNAL "")
  message(STATUS "Found zmq library: ${ZMQ_LIBRARIES}")
endif()
