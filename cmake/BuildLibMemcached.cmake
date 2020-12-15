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

set(LIBMEMCACHED_BASE "${CMAKE_BINARY_DIR}/libmemcached")

set(LIBMEMCACHED_SOURCE "${LIBMEMCACHED_BASE}/src")
set(LIBMEMCACHED_BINARY "${LIBMEMCACHED_BASE}/build")
set(LIBMEMCACHED_INSTALL "${LIBMEMCACHED_BASE}/install")

ExternalProject_Add(libmemcached
  URL ${LIBMEMCACHED_URL}
  SOURCE_DIR ${LIBMEMCACHED_SOURCE}
  CONFIGURE_COMMAND ${LIBMEMCACHED_SOURCE}/configure --prefix=${LIBMEMCACHED_INSTALL} --enable-shared --with-pic --libdir=${LIBMEMCACHED_INSTALL}/lib/
  PATCH_COMMAND sed -i "s/opt_servers == false/opt_servers == 0/" ${LIBMEMCACHED_SOURCE}/clients/memflush.cc
  BINARY_DIR ${LIBMEMCACHED_BINARY}
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

set(LIBMEMCACHED_INCLUDE_DIR ${LIBMEMCACHED_INSTALL}/include CACHE INTERNAL "")
set(LIBMEMCACHED_LIBRARIES ${LIBMEMCACHED_INSTALL}/lib/libmemcached.a)
