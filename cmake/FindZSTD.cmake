#
# Find zstd: https://github.com/facebook/zstd
#
# The following relevant variables are set:
# ZSTD_FOUND       - If the zstd library was found
# ZSTD_INCLUDE_DIR - The include directories
# ZSTD_LIBRARIES   - The libraries to link
#

find_path(ZSTD_INCLUDE_DIR zstd.h)
find_library(ZSTD_LIBRARIES zstd)

if (ZSTD_INCLUDE_DIR AND ZSTD_LIBRARIES)
  message(STATUS "Found zstd: ${ZSTD_LIBRARIES}")
  set(ZSTD_FOUND TRUE)
elseif(ZSTD_FIND_REQUIRED)
  message(FATAL_ERROR "Could not find zstd")
else()
  message(STATUS "Could not find zstd")
endif()
