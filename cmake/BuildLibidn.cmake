set(LIBIDN_FILE "libidn/libidn-1.41.tar.gz")
set(LIBIDN_URLS
  "http://ftpmirror.gnu.org/${LIBIDN_FILE}"
  "http://ftp.funet.fi/pub/gnu/prep/${LIBIDN_FILE}"
  "http://mirrors.kernel.org/gnu/${LIBIDN_FILE}")

ExternalProject_Add(libidn
  URL ${LIBIDN_URLS}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/libidn/
  CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/libidn/configure --prefix=${CMAKE_BINARY_DIR}/libidn/
  --disable-shared --enable-static --with-pic --libdir=${CMAKE_BINARY_DIR}/libidn/lib/
  BINARY_DIR ${CMAKE_BINARY_DIR}/libidn/
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

set(LIBIDN_INCLUDE_DIR ${CMAKE_BINARY_DIR}/libidn/include/)
set(LIBIDN_LIBRARIES ${CMAKE_BINARY_DIR}/libidn/lib/libidn.a)

mark_as_advanced(LIBIDN_URLS LIBIDN_FILE LIBIDN_LIBRARIES LIBIDN_INCLUDE_DIR)
