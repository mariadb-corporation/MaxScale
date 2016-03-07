include(ExternalProject)

ExternalProject_Add(sqlite
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sqlite-src-3110100/
  BINARY_DIR ${CMAKE_BINARY_DIR}/sqlite
  CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/sqlite-src-3110100/configure --with-pic
  BUILD_COMMAND make libsqlite3.la
  INSTALL_COMMAND "")
