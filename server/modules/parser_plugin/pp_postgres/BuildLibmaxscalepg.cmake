#
# Builds a library from the Postgres server
#
# - Only intended for the utilization of the Postgres parser.
# - Builds using -fPIC
# - Marks certain global variables with __thread, to enable use
#   in multi-threaded programs.
#
# The following relevant variables are set:
#
# POSTGRES_INCLUDE_DIR            - The include directory of Postgres.
# LIBMAXSCALEPG_STATIC_LIBRARIES  - The libraries to link to.
#

set(POSTGRES_TAG "REL_15_2")
set(POSTGRES_FILE "${POSTGRES_TAG}.zip")
set(POSTGRES_URL "https://github.com/postgres/postgres/archive/refs/tags/${POSTGRES_FILE}")

#
# Some Postgres makefiles-rules depend upon the make nesting level.
# Since we here invoke the Postgres make from a cmake make, the
# starting nesting level will not be 0 and things will not work.
# Hence it is explicitly forced to be 0 with "MAKELEVEL=0".
#

ExternalProject_Add(libmaxscalepg
  URL ${POSTGRES_URL}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/postgres
  CONFIGURE_COMMAND CFLAGS=-fPIC ${CMAKE_CURRENT_BINARY_DIR}/postgres/configure --enable-debug
  BUILD_IN_SOURCE 1
  PATCH_COMMAND patch -p 1 < ${CMAKE_CURRENT_SOURCE_DIR}/postgres.diff
  BUILD_COMMAND MAKELEVEL=0 make
  INSTALL_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build_libmaxscalepg.sh
  )

set(POSTGRES_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/postgres/src/include CACHE INTERNAL "")
set(LIBMAXSCALEPG_STATIC_LIBRARIES
  ${CMAKE_CURRENT_BINARY_DIR}/libmaxscalepg.a
  ${CMAKE_CURRENT_BINARY_DIR}/postgres/src/common/libpgcommon_srv.a
  CACHE INTERNAL "")
