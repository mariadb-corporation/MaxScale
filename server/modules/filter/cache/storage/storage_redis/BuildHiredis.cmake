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
  GIT_SHALLOW TRUE
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/hiredis
  CONFIGURE_COMMAND ""
  BUILD_IN_SOURCE 1
  BUILD_COMMAND make
# The install command is intentionally left out: for some strange and
# unknown reason it causes the library to be installed as a part of the
# MaxScale package in the location where it would be installed. This is
# definitely not wanted and in addition to that it can break the generated
# package by changing the ownership of home directories to root.
  INSTALL_COMMAND ""
  LOG_DOWNLOAD 1
  LOG_UPDATE 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1
  )

set(HIREDIS_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/hiredis CACHE INTERNAL "")
set(HIREDIS_STATIC_LIBRARIES ${CMAKE_CURRENT_BINARY_DIR}/hiredis/libhiredis.a)
