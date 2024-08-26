#
# Find Hiredis: https://github.com/redis/hiredis
#
# The following relevant variables are set:
# HIREDIS_FOUND       - If the hiredis library was found
# HIREDIS_INCLUDE_DIR - The include directories
# HIREDIS_LIBRARIES   - The libraries to link
#

find_path(HIREDIS_INCLUDE_DIR hiredis.h PATH_SUFFIXES hiredis)
find_library(HIREDIS_LIBRARIES hiredis)

if (HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARIES)
  message(STATUS "Found hiredis: ${HIREDIS_LIBRARIES}")
  set(HIREDIS_FOUND TRUE)
else()
  message(STATUS "Could not find hiredis")
endif()
