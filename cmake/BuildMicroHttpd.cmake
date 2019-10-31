if (GNUTLS_VERSION_STRING VERSION_GREATER "3.0.0")
  set(LIBMICROHTTPD_VERSION "0.9.68")
else()
  set(LIBMICROHTTPD_VERSION "0.9.63")
  message(STATUS "GnuTLS is too old, using older libmicrohttpd")
endif()

message(STATUS "Using libmicrohttpd version ${LIBMICROHTTPD_VERSION}")
set(LIBMICROHTTPD_URL "http://ftpmirror.gnu.org/libmicrohttpd/libmicrohttpd-${LIBMICROHTTPD_VERSION}.tar.gz"
  CACHE STRING "GNU libmicrochttpd source code")

ExternalProject_Add(libmicrohttpd
  URL ${LIBMICROHTTPD_URL}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/libmicrohttpd/
  CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/libmicrohttpd//configure --prefix=${CMAKE_BINARY_DIR}/libmicrohttpd/ --enable-shared --with-pic --libdir=${CMAKE_BINARY_DIR}/libmicrohttpd/lib/
  BINARY_DIR ${CMAKE_BINARY_DIR}/libmicrohttpd/
  BUILD_COMMAND make
  INSTALL_COMMAND make install)

include_directories(${CMAKE_BINARY_DIR}/libmicrohttpd/include/)
set(MICROHTTPD_LIBRARIES ${CMAKE_BINARY_DIR}/libmicrohttpd/lib/libmicrohttpd.a)

mark_as_advanced(LIBMICROHTTPD_VERSION LIBMICROHTTPD_URL MICROHTTPD_LIBRARIES)
