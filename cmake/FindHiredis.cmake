#
# Find Hiredis: https://github.com/redis/hiredis
#
# The following relevant variables are set:
# HIREDIS_FOUND       - If the hiredis library was found and it supports TLS
# HIREDIS_SSL         - The location of the hiredis_ssl.h header if it's found
# HIREDIS_INCLUDE_DIR - The include directories
# HIREDIS_LIBRARIES   - The libraries to link
#

find_path(HIREDIS_INCLUDE_DIR hiredis.h PATH_SUFFIXES hiredis)
find_path(HIREDIS_SSL hiredis_ssl.h PATH_SUFFIXES hiredis)
find_library(HIREDIS_LIBRARIES hiredis)

if (HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARIES AND HIDERIS_SSL)
  message(STATUS "Found hiredis: ${HIREDIS_LIBRARIES}")
  set(HIREDIS_FOUND TRUE)
else()
  if (HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARIES)
    message(STATUS "Found hiredis but it does not support TLS, cannot use it")
  else()
    message(STATUS "Could not find hiredis")
  endif()
endif()
