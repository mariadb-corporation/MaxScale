# Build the maxutils library

ExternalProject_Add(maxutils
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/maxutils
  BINARY_DIR ${CMAKE_BINARY_DIR}/maxutils
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/maxutils/install)

set(MAXUTILS_INCLUDE_DIR ${CMAKE_BINARY_DIR}/maxutils/install/include CACHE INTERNAL "")
set(MAXUTILS_LIBRARIES ${CMAKE_BINARY_DIR}/maxutils/install/lib/libmaxbase.a CACHE INTERNAL "")
