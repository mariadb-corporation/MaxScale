#
# Builds libmemcached: https://libmemcached.org/libMemcached.html
#
# Sources taken from https://launchpad.net/libmemcached/+download
#
# The following variables are set:
# LIBMEMCACHED_VERSION   - The libcached version used.
# LIBMEMCACHED_URL       - The download URL.
# LIBMEMCACHED_INCLUDE   - The include directories
# LIBMEMCACHED_LIBRARIES - The libraries to link
#

set(LIBMEMCACHED_VERSION "1.0.18")

message(STATUS "Using libmemcached version ${LIBMEMCACHED_VERSION}")

set(LIBMEMCACHED_URL "https://launchpad.net/libmemcached/1.0/${LIBMEMCACHED_VERSION}/+download/libmemcached-${LIBMEMCACHED_VERSION}.tar.gz")

ExternalProject_Add(libmemcached
  URL ${LIBMEMCACHED_URL}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/libmemcached/
  CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/libmemcached//configure --prefix=${CMAKE_BINARY_DIR}/libmemcached/ --enable-shared --with-pic --libdir=${CMAKE_BINARY_DIR}/libmemcached/lib/
  PATCH_COMMAND sed -i "s/opt_servers == false/opt_servers == 0/" ${CMAKE_BINARY_DIR}/libmemcached/clients/memflush.cc
  BINARY_DIR ${CMAKE_BINARY_DIR}/libmemcached/
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

set(LIBMEMCACHED_INCLUDE_DIR ${CMAKE_BINARY_DIR}/libmemcached/include CACHE INTERNAL "")
set(LIBMEMCACHED_LIBRARIES ${CMAKE_BINARY_DIR}/libmemcached/lib/libmemcached.a)
