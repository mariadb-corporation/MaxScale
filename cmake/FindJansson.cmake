# This CMake file locates the Jansson libraries and headers
#
# The following variables are set:
# JANSSON_FOUND - If the Avro C library was found
# JANSSON_LIBRARIES - Path to the static library
# JANSSON_INCLUDE_DIR - Path to Avro headers

find_path(JANSSON_INCLUDE_DIR jansson.h)
find_library(JANSSON_LIBRARIES NAMES libjansson.a libjansson.so)

if (JANSSON_INCLUDE_DIR AND JANSSON_LIBRARIES)
  message(STATUS "Found Jansson: ${JANSSON_LIBRARIES}")
  set(JANSSON_FOUND TRUE)
else()
  message(STATUS "Could not find Jansson")
endif()
