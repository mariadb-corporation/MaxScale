# This CMake file locates the libavro.a static library and the avro.h header
#
# The following variables are set:
# AVRO_FOUND - If the Avro C library was found
# AVRO_LIBRARIES - Path to the static library
# AVRO_INCLUDE_DIR - Path to Avro headers

find_path(AVRO_INCLUDE_DIR avro.h)
find_library(AVRO_LIBRARIES libavro.a)

if (AVRO_INCLUDE_DIR AND AVRO_LIBRARIES)
  message(STATUS "Found Avro C libraries: ${AVRO_LIBRARIES}")
  set(AVRO_FOUND TRUE)
endif()
