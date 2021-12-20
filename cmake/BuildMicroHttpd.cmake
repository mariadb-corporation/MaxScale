if (GNUTLS_VERSION_STRING VERSION_GREATER "3.0.0")
  set(LIBMICROHTTPD_VERSION "0.9.68")
else()
  set(LIBMICROHTTPD_VERSION "0.9.63")
  message(STATUS "GnuTLS is too old, using older libmicrohttpd")
endif()

message(STATUS "Using libmicrohttpd version ${LIBMICROHTTPD_VERSION}")

set(LIBMICROHTTPD_FILE "libmicrohttpd/libmicrohttpd-${LIBMICROHTTPD_VERSION}.tar.gz")
set(LIBMICROHTTPD_URLS
  "http://ftpmirror.gnu.org/${LIBMICROHTTPD_FILE}"
  "http://ftp.funet.fi/pub/gnu/prep/${LIBMICROHTTPD_FILE}"
  "http://mirrors.kernel.org/gnu/${LIBMICROHTTPD_FILE}")

ExternalProject_Add(libmicrohttpd
  URL ${LIBMICROHTTPD_URLS}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/libmicrohttpd/
  CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/libmicrohttpd//configure --prefix=${CMAKE_BINARY_DIR}/libmicrohttpd/ --enable-shared --with-pic --libdir=${CMAKE_BINARY_DIR}/libmicrohttpd/lib/
  BINARY_DIR ${CMAKE_BINARY_DIR}/libmicrohttpd/
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

include_directories(${CMAKE_BINARY_DIR}/libmicrohttpd/include/)
set(MICROHTTPD_LIBRARIES ${CMAKE_BINARY_DIR}/libmicrohttpd/lib/libmicrohttpd.a)

mark_as_advanced(LIBMICROHTTPD_VERSION LIBMICROHTTPD_URL MICROHTTPD_LIBRARIES LIBMICROHTTPD_FILE)
