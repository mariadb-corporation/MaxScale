set(LIBGSASL_FILE "gsasl/libgsasl-1.10.0.tar.gz")
set(LIBGSASL_URLS
  "http://ftpmirror.gnu.org/${LIBGSASL_FILE}"
  "http://ftp.funet.fi/pub/gnu/prep/${LIBGSASL_FILE}"
  "http://mirrors.kernel.org/gnu/${LIBGSASL_FILE}")

ExternalProject_Add(libgsasl
  URL ${LIBGSASL_URLS}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/libgsasl/
  CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/libgsasl/configure --prefix=${CMAKE_BINARY_DIR}/libgsasl/
  --disable-shared --enable-static --with-pic --libdir=${CMAKE_BINARY_DIR}/libgsasl/lib/ --with-openssl=yes
  --disable-valgrind-tests --disable-dependency-tracking --enable-scram-sha256
  BINARY_DIR ${CMAKE_BINARY_DIR}/libgsasl/
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

set(LIBGSASL_INCLUDE_DIR ${CMAKE_BINARY_DIR}/libgsasl/include/)
set(LIBGSASL_LIBRARIES ${CMAKE_BINARY_DIR}/libgsasl/lib/libgsasl.a)

mark_as_advanced(LIBGSASL_URLS LIBGSASL_FILE LIBGSASL_LIBRARIES LIBGSASL_INCLUDE_DIR)
