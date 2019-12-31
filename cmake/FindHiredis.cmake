# This CMake file locates the Hiredis libraries and headers
#
# The following variables are set:
# HIREDIS_FOUND       - If the Hiredis library was found
# HIREDIS_LIBRARIES   - Path to the library
# HIREDIS_INCLUDE_DIR - Path to Hiredis headers

find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h HINTS "/usr/include")

find_library(HIREDIS_LIBRARIES NAMES hiredis HINTS "/usr/lib")

if (HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARIES)
  message(STATUS "Found Hiredis: ${HIREDIS_LIBRARIES}")
  set(HIREDIS_FOUND TRUE)
else()
  message(STATUS "Could not find Hiredis")
endif()
