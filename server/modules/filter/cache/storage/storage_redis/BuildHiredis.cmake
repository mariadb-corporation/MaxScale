#
# Downloads and builds Hiredis: https://github.com/redis/hiredis
#
# The following relevant variables are set:
# HIREDIS_INCLUDE          - The include directories
# HIREDIS_STATIC_LIBRARIES - The static libraries to link
#

set(HIREDIS_REPO "https://github.com/redis/hiredis.git" CACHE STRING "Hiredis Git repository")
set(HIREDIS_TAG "v0.14.0" CACHE STRING "Hiredis Git tag")

message(STATUS "Using hiredis version ${HIREDIS_VERSION}")

ExternalProject_add(hiredis
  GIT_REPOSITORY ${HIREDIS_REPO}
  GIT_TAG ${HIREDIS_TAG}
  CONFIGURE_COMMAND ""
  BUILD_IN_SOURCE 1
  BUILD_COMMAND make
  INSTALL_COMMAND make PREFIX=${CMAKE_CURRENT_BINARY_DIR}/hiredis install
  )

set(HIREDIS_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/hiredis/include CACHE INTERNAL "")
set(HIREDIS_STATIC_LIBRARIES ${CMAKE_CURRENT_BINARY_DIR}/hiredis/lib/libhiredis.a)
