# Find AddressSanitizer libraries
#
# The following variables are set:
# ASAN_FOUND - If AddressSanitizer was found
# ASAN_LIBRARIES - Path to the libasan library

find_library(ASAN_LIBRARIES NAMES libasan.so.0 libasan.so.1 libasan.so.2 libasan.so.3 libasan.so.4 libasan.so.5)

if (ASAN_LIBRARIES)
  message(STATUS "Found AddressSanitizer libraries: ${ASAN_LIBRARIES}")
  set(ASAN_FOUND TRUE CACHE INTERNAL "")
else()
  message(STATUS "Could not find AddressSanitizer")
endif()
