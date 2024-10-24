# This CMake file locates the Jansson libraries and headers
#
# The following variables are set:
# JANSSON_FOUND - If the Jansson library was found
# JANSSON_LIBRARIES - Path to the static library
# JANSSON_INCLUDE_DIR - Path to Jansson headers

find_path(JANSSON_INCLUDE_DIR jansson.h)

# Use the static library
find_library(JANSSON_LIBRARIES jansson)

if (JANSSON_INCLUDE_DIR AND JANSSON_LIBRARIES)
  message(STATUS "Found Jansson: ${JANSSON_LIBRARIES}")
  set(JANSSON_FOUND TRUE)
else()
  message(STATUS "Could not find Jansson")
endif()
