#
# Builds librdkafka: https://github.com/edenhill/librdkafka
#
# Following variables are set:
# RDKAFKA_INCLUDE_DIR - Path to librdkafka headers
# RDKAFKA_LIBRARIES   - Path to the librdkafka static library
#

set(RDKAFKA_URL "https://github.com/edenhill/librdkafka/archive/refs/tags/v1.6.1.tar.gz" CACHE STRING "librdkafka source tarball")

# Pass on the C and C++ compiler launchers to librdkafka, if set.
if (CMAKE_C_COMPILER_LAUNCHER)
  set(RDKAFKA_C_COMPILER "--cc=${CMAKE_C_COMPILER_LAUNCHER} ${CMAKE_C_COMPILER}")
endif()

if (CMAKE_CXX_COMPILER_LAUNCHER)
  set(RDKAFKA_CXX_COMPILER "--cxx=${CMAKE_CXX_COMPILER_LAUNCHER} ${CMAKE_CXX_COMPILER}")
endif()

ExternalProject_Add(librdkafka
  URL ${RDKAFKA_URL}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/librdkafka/
  BINARY_DIR ${CMAKE_BINARY_DIR}/librdkafka/
  CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/librdkafka/configure ${RDKAFKA_C_COMPILER} ${RDKAFKA_CXX_COMPILER} --prefix=${CMAKE_BINARY_DIR}/librdkafka/ --disable-zstd --disable-lz4-ext
  BUILD_COMMAND make
  INSTALL_COMMAND make install
  UPDATE_COMMAND ""
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

set(RDKAFKA_FOUND TRUE CACHE INTERNAL "")
set(RDKAFKA_INCLUDE_DIR ${CMAKE_BINARY_DIR}/librdkafka/include CACHE INTERNAL "")
set(RDKAFKA_LIBRARIES
  ${CMAKE_BINARY_DIR}/librdkafka/lib/librdkafka++.a
  ${CMAKE_BINARY_DIR}/librdkafka/lib/librdkafka.a
  sasl2
  CACHE INTERNAL "")

mark_as_advanced(RDKAFKA_C_COMPILER RDKAFKA_CXX_COMPILER)
