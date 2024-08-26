#
# Finds libmemcached: https://libmemcached.org/libMemcached.html
#
# The following variables are set:
# LIBMEMCACHED_FOUND       - If the libmemcached library was found
# LIBMEMCACHED_INCLUDE_DIR - The include directories
# LIBMEMCACHED_LIBRARIES   - The libraries to link
#

find_path(LIBMEMCACHED_INCLUDE_DIR memcached.h PATH_SUFFIXES libmemcached)
find_library(LIBMEMCACHED_LIBRARIES memcached)

if (LIBMEMCACHED_INCLUDE_DIR AND LIBMEMCACHED_LIBRARIES)
  message(STATUS "Found libmemcached: ${LIBMEMCACHED_LIBRARIES}")
  set(LIBMEMCACHED_FOUND TRUE)
else()
  message(STATUS "Could not find libmemcached")
endif()
