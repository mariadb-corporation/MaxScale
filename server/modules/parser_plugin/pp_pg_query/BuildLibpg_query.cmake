#
# Builds libpg_query: https://github.com/pganalyze/libpg_query
#
# The following relevant variables are set:
#
# LIBPG_QUERY_INCLUDE_DIR       - The include directory of Postgres.
# LIBPG_QUERY_STATIC_LIBRARIES  - The libraries to link to.
#

set(LIBPG_QUERY_TAG "15-4.2.0")
set(LIBPG_QUERY_FILE "${LIBPG_QUERY_TAG}.zip")
set(LIBPG_QUERY_URL "https://github.com/pganalyze/libpg_query/archive/refs/tags/${LIBPG_QUERY_FILE}")

#
# Some Postgres makefiles-rules depend upon the make nesting level.
# Since we here invoke the Postgres make from a cmake make, the
# starting nesting level will not be 0 and things will not work.
# Hence it is explicitly forced to be 0 with "MAKELEVEL=0".
#

ExternalProject_Add(libpg_query
  URL ${LIBPG_QUERY_URL}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/libpg_query
  CONFIGURE_COMMAND ""
  BUILD_IN_SOURCE 1
  PATCH_COMMAND ""
  BUILD_COMMAND make
  INSTALL_COMMAND ""
  )

set(LIBPG_QUERY_INCLUDE_DIR
  ${CMAKE_CURRENT_BINARY_DIR}/libpg_query
  ${CMAKE_CURRENT_BINARY_DIR}/libpg_query/vendor
  ${CMAKE_CURRENT_BINARY_DIR}/libpg_query/src/postgres/include
  CACHE INTERNAL "")
set(LIBPG_QUERY_STATIC_LIBRARIES ${CMAKE_CURRENT_BINARY_DIR}/libpg_query/libpg_query.a CACHE INTERNAL "")
