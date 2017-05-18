ExternalProject_Add(libmicrohttpd
  URL http://ftpmirror.gnu.org/libmicrohttpd/libmicrohttpd-0.9.54.tar.gz
  SOURCE_DIR ${CMAKE_BINARY_DIR}/libmicrohttpd/
  CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/libmicrohttpd//configure --prefix=${CMAKE_BINARY_DIR}/libmicrohttpd/ --enable-shared --with-pic
  BINARY_DIR ${CMAKE_BINARY_DIR}/libmicrohttpd/
  BUILD_COMMAND make
  INSTALL_COMMAND make install)

include_directories(${CMAKE_BINARY_DIR}/libmicrohttpd/include/)
set(MICROHTTPD_LIBRARIES ${CMAKE_BINARY_DIR}/libmicrohttpd/lib/libmicrohttpd.a)
